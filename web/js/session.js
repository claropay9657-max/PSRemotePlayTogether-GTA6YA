// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// One guest's connection to a host: finding it through the meeting point,
// the direct connection (WebRTC), and the co-play protocol on top of it.
// Picture, sound and controllers go straight from browser to host and back;
// only the two notes that set the connection up pass a server.

import { hex } from './bytes.js';
import * as meet from './meet.js';
import * as P from './proto.js';
import { FrameAssembler } from './video.js';

const STUN_SERVERS = ['stun:stun.l.google.com:19302', 'stun:stun.cloudflare.com:3478'];

const HELLO_INTERVAL_MS = 250;
/** How long a guest waits for a host that lets each newcomer in by hand. */
const ADMISSION_TIMEOUT_MS = 120000;
const PING_INTERVAL_MS = 500;
const FAST_PING_INTERVAL_MS = 100;
const FAST_PINGS = 5;
/** Refresh of an unchanged controller state; must stay well below the host's stale time. */
const INPUT_REFRESH_MS = 40;
/** A changed state is sent this many times in quick succession to survive packet loss. */
const INPUT_BURST = 3;
const INPUT_BURST_SPACING_MS = 4;
const HOST_TIMEOUT_MS = 6000;
const CHAT_RESEND_MS = 400;
const CHAT_GIVE_UP_MS = 20000;

/** Why joining did not work, in words for the person in front of the screen. */
export class JoinError extends Error {
  constructor(message, kind = 'failed') {
    super(message);
    this.kind = kind;
  }
}

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const nowMs = () => performance.now();

/** A sequence number that may start over after a pause, like VoiceSequence in the app. */
class LooseSequence {
  constructor() {
    this.have = false;
    this.last = 0;
    this.lastMs = 0;
  }

  accept(seq, now) {
    if (this.have && now - this.lastMs < 2000 && ((seq - this.last) | 0) <= 0) return false;
    this.have = true;
    this.last = seq;
    this.lastMs = now;
    return true;
  }
}

export class Session {
  /**
   * @param {object} options
   * @param {Uint8Array} options.secret the invite's key
   * @param {string} options.meetUrl
   * @param {string} options.name
   * @param {Uint8Array} options.playerId 8 random bytes, the same for as long as the page is open
   * @param {object} handlers called as things happen; every one is optional
   */
  constructor(options, handlers) {
    this.options = options;
    this.h = handlers || {};
    this.pc = null;
    this.dc = null;
    this.closed = false;
    this.connected = false;
    this.welcome = null;
    this.state = null;
    this.haveState = false;
    this.want = options.want !== undefined ? options.want : (P.WANT_VIDEO | P.WANT_AUDIO | P.WANT_VOICE | P.WANT_CAMS);
    this.controller = P.CONTROLLER_NONE;
    this.media = 0;
    this.battery = P.BATTERY_UNKNOWN;
    this.rttMs = 0;
    this.pingId = 0;
    this.pingsSent = 0;
    this.lastPingMs = 0;
    this.pingNow = false;
    this.lastRxMs = 0;
    this.pad = P.emptyPad();
    this.inputSeq = 0;
    this.inputBurst = 0;
    this.lastInputMs = 0;
    this.assembler = new FrameAssembler();
    // A browser hands messages over in batches, between other work: a gap of a few
    // milliseconds inside a frame is no sign of loss yet
    this.assembler.nackDelayMs = 20;
    this.videoConfigId = 0;
    this.haveVideoConfig = false;
    this.haveAudio = false;
    this.lastAudioSeq = 0;
    this.voiceIn = new Map();
    this.voiceSeq = 0;
    this.camSeq = 0;
    this.haveHaptics = false;
    this.lastHapticsSeq = 0;
    this.haveInputs = false;
    this.lastInputsSeq = 0;
    this.chatSeen = [];
    this.chatOut = new Map();
    this.chatUpSeq = 0;
    // Read marks and reactions (coplay Client): as the host told them, the newest mark number
    // applied for each thing, how far this player has read, and its reactions on their way
    this.chatMarks = P.newChatMarks();
    this.marksApplied = new Map();
    this.readWanted = 0;
    this.readSentMs = 0;
    this.readFirstMs = 0;
    this.reactionsOut = [];
    this.deletesOut = [];
    this.timer = 0;
    this.abort = new AbortController();
    this.stats = { packets: 0, bytes: 0, audioFrames: 0 };
    this.blobs = new Map(); // what is being fetched: key -> {ref, data, got, asked}
    this.blobsDone = new Map(); // key -> Uint8Array
    this.lastBlobPollMs = 0;
  }

  status(text) {
    if (this.h.onStatus && !this.closed) this.h.onStatus(text);
  }

  // ------------------------------------------------------------------ getting in

  /**
   * Finds the host and connects. Resolves once the host has let this guest in.
   * @throws {JoinError}
   */
  async connect() {
    if (typeof RTCPeerConnection !== 'function') {
      throw new JoinError('This browser has no WebRTC, which the direct connection to the host needs.', 'unsupported');
    }
    const { secret, meetUrl } = this.options;
    const { room, noteKey } = await meet.deriveSession(secret);
    const peerId = crypto.getRandomValues(new Uint8Array(8));
    peerId[0] |= 1; // never all zeros
    const myId = hex(peerId);

    this.status('Getting ready…');
    // Whom to ask for this browser's address on the internet: the host's choice if
    // its link names one ("off": nobody), otherwise the servers the app asks too
    const stun = this.options.stun || '';
    const iceServers = stun === 'off' ? [] : stun ? [{ urls: 'stun:' + stun }] : [{ urls: STUN_SERVERS }];
    const pc = new RTCPeerConnection({ iceServers });
    this.pc = pc;
    // Like UDP: no order, nothing sent again. The protocol repairs what it needs itself.
    const dc = pc.createDataChannel('psrpt', { ordered: false, maxRetransmits: 0 });
    dc.binaryType = 'arraybuffer';
    this.dc = dc;

    let opened = false;
    let failed = false;
    let wake = () => {};
    const woken = () => new Promise((resolve) => { wake = resolve; });
    dc.onopen = () => {
      opened = true;
      wake();
    };
    pc.onconnectionstatechange = () => {
      if (pc.connectionState === 'failed') {
        failed = true;
        wake();
      }
      if (this.connected && (pc.connectionState === 'failed' || pc.connectionState === 'closed')) this.lost('timeout');
    };
    dc.onclose = () => {
      if (this.connected) this.lost('timeout');
    };

    await pc.setLocalDescription(await pc.createOffer());
    const mine = meet.describeOffer(pc.localDescription.sdp);
    if (!mine) throw new JoinError('This browser describes its connection in a way this page does not understand.', 'unsupported');
    let noteStale = true;
    let gathered = pc.iceGatheringState === 'complete';
    pc.onicecandidate = (e) => {
      if (e.candidate && e.candidate.candidate) {
        if (meet.addCandidate(mine.side.candidates, e.candidate.candidate)) noteStale = true;
      } else gathered = true;
      wake();
    };
    // A moment for the browser to learn its address on the internet; the note is
    // written again if more turns up later
    const gatherStart = nowMs();
    while (!gathered && nowMs() - gatherStart < 1500
      && !mine.side.candidates.some((c) => c.type === meet.CAND_REFLEXIVE)) {
      await Promise.race([woken(), sleep(100)]);
    }

    this.status('Looking for the host…');
    const started = nowMs();
    let note = '';
    let hostSeen = false;
    let answered = false;
    let answeredMs = 0;
    let hostProtocol = 0;
    let remoteSet = false;
    const added = new Set();
    let meetProblem = '';
    let meetFailures = 0;

    while (!opened && !this.closed) {
      const elapsed = nowMs() - started;
      if (failed || (answered && nowMs() - answeredMs > 25000)) {
        throw new JoinError('The host is sharing, but no connection could be made through your two routers. '
          + 'That happens when a router gives every connection a new port (mobile networks often do), or blocks this kind of traffic. '
          + 'Another network on your side may work, or the app instead of the browser if the host has a forwarded port.', 'routers');
      }
      if (!answered && elapsed > 20000) {
        if (meetFailures > 3 && !hostSeen) {
          throw new JoinError(`The meeting point could not be reached (${meetProblem}). Check your internet connection.`, 'meet');
        }
        if (hostSeen) {
          throw new JoinError('The host is sharing, but its app did not answer a web browser. '
            + 'It needs a version that lets friends join from a browser, with that switched on under Play Together.', 'no-web');
        }
        throw new JoinError('No answer from the host, and it is not at the meeting point either. Check the link, and that the host is sharing right now.', 'no-host');
      }

      if (noteStale) {
        noteStale = false;
        note = await meet.sealOffer(noteKey, { peerId, fingerprint: mine.fingerprint, mid: mine.mid, side: mine.side });
      }
      let reply = null;
      try {
        reply = await meet.visit(meetUrl, room, peerId, note, this.abort.signal);
        meetFailures = 0;
      } catch (e) {
        if (this.closed) break;
        meetFailures++;
        meetProblem = e && e.message ? e.message : 'no answer';
      }
      if (reply) {
        if (reply.notes.length) hostSeen = true;
        for (const text of reply.notes) {
          const answers = await meet.openAnswers(noteKey, text);
          if (!answers) continue;
          hostProtocol = answers.protocol;
          const answer = answers.answers.find((a) => a.peerId === myId);
          if (!answer) continue;
          if (!remoteSet) {
            remoteSet = true;
            answered = true;
            answeredMs = nowMs();
            this.status('Host found, connecting…');
            await pc.setRemoteDescription({ type: 'answer', sdp: meet.answerSdp(mine.mid, answers.fingerprint, answer) });
          }
          for (let i = 0; i < answer.side.candidates.length; i++) {
            const line = meet.candidateLine(answer.side.candidates[i], i);
            const key = line.split(' ').slice(4, 8).join(' ');
            if (added.has(key)) continue;
            added.add(key);
            try {
              await pc.addIceCandidate({ candidate: line, sdpMid: mine.mid, sdpMLineIndex: 0 });
            } catch (e) {
              console.warn('candidate not taken:', line, e);
            }
          }
        }
      }
      if (hostProtocol && hostProtocol !== P.PROTOCOL_VERSION) {
        throw new JoinError(versionMessage(hostProtocol), 'version');
      }
      if (!opened) await Promise.race([woken(), sleep(meetFailures ? 1500 : 700)]);
    }
    if (this.closed) throw new JoinError('Cancelled', 'cancelled');

    // The note has done its work
    meet.visit(meetUrl, room, peerId, '', this.abort.signal).catch(() => {});

    this.status('Connected, saying hello…');
    await this.handshake();
  }

  async handshake() {
    const dc = this.dc;
    const nonce = crypto.getRandomValues(new Uint8Array(16));
    const hello = P.helloFrame({
      nonce,
      want: this.want,
      controller: this.controller,
      name: this.options.name,
      avatar: this.options.avatar ? this.options.avatar.ref : null,
      media: this.media,
      battery: this.battery,
      playerId: this.options.playerId,
    });
    let resolve;
    let reject;
    const done = new Promise((res, rej) => {
      resolve = res;
      reject = rej;
    });
    dc.onmessage = (e) => {
      const data = new Uint8Array(e.data);
      if (!data.length) return;
      if (data[0] === P.PT.Welcome) {
        const w = P.readWelcome(data.subarray(1));
        if (w && !this.welcome) {
          this.welcome = w;
          resolve();
        }
      } else if (data[0] === P.PT.Waiting) {
        // A host that lets each newcomer in by hand: it may take a while
        if (!waitingSince) {
          waitingSince = nowMs();
          this.status('Waiting for the host to let you in…');
        }
      } else if (data[0] === P.PT.Reject) {
        const reason = P.readReject(data.subarray(1));
        const why = {
          [P.REJECT_FULL]: ['The session is full.', 'full'],
          [P.REJECT_VERSION]: [versionMessage(0), 'version'],
          [P.REJECT_REFUSED]: ['The host did not let you in.', 'refused'],
          [P.REJECT_LOCKED]: ['The host has closed the session to newcomers.', 'locked'],
        }[reason] || ['The host declined the connection.', 'failed'];
        reject(new JoinError(why[0], why[1]));
      } else if (this.welcome) this.onMessage(data); // nothing may get lost between the two
    };
    const started = nowMs();
    let waitingSince = 0;
    const repeat = setInterval(() => {
      if (this.welcome || this.closed) return;
      if (waitingSince && nowMs() - waitingSince > ADMISSION_TIMEOUT_MS) {
        reject(new JoinError('The host did not answer your request to join.', 'failed'));
      } else if (!waitingSince && nowMs() - started > 8000) {
        reject(new JoinError('The host did not answer. Try again.', 'failed'));
      } else if (dc.readyState === 'open') dc.send(hello);
    }, HELLO_INTERVAL_MS);
    try {
      if (dc.readyState === 'open') dc.send(hello);
      await done;
    } finally {
      clearInterval(repeat);
    }
    if (this.closed) throw new JoinError('Cancelled', 'cancelled');

    this.connected = true;
    this.lastRxMs = nowMs();
    dc.onmessage = (e) => this.onMessage(new Uint8Array(e.data));
    // The host counts this guest in from its Hello on; from here it is kept informed
    this.timer = setInterval(() => this.tick(), 4);
    this.sendPing();
    if (this.h.onWelcome) this.h.onWelcome(this.welcome);
  }

  // ------------------------------------------------------------------ sending

  send(bytes) {
    const dc = this.dc;
    if (!dc || dc.readyState !== 'open') return false;
    try {
      dc.send(bytes);
      return true;
    } catch (e) {
      return false;
    }
  }

  sendPing() {
    this.pingId++;
    this.pingsSent++;
    this.lastPingMs = nowMs();
    this.send(P.pingFrame({
      id: this.pingId,
      sentUs: Math.round(performance.now() * 1000),
      want: this.want,
      rtt: Math.round(this.rttMs),
      controller: this.controller,
      media: this.media,
      battery: this.battery,
    }));
  }

  /** What the host should send: WANT_* flags. */
  setWant(want) {
    if (want === this.want) return;
    const videoOn = (want & P.WANT_VIDEO) && !(this.want & P.WANT_VIDEO);
    this.want = want;
    if (videoOn) this.assembler.reset();
    this.pingNow = true;
  }

  setController(kind, battery = P.BATTERY_UNKNOWN) {
    if (kind === this.controller && battery === this.battery) return;
    this.controller = kind;
    this.battery = battery;
    this.pingNow = true;
  }

  /** What this player contributes to the conversation: MEDIA_* flags. */
  setMedia(media) {
    if (media === this.media) return;
    this.media = media;
    this.pingNow = true;
  }

  /** The state of this player's controller; sent at once when it changed. */
  setPad(pad) {
    if (!this.connected) return;
    if (P.padEquals(pad, this.pad) && this.inputSeq !== 0) return;
    this.pad = P.copyPad(pad);
    this.inputBurst = INPUT_BURST - 1;
    this.sendInput();
  }

  sendInput() {
    this.inputSeq = (this.inputSeq + 1) >>> 0;
    this.lastInputMs = nowMs();
    this.send(P.inputFrame(this.inputSeq, this.pad));
  }

  /** Asks the host to wake its sleeping console; it acts on the first that arrives. */
  requestWake() {
    if (!this.connected) return;
    for (let i = 0; i < 3; i++) this.send(P.wakeFrame());
  }

  /** Mouse and keyboard for a game on the host's computer; sent at once, and again a moment later. */
  setKeyMouse(km) {
    if (!this.connected) return;
    this.keyMouse = km;
    this.keyMouseBurst = INPUT_BURST - 1;
    this.sendKeyMouse();
  }

  sendKeyMouse() {
    this.keyMouseSeq = ((this.keyMouseSeq || 0) + 1) >>> 0;
    this.lastKeyMouseMs = nowMs();
    this.send(P.keyMouseFrame(this.keyMouseSeq, this.keyMouse));
  }

  /** One picture of the own webcam, as JPEG. */
  sendCamFrame(jpeg) {
    if (!this.connected) return;
    this.camSeq = (this.camSeq + 1) >>> 0;
    for (const packet of P.camUpFrames(this.camSeq, jpeg)) this.send(packet);
  }

  sendVoice(opus) {
    if (!this.connected || !opus.length || opus.length > P.MAX_VOICE_FRAME) return;
    this.voiceSeq = (this.voiceSeq + 1) >>> 0;
    this.send(P.voiceUpFrame(this.voiceSeq, opus));
  }

  /**
   * @param {string} text
   * @param {number} [reply] the message it answers (the host's numbering), 0 for none
   * @returns {number} the message's own number, 0 if it could not be sent
   */
  sendChat(text, reply = 0) {
    const clean = text.trim();
    if (!this.connected || !clean) return 0;
    const id = ++this.chatUpSeq;
    const payload = P.chatUpFrame(id, clean, reply);
    this.chatOut.set(id, { payload, firstMs: nowMs(), lastMs: nowMs() });
    this.send(payload);
    return id;
  }

  /** The chat is read up to this message (the host's numbering); told again until the host's marks show it. */
  setChatRead(seq) {
    if (!this.connected || this.closed || !seq || seq <= this.readWanted) return;
    this.readWanted = seq;
    this.readSentMs = this.readFirstMs = nowMs();
    this.send(P.chatSeenFrame(seq));
  }

  /** This player's reaction to a message, given or taken back; sent again until the host shows it. */
  react(seq, emoji, on) {
    if (!this.connected || this.closed || !seq || !P.validReaction(emoji)) return;
    const now = nowMs();
    // The newest wish for that reaction is the one that counts
    this.reactionsOut = this.reactionsOut.filter((o) => !(o.seq === seq && o.emoji === emoji));
    const payload = P.chatReactUpFrame(seq, emoji, on);
    this.reactionsOut.push({ seq, emoji, on, payload, firstMs: now, lastMs: now });
    this.send(payload);
  }

  /** Deletes an own message for everybody; asked again until the host's mark shows it (or given up). */
  deleteChat(seq) {
    if (!this.connected || this.closed || !seq || this.chatMarks.deleted.has(seq)) return;
    const now = nowMs();
    this.deletesOut = this.deletesOut.filter((o) => o.seq !== seq);
    this.deletesOut.push({ seq, firstMs: now, lastMs: now });
    this.send(P.chatDeleteUpFrame(seq));
  }

  /** Whether this player has given that reaction to a message, as the host last said. */
  reacted(seq, emoji) {
    const kinds = this.chatMarks.reactions.get(seq);
    const slots = kinds ? kinds.get(emoji) : null;
    return !!slots && !!this.welcome && slots.includes(this.welcome.slot);
  }

  /** Reactions and the read mark again, until the host's marks show them. */
  resendMarks(now) {
    // Deleting, until the host's mark shows it (or given up like a message)
    this.deletesOut = this.deletesOut.filter((o) => now - o.firstMs <= CHAT_GIVE_UP_MS);
    for (const o of this.deletesOut) {
      if (now - o.lastMs >= CHAT_RESEND_MS) {
        o.lastMs = now;
        this.send(P.chatDeleteUpFrame(o.seq));
      }
    }
    this.reactionsOut = this.reactionsOut.filter((o) => now - o.firstMs <= CHAT_GIVE_UP_MS);
    for (const o of this.reactionsOut) {
      if (now - o.lastMs >= CHAT_RESEND_MS) {
        o.lastMs = now;
        this.send(o.payload);
      }
    }
    // A host without read marks never shows it: given up like a message, told again with the next one read
    const own = this.welcome ? this.chatMarks.read.get(this.welcome.slot) || 0 : 0;
    if (this.readWanted && own < this.readWanted && now - this.readFirstMs <= CHAT_GIVE_UP_MS &&
      now - this.readSentMs >= CHAT_RESEND_MS * 3) {
      this.readSentMs = now;
      this.send(P.chatSeenFrame(this.readWanted));
    }
  }

  requestKeyframe() {
    this.send(new Uint8Array([P.PT.KeyframeRequest]));
  }

  // ------------------------------------------------------------------ receiving

  onMessage(data) {
    if (data.length < 1 || this.closed) return;
    const now = nowMs();
    this.lastRxMs = now;
    this.stats.packets++;
    this.stats.bytes += data.length;
    const payload = data.subarray(1);
    switch (data[0]) {
      case P.PT.Pong: {
        const pong = P.readPong(payload);
        if (!pong) break;
        const sample = Math.max(0, performance.now() - pong.sentUs / 1000);
        this.rttMs = this.rttMs ? (this.rttMs * 7 + sample) / 8 : sample;
        this.assembler.rttMs = this.rttMs;
        break;
      }
      case P.PT.State: {
        const s = P.readState(payload);
        if (!s) break;
        if (this.haveState && ((s.seq - this.state.seq) | 0) <= 0) break;
        this.haveState = true;
        this.state = s;
        for (const p of s.participants) this.wantBlob(p.avatar);
        if (this.h.onState) this.h.onState(s);
        break;
      }
      case P.PT.VideoConfig: {
        if (payload.length < 3) break;
        const id = (payload[0] << 8) | payload[1];
        if (!id || (this.haveVideoConfig && id === this.videoConfigId)) break;
        this.haveVideoConfig = true;
        this.videoConfigId = id;
        // Frames encoded against the old parameter sets are of no use any more
        this.assembler.reset();
        if (this.h.onVideoConfig) this.h.onVideoConfig(payload.subarray(2));
        break;
      }
      case P.PT.Video: {
        if (!(this.want & P.WANT_VIDEO)) break;
        const h = P.readVideoFragment(payload);
        if (!h || !this.assembler.onFragment(h, now)) break;
        for (let frame = this.assembler.popReady(); frame; frame = this.assembler.popReady()) {
          if (!this.haveVideoConfig) {
            // Cannot decode without parameter sets; they come with the next key frame
            this.assembler.reset();
            break;
          }
          if (this.h.onVideoFrame) this.h.onVideoFrame(frame);
        }
        break;
      }
      case P.PT.Audio: {
        if (payload.length < 5) break;
        const seq = ((payload[0] << 24) | (payload[1] << 16) | (payload[2] << 8) | payload[3]) >>> 0;
        if (this.haveAudio && ((seq - this.lastAudioSeq) | 0) <= 0) break; // late or twice
        this.haveAudio = true;
        this.lastAudioSeq = seq;
        this.stats.audioFrames++;
        if (this.h.onAudio && (this.want & P.WANT_AUDIO)) this.h.onAudio(payload.subarray(4));
        break;
      }
      case P.PT.Haptics: {
        if (payload.length < 5 || !(this.want & P.WANT_HAPTICS)) break;
        const seq = ((payload[0] << 24) | (payload[1] << 16) | (payload[2] << 8) | payload[3]) >>> 0;
        if (this.haveHaptics && ((seq - this.lastHapticsSeq) | 0) <= 0) break;
        this.haveHaptics = true;
        this.lastHapticsSeq = seq;
        if (this.h.onHaptics) this.h.onHaptics(payload.subarray(4));
        break;
      }
      case P.PT.Inputs: {
        const inputs = P.readInputs(payload);
        if (!inputs || (this.haveInputs && ((inputs.seq - this.lastInputsSeq) | 0) <= 0)) break;
        this.haveInputs = true;
        this.lastInputsSeq = inputs.seq;
        if (this.h.onInputs) this.h.onInputs(inputs);
        break;
      }
      case P.PT.Voice: {
        const v = P.readVoice(payload);
        if (!v || !(this.want & P.WANT_VOICE)) break;
        let seq = this.voiceIn.get(v.slot);
        if (!seq) this.voiceIn.set(v.slot, (seq = new LooseSequence()));
        if (seq.accept(v.seq, now) && this.h.onVoice) this.h.onVoice(v.slot, v.data);
        break;
      }
      case P.PT.Cam: {
        const c = P.readCam(payload);
        if (c && (this.want & P.WANT_CAMS) && this.h.onCamFragment) this.h.onCamFragment(c, now);
        break;
      }
      case P.PT.Chat: {
        const m = P.readChat(payload);
        if (!m) break;
        // Always acknowledged: the earlier answer may have been lost
        this.send(P.u32Frame(P.PT.ChatAck, m.seq));
        if (this.chatSeen.includes(m.seq)) break;
        this.chatSeen.push(m.seq);
        if (this.chatSeen.length > 256) this.chatSeen.shift();
        if (m.embed && m.embed.image.valid) this.wantBlob(m.embed.image);
        if (this.h.onChat) this.h.onChat(m);
        break;
      }
      case P.PT.ChatMark: {
        const m = P.readChatMark(payload);
        if (!m) break;
        // Always acknowledged: the earlier answer may have been lost
        this.send(P.chatMarkAckFrame(m.mark));
        // A mark older than one applied for the same thing changes nothing
        const key = P.chatMarkKey(m);
        let changed = false;
        if (m.mark > (this.marksApplied.get(key) || 0)) {
          this.marksApplied.set(key, m.mark);
          changed = P.applyChatMark(this.chatMarks, m);
        }
        // This player's own reaction, as the host has it now as wanted: no need to send it again.
        // A mark the other way is an older wish (taking it back got lost) or a refusal over the
        // limits: sent again, until it shows or is given up
        if (m.kind === P.MARK_REACTION && this.welcome && m.slot === this.welcome.slot) {
          this.reactionsOut = this.reactionsOut.filter((o) => !(o.seq === m.seq && o.emoji === m.emoji && o.on === m.on));
        }
        // Deleted: no need to ask again
        if (m.kind === P.MARK_DELETED) this.deletesOut = this.deletesOut.filter((o) => o.seq !== m.seq);
        if (changed && this.h.onChatMarks) this.h.onChatMarks(this.chatMarks);
        break;
      }
      case P.PT.ChatUpAck: {
        if (payload.length >= 4) {
          const id = ((payload[0] << 24) | (payload[1] << 16) | (payload[2] << 8) | payload[3]) >>> 0;
          this.chatOut.delete(id);
        }
        break;
      }
      case P.PT.BlobRequest: {
        // The host asks for this player's picture
        const request = P.readBlobRequest(payload);
        const avatar = this.options.avatar;
        if (!request || !avatar || P.hashKey(request.hash) !== P.hashKey(avatar.ref.hash)) break;
        const chunk = P.blobChunkFrame(avatar.ref.hash, avatar.data, request.index);
        if (chunk) this.send(chunk);
        break;
      }
      case P.PT.BlobChunk: {
        const chunk = P.readBlobChunk(payload);
        if (chunk) this.onBlobChunk(chunk);
        break;
      }
      case P.PT.Bye: {
        const reason = payload.length ? payload[0] : P.BYE_HOST_ENDED;
        this.lost(reason === P.BYE_KICKED ? 'kicked' : reason === P.BYE_RECONNECTING ? 'reconnecting' : 'ended');
        break;
      }
      default:
        break;
    }
  }

  // ------------------------------------------------------------------ pictures of the players

  wantBlob(ref) {
    if (!ref || !ref.valid || ref.size > P.MAX_AVATAR_SIZE) return;
    const key = P.hashKey(ref.hash);
    if (this.blobsDone.has(key) || this.blobs.has(key)) return;
    if (this.blobs.size >= 32) return;
    const count = Math.ceil(ref.size / P.BLOB_CHUNK_SIZE);
    this.blobs.set(key, { ref, data: new Uint8Array(ref.size), got: new Uint8Array(count), gotCount: 0, asked: new Float64Array(count) });
  }

  /** The bytes of a player's picture, or null while it is not here. */
  blob(ref) {
    return ref && ref.valid ? this.blobsDone.get(P.hashKey(ref.hash)) || null : null;
  }

  onBlobChunk(chunk) {
    const key = P.hashKey(chunk.hash);
    const d = this.blobs.get(key);
    if (!d || chunk.totalSize !== d.ref.size || chunk.index >= d.got.length || d.got[chunk.index]) return;
    const offset = chunk.index * P.BLOB_CHUNK_SIZE;
    const expected = Math.min(P.BLOB_CHUNK_SIZE, d.ref.size - offset);
    if (chunk.data.length !== expected) return;
    d.data.set(chunk.data, offset);
    d.got[chunk.index] = 1;
    d.gotCount++;
    if (d.gotCount !== d.got.length) return;
    this.blobs.delete(key);
    // Only what matches the name it was asked for under is kept
    crypto.subtle.digest('SHA-256', d.data).then((digest) => {
      if (P.hashKey(new Uint8Array(digest).subarray(0, 8)) !== key) return;
      this.blobsDone.set(key, d.data);
      if (this.h.onBlob) this.h.onBlob(d.ref, d.data);
    });
  }

  pollBlobs(now) {
    let budget = 8;
    for (const d of this.blobs.values()) {
      for (let i = 0; i < d.got.length && budget > 0; i++) {
        if (d.got[i] || (d.asked[i] && now - d.asked[i] < 200)) continue;
        d.asked[i] = now;
        budget--;
        this.send(P.blobRequestFrame(d.ref.hash, i));
      }
    }
  }

  // ------------------------------------------------------------------ keeping it going

  tick() {
    if (!this.connected || this.closed) return;
    const now = nowMs();
    if (now - this.lastRxMs > HOST_TIMEOUT_MS) {
      this.lost('timeout');
      return;
    }

    const pingInterval = this.pingsSent < FAST_PINGS ? FAST_PING_INTERVAL_MS : PING_INTERVAL_MS;
    if (this.pingNow || now - this.lastPingMs >= pingInterval) {
      this.pingNow = false;
      this.sendPing();
    }

    if (this.inputBurst > 0) {
      if (now - this.lastInputMs >= INPUT_BURST_SPACING_MS) {
        this.inputBurst--;
        this.sendInput();
      }
    } else if (now - this.lastInputMs >= INPUT_REFRESH_MS) this.sendInput();
    if (this.keyMouse) {
      if (this.keyMouseBurst > 0) {
        if (now - this.lastKeyMouseMs >= INPUT_BURST_SPACING_MS) {
          this.keyMouseBurst--;
          this.sendKeyMouse();
        }
      } else if (now - this.lastKeyMouseMs >= INPUT_REFRESH_MS) this.sendKeyMouse();
    }

    for (const [id, m] of this.chatOut) {
      if (now - m.firstMs > CHAT_GIVE_UP_MS) {
        this.chatOut.delete(id);
        // Said so on the message, as the app does
        if (this.h.onChatFailed) this.h.onChatFailed(id);
      }
      else if (now - m.lastMs >= CHAT_RESEND_MS) {
        m.lastMs = now;
        this.send(m.payload);
      }
    }

    this.resendMarks(now);

    if (now - this.lastBlobPollMs >= 50) {
      this.lastBlobPollMs = now;
      this.pollBlobs(now);
    }

    if (this.want & P.WANT_VIDEO) {
      const { nacks, needKeyframe } = this.assembler.poll(now);
      for (const packet of P.nackFrames(nacks)) this.send(packet);
      if (needKeyframe) this.requestKeyframe();
    }
    if (this.h.onTick) this.h.onTick(now);
  }

  /** The connection ended without this side wanting it. */
  lost(reason) {
    if (this.closed) return;
    const wasConnected = this.connected;
    this.shutdown();
    if (wasConnected && this.h.onClosed) this.h.onClosed(reason);
  }

  /**
   * Leaves the session.
   * @param {boolean} [sleepConsole] also asks the host to put the console to sleep; only for a
   *   guest the session state allows it to (maySleep), any other host just sees a goodbye
   */
  close(sleepConsole = false) {
    if (this.closed) return;
    if (this.connected) {
      const bye = P.byeFrame(sleepConsole ? P.BYE_SLEEP : P.BYE_QUIT);
      this.send(bye);
      this.send(bye);
    }
    // A moment for the goodbye to get out
    const pc = this.pc;
    const dc = this.dc;
    this.pc = null;
    this.dc = null;
    this.shutdown();
    setTimeout(() => {
      try {
        if (dc) dc.close();
        if (pc) pc.close();
      } catch (e) {
        // gone already
      }
    }, 150);
  }

  shutdown() {
    this.closed = true;
    this.connected = false;
    clearInterval(this.timer);
    this.abort.abort();
    try {
      if (this.dc) this.dc.close();
      if (this.pc) this.pc.close();
    } catch (e) {
      // gone already
    }
  }
}

function versionMessage(hostProtocol) {
  if (hostProtocol && hostProtocol < P.PROTOCOL_VERSION) {
    return 'The host runs an older version of the app than this page works with. The host needs to update.';
  }
  return 'The host runs a different version of the app than this page works with. Reload the page; if that does not help, the host needs to update.';
}
