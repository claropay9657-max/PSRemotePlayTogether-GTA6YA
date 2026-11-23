// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Sound: the game's audio and the other players' voices are Opus, decoded by
// the browser (WebCodecs) and played through one mixer on the audio thread.
// The own microphone goes the other way.

const RATE = 48000;
const VOICE_FRAME = 960; // 20 ms
/** A voice frame quieter than this is not sent ... */
export const GATE_RMS = 0.006;
/** ... unless somebody was heard this short a time ago. */
const HANGOVER_MS = 450;

export class Sound {
  constructor() {
    this.context = null;
    this.mixer = null;
    this.gameDecoder = null;
    this.gameChannels = 2;
    this.gameTime = 0;
    this.gameVolume = 1;
    this.voiceVolume = 1;
    /** @type {Map<number, object>} by player */
    this.voices = new Map();
    this.muted = new Set();
    this.deafened = false;
    this.micStream = null;
    this.micNode = null;
    this.micSource = null;
    this.encoder = null;
    this.micMuted = false;
    this.micTime = 0;
    this.lastLoudMs = 0;
    this.onVoiceFrame = null;
    this.onTalking = null;
    this.talking = new Map(); // player -> when last heard
    this.selfTalkingMs = 0;
    this.problem = '';
  }

  static supported() {
    return typeof AudioDecoder === 'function' && typeof AudioWorkletNode === 'function';
  }

  static canSpeak() {
    return typeof AudioEncoder === 'function' && typeof AudioData === 'function'
      && !!(navigator.mediaDevices && navigator.mediaDevices.getUserMedia);
  }

  /** Has to be called from a click or key press: browsers only start sound then. */
  async start() {
    if (this.context) {
      await this.wake();
      return true;
    }
    if (!Sound.supported()) {
      this.problem = 'This browser cannot play the sound (it lacks WebCodecs audio). A current Chrome, Edge, Firefox or Safari can.';
      return false;
    }
    try {
      this.context = new AudioContext({ sampleRate: RATE, latencyHint: 'interactive' });
      await this.context.audioWorklet.addModule(new URL('./audio-worklet.js', import.meta.url));
      this.mixer = new AudioWorkletNode(this.context, 'psrpt-mixer', { numberOfInputs: 0, outputChannelCount: [2] });
      this.mixer.connect(this.context.destination);
      await this.wake();
      return true;
    } catch (e) {
      this.problem = 'The sound could not be started: ' + (e && e.message ? e.message : e);
      this.context = null;
      return false;
    }
  }

  /**
   * Asks the browser to let the sound play. It only does so after a click or a key
   * press, and some browsers leave the question open until then: this never waits
   * for longer than a moment, and is simply asked again at the next click.
   */
  async wake() {
    if (!this.context || this.context.state !== 'suspended') return;
    const resumed = this.context.resume().catch(() => {});
    await Promise.race([resumed, new Promise((resolve) => setTimeout(resolve, 600))]);
  }

  post(id, audioData, start, max, gain) {
    const channels = audioData.numberOfChannels;
    const planes = [];
    for (let c = 0; c < channels; c++) {
      const plane = new Float32Array(audioData.numberOfFrames);
      audioData.copyTo(plane, { planeIndex: c, format: 'f32-planar' });
      planes.push(plane);
    }
    audioData.close();
    this.mixer.port.postMessage({ type: 'pcm', id, planes, start, max, gain }, planes.map((p) => p.buffer));
  }

  // ------------------------------------------------------------------ the game

  /** What the console's sound looks like: {channels, rate}. */
  setGameFormat(header) {
    const channels = header && header.valid && header.channels ? header.channels : 2;
    if (this.gameDecoder && channels === this.gameChannels) return;
    this.gameChannels = channels;
    this.closeDecoder(this.gameDecoder);
    this.gameDecoder = null;
  }

  /** One Opus frame of the game's sound. */
  pushGame(opus) {
    if (!this.mixer) return;
    if (!this.gameDecoder || this.gameDecoder.state !== 'configured') {
      this.closeDecoder(this.gameDecoder);
      this.gameDecoder = new AudioDecoder({
        // 40 ms before it starts, never more than 150 ms behind
        output: (data) => this.post('game', data, 1920, 7200, this.gameVolume),
        error: (e) => console.warn('game sound decoder:', e),
      });
      this.gameDecoder.configure({ codec: 'opus', sampleRate: RATE, numberOfChannels: this.gameChannels });
    }
    this.gameTime += 10000;
    try {
      this.gameDecoder.decode(new EncodedAudioChunk({ type: 'key', timestamp: this.gameTime, data: opus }));
    } catch (e) {
      console.warn('game sound:', e);
    }
  }

  setGameVolume(volume) {
    this.gameVolume = volume;
    if (this.mixer) this.mixer.port.postMessage({ type: 'gain', id: 'game', gain: volume });
  }

  // ------------------------------------------------------------------ the others' voices

  voiceGain(slot) {
    return this.deafened || this.muted.has(slot) ? 0 : this.voiceVolume;
  }

  /** One Opus frame of a player's microphone. */
  pushVoice(slot, opus) {
    if (!this.mixer) return;
    let v = this.voices.get(slot);
    if (!v || v.decoder.state !== 'configured') {
      if (v) this.closeDecoder(v.decoder);
      const id = 'voice' + slot;
      v = { time: 0 };
      v.decoder = new AudioDecoder({
        // 60 ms before it starts, never more than 300 ms behind
        output: (data) => this.post(id, data, 2880, 14400, this.voiceGain(slot)),
        error: (e) => console.warn('voice decoder:', e),
      });
      v.decoder.configure({ codec: 'opus', sampleRate: RATE, numberOfChannels: 1 });
      this.voices.set(slot, v);
    }
    v.time += 20000;
    try {
      v.decoder.decode(new EncodedAudioChunk({ type: 'key', timestamp: v.time, data: opus }));
    } catch (e) {
      console.warn('voice:', e);
    }
    this.talking.set(slot, performance.now());
  }

  /** Who has been heard in the last moment, by player; the own place with selfSlot. */
  talkingNow(selfSlot) {
    const now = performance.now();
    const out = new Set();
    for (const [slot, at] of this.talking) if (now - at < 350) out.add(slot);
    if (selfSlot !== undefined && now - this.selfTalkingMs < 350) out.add(selfSlot);
    return out;
  }

  applyVoiceGains() {
    if (!this.mixer) return;
    for (const slot of this.voices.keys()) {
      this.mixer.port.postMessage({ type: 'gain', id: 'voice' + slot, gain: this.voiceGain(slot) });
    }
  }

  setVoiceVolume(volume) {
    this.voiceVolume = volume;
    this.applyVoiceGains();
  }

  setDeafened(on) {
    this.deafened = on;
    this.applyVoiceGains();
  }

  setPlayerMuted(slot, on) {
    if (on) this.muted.add(slot);
    else this.muted.delete(slot);
    this.applyVoiceGains();
  }

  /** A player has left: its place may go to somebody else. */
  forgetVoice(slot) {
    const v = this.voices.get(slot);
    if (v) this.closeDecoder(v.decoder);
    this.voices.delete(slot);
    this.talking.delete(slot);
    this.muted.delete(slot);
    if (this.mixer) this.mixer.port.postMessage({ type: 'remove', id: 'voice' + slot });
  }

  // ------------------------------------------------------------------ the own microphone

  get micOn() {
    return !!this.micStream;
  }

  /**
   * Opens the microphone (the browser asks for permission).
   * @returns {Promise<string>} empty if it worked, otherwise why not
   */
  async startMic() {
    if (this.micStream) return '';
    if (!this.context) return 'The sound has not been started.';
    if (!Sound.canSpeak()) return 'This browser cannot send a microphone (it lacks WebCodecs audio encoding).';
    try {
      this.micStream = await navigator.mediaDevices.getUserMedia({
        audio: { echoCancellation: true, noiseSuppression: true, autoGainControl: true, channelCount: 1 },
        video: false,
      });
    } catch (e) {
      this.micStream = null;
      return e && e.name === 'NotAllowedError' ? 'The browser was not allowed to use the microphone.'
        : 'No microphone could be opened: ' + (e && e.message ? e.message : e);
    }
    try {
      this.encoder = new AudioEncoder({
        output: (chunk) => {
          const data = new Uint8Array(chunk.byteLength);
          chunk.copyTo(data);
          if (this.onVoiceFrame) this.onVoiceFrame(data);
        },
        error: (e) => console.warn('voice encoder:', e),
      });
      this.encoder.configure({
        codec: 'opus',
        sampleRate: RATE,
        numberOfChannels: 1,
        bitrate: 32000,
        opus: { frameDuration: 20000, application: 'voip', signal: 'voice', useinbandfec: true, packetlossperc: 5 },
      });
      this.micSource = this.context.createMediaStreamSource(this.micStream);
      this.micNode = new AudioWorkletNode(this.context, 'psrpt-microphone', { numberOfInputs: 1, numberOfOutputs: 0 });
      this.micNode.port.onmessage = (e) => this.onMicFrame(e.data);
      this.micSource.connect(this.micNode);
    } catch (e) {
      this.stopMic();
      return 'The microphone could not be started: ' + (e && e.message ? e.message : e);
    }
    return '';
  }

  onMicFrame(frame) {
    if (!this.encoder || this.encoder.state !== 'configured' || this.micMuted) return;
    let sum = 0;
    for (let i = 0; i < frame.length; i++) sum += frame[i] * frame[i];
    const rms = Math.sqrt(sum / frame.length);
    const now = performance.now();
    if (rms >= GATE_RMS) this.lastLoudMs = now;
    // Silence is not sent; the end of a word still is
    if (now - this.lastLoudMs > HANGOVER_MS) return;
    this.selfTalkingMs = now;
    this.micTime += 20000;
    const data = new AudioData({
      format: 'f32-planar',
      sampleRate: RATE,
      numberOfFrames: VOICE_FRAME,
      numberOfChannels: 1,
      timestamp: this.micTime,
      data: frame,
    });
    try {
      this.encoder.encode(data);
    } catch (e) {
      console.warn('voice encode:', e);
    }
    data.close();
  }

  setMicMuted(on) {
    this.micMuted = on;
  }

  stopMic() {
    if (this.micNode) {
      this.micNode.port.onmessage = null;
      this.micNode.disconnect();
    }
    if (this.micSource) this.micSource.disconnect();
    if (this.micStream) for (const track of this.micStream.getTracks()) track.stop();
    this.closeDecoder(this.encoder);
    this.micNode = null;
    this.micSource = null;
    this.micStream = null;
    this.encoder = null;
  }

  // ------------------------------------------------------------------

  closeDecoder(coder) {
    if (coder && coder.state !== 'closed') {
      try {
        coder.close();
      } catch (e) {
        // gone already
      }
    }
  }

  /** The session is over; the sound itself stays ready for the next one. */
  reset() {
    this.closeDecoder(this.gameDecoder);
    this.gameDecoder = null;
    for (const slot of Array.from(this.voices.keys())) this.forgetVoice(slot);
    if (this.mixer) this.mixer.port.postMessage({ type: 'remove', id: 'game' });
  }

  close() {
    this.reset();
    this.stopMic();
    if (this.context) this.context.close().catch(() => {});
    this.context = null;
    this.mixer = null;
  }
}
