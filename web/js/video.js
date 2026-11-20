// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The console's picture: fragments are put back together into frames
// (FrameAssembler, a port of coplay/src/video.cpp), frames are decoded by the
// browser's own decoder (WebCodecs) and drawn onto a canvas.

import { CODEC_H264, MAX_VIDEO_FRAME_SIZE, VIDEO_FRAGMENT_SIZE, VIDEO_KEY, VIDEO_RECOVERED } from './proto.js';

/**
 * Puts fragments back together and hands out frames strictly in order. A
 * decoder cannot use a frame whose predecessor is missing, so after a loss
 * that cannot be repaired nothing is handed out until the next key frame.
 */
export class FrameAssembler {
  constructor() {
    this.nackDelayMs = 5;
    this.nackIntervalMs = 40;
    this.maxWaitMs = 150;
    this.keyframeIntervalMs = 500;
    this.maxPending = 64;
    /** @type {Map<number, object>} by frame number */
    this.pending = new Map();
    this.ready = [];
    this.missingNackMs = new Map();
    this.waitingForKey = true;
    this.keyframeRequestDue = true;
    this.nextSeq = 0;
    this.lostSinceDelivery = 0;
    this.recoveredPending = false;
    this.deliveredAny = false;
    this.lastDeliveredCaptureMs = 0;
    this.lastKeyframeRequestMs = 0;
    this.rttMs = 0;
    this.stats = { fragments: 0, duplicates: 0, delivered: 0, dropped: 0, nackFragments: 0, keyframeRequests: 0 };
  }

  /** Drops everything and waits for a key frame. */
  reset() {
    this.pending.clear();
    this.ready.length = 0;
    this.missingNackMs.clear();
    this.waitingForKey = true;
    this.keyframeRequestDue = true;
    this.lostSinceDelivery = 0;
    this.recoveredPending = false;
  }

  get nackInterval() {
    return Math.max(this.nackIntervalMs, this.rttMs + this.rttMs / 2 + 10);
  }

  get maxWait() {
    return Math.max(this.maxWaitMs, this.rttMs * 3 + 60);
  }

  sortedSeqs() {
    return Array.from(this.pending.keys()).sort((a, b) => a - b);
  }

  /** @returns {boolean} false if the fragment is malformed */
  onFragment(h, nowMs) {
    if (h.totalSize === 0 || h.totalSize > MAX_VIDEO_FRAME_SIZE) return false;
    if (h.fragCount !== Math.ceil(h.totalSize / VIDEO_FRAGMENT_SIZE) || h.fragIndex >= h.fragCount) return false;
    const expected = h.fragIndex + 1 < h.fragCount ? VIDEO_FRAGMENT_SIZE
      : h.totalSize - (h.fragCount - 1) * VIDEO_FRAGMENT_SIZE;
    if (h.data.length !== expected) return false;
    this.stats.fragments++;

    if (!this.waitingForKey && h.frameSeq < this.nextSeq) {
      this.stats.duplicates++; // already delivered or given up on
      return true;
    }

    let p = this.pending.get(h.frameSeq);
    if (!p) {
      if (this.pending.size >= this.maxPending) {
        const seqs = this.sortedSeqs();
        if (h.frameSeq < seqs[0]) return true; // older than everything we keep
        // Hopelessly behind: frames pile up faster than they complete
        if (!this.waitingForKey) this.startWaitingForKey();
        if (this.pending.size >= this.maxPending) {
          this.stats.dropped++;
          this.pending.delete(this.sortedSeqs()[0]);
        }
      }
      p = {
        flags: h.flags & (VIDEO_KEY | VIDEO_RECOVERED),
        framesLost: h.framesLost,
        totalSize: h.totalSize,
        fragCount: h.fragCount,
        captureMs: h.captureMs,
        data: new Uint8Array(h.totalSize),
        received: new Uint8Array(h.fragCount),
        receivedCount: 0,
        firstMs: nowMs,
        lastRxMs: nowMs,
        lastNackMs: 0,
      };
      this.pending.set(h.frameSeq, p);
      this.missingNackMs.delete(h.frameSeq);
    } else if (p.totalSize !== h.totalSize || p.fragCount !== h.fragCount) return false;

    if (p.received[h.fragIndex]) {
      this.stats.duplicates++;
      return true;
    }
    p.data.set(h.data, h.fragIndex * VIDEO_FRAGMENT_SIZE);
    p.received[h.fragIndex] = 1;
    p.receivedCount++;
    p.lastRxMs = nowMs;
    if (p.receivedCount === p.fragCount) this.deliver();
    return true;
  }

  startWaitingForKey() {
    this.waitingForKey = true;
    this.keyframeRequestDue = true;
    this.recoveredPending = true;
    this.missingNackMs.clear();
    // Only a key frame and what follows it is still useful
    for (const seq of this.sortedSeqs()) {
      if (this.pending.get(seq).flags & VIDEO_KEY) break;
      this.pending.delete(seq);
      this.stats.dropped++;
      this.lostSinceDelivery++;
    }
  }

  /** The newest complete key frame among the given frames, or -1. */
  newestCompleteKey(seqs) {
    let key = -1;
    for (const seq of seqs) {
      const p = this.pending.get(seq);
      if (p.receivedCount === p.fragCount && (p.flags & VIDEO_KEY)) key = seq;
    }
    return key;
  }

  dropBefore(seq) {
    for (const s of this.sortedSeqs()) {
      if (s >= seq) break;
      this.pending.delete(s);
    }
  }

  deliver() {
    if (this.waitingForKey) {
      const seqs = this.sortedSeqs();
      const key = this.newestCompleteKey(seqs);
      if (key < 0) return;
      const dropped = seqs.filter((s) => s < key).length;
      this.stats.dropped += dropped;
      this.lostSinceDelivery += dropped;
      this.dropBefore(key);
      this.nextSeq = key;
      this.waitingForKey = false;
      this.keyframeRequestDue = false;
      this.missingNackMs.clear();
    }

    for (;;) {
      const head = this.pending.get(this.nextSeq);
      if (head && head.receivedCount === head.fragCount) {
        this.ready.push({
          seq: this.nextSeq,
          key: (head.flags & VIDEO_KEY) !== 0,
          recovered: (head.flags & VIDEO_RECOVERED) !== 0 || (this.recoveredPending && this.deliveredAny),
          framesLost: head.framesLost + this.lostSinceDelivery,
          captureMs: head.captureMs,
          data: head.data,
        });
        this.lastDeliveredCaptureMs = head.captureMs;
        this.pending.delete(this.nextSeq);
        this.missingNackMs.delete(this.nextSeq);
        this.nextSeq++;
        this.lostSinceDelivery = 0;
        this.recoveredPending = false;
        this.deliveredAny = true;
        this.stats.delivered++;
        continue;
      }
      // The head is not ready. A complete key frame further ahead makes everything before it unnecessary.
      const key = this.newestCompleteKey(this.sortedSeqs().filter((s) => s > this.nextSeq));
      if (key < 0) break;
      const skipped = key - this.nextSeq;
      this.stats.dropped += skipped;
      this.lostSinceDelivery += skipped;
      this.recoveredPending = true;
      this.dropBefore(key);
      this.nextSeq = key;
      this.missingNackMs.clear();
    }
  }

  /** The next frame for the decoder, or null. */
  popReady() {
    return this.ready.length ? this.ready.shift() : null;
  }

  /** How long nothing may arrive of a frame before its gaps count as lost. */
  get nackDelay() {
    // The sender spreads a large frame over the time its packets take to be confirmed
    return Math.max(this.nackDelayMs, this.rttMs / 2 + 8);
  }

  collectNack(seq, p, nowMs, nacks) {
    // Fragments are still coming in: give them a moment before calling the gaps lost
    if (nowMs - p.lastRxMs < this.nackDelay) return;
    if (p.lastNackMs && nowMs - p.lastNackMs < this.nackInterval) return;
    const missing = p.fragCount - p.receivedCount;
    const fragments = [];
    if (missing <= 512) {
      for (let i = 0; i < p.fragCount; i++) if (!p.received[i]) fragments.push(i);
    }
    // else: an empty list = send the whole frame again
    this.stats.nackFragments += missing;
    p.lastNackMs = nowMs;
    nacks.push({ frameSeq: seq, fragments });
  }

  /**
   * Call regularly.
   * @returns {{nacks: object[], needKeyframe: boolean}} what to ask the host for
   */
  poll(nowMs) {
    const nacks = [];
    let needKeyframe = false;
    let seqs = this.sortedSeqs();

    if (!this.waitingForKey && seqs.length) {
      // The head frame blocks delivery. Is it still worth waiting for?
      const head = this.pending.get(this.nextSeq);
      const first = this.pending.get(seqs[0]);
      const newest = this.pending.get(seqs[seqs.length - 1]);
      const blockedSince = head ? head.firstMs : first.firstMs;
      const headCapture = head ? head.captureMs : (this.deliveredAny ? this.lastDeliveredCaptureMs : first.captureMs);
      const behindMs = (newest.captureMs - headCapture) | 0;
      if (nowMs - blockedSince >= this.maxWait || behindMs > this.maxWait || seqs[seqs.length - 1] - this.nextSeq > 1024) {
        this.startWaitingForKey();
        seqs = this.sortedSeqs();
      }
    }

    if (this.waitingForKey) {
      if (this.keyframeRequestDue || nowMs - this.lastKeyframeRequestMs >= this.keyframeIntervalMs) {
        needKeyframe = true;
        this.keyframeRequestDue = false;
        this.lastKeyframeRequestMs = nowMs;
        this.stats.keyframeRequests++;
      }
      // A key frame that is partly here is the fastest way back to a picture
      for (const seq of seqs) {
        const p = this.pending.get(seq);
        if ((p.flags & VIDEO_KEY) && p.receivedCount !== p.fragCount) this.collectNack(seq, p, nowMs, nacks);
      }
      return { nacks, needKeyframe };
    }
    if (!seqs.length) return { nacks, needKeyframe };

    const highest = seqs[seqs.length - 1];
    let later = 0; // index into seqs of the first frame after seq
    for (let seq = this.nextSeq; seq <= highest; seq++) {
      const p = this.pending.get(seq);
      if (p) {
        if (p.receivedCount !== p.fragCount) this.collectNack(seq, p, nowMs, nacks);
        continue;
      }
      // Nothing of this frame arrived, but a later frame did
      while (later < seqs.length && seqs[later] <= seq) later++;
      if (later >= seqs.length || nowMs - this.pending.get(seqs[later]).firstMs < this.nackDelay) continue;
      const last = this.missingNackMs.get(seq) || 0;
      if (last && nowMs - last < this.nackInterval) continue;
      this.missingNackMs.set(seq, nowMs);
      nacks.push({ frameSeq: seq, fragments: [] });
      this.stats.nackFragments++;
    }
    return { nacks, needKeyframe };
  }
}

// ------------------------------------------------------------------ what the stream is

/** The NAL units of a buffer with start codes (00 00 01), each without its start code. */
function nalUnits(bytes) {
  const units = [];
  let start = -1;
  let i = 0;
  while (i + 3 <= bytes.length) {
    if (bytes[i] === 0 && bytes[i + 1] === 0 && bytes[i + 2] === 1) {
      if (start >= 0) {
        let end = i;
        while (end > start && bytes[end - 1] === 0) end--; // a longer start code's zeros
        units.push(bytes.subarray(start, end));
      }
      start = i + 3;
      i += 3;
    } else i++;
  }
  if (start >= 0) units.push(bytes.subarray(start));
  return units;
}

/** Takes out the bytes an encoder puts in so that no start code appears inside a unit. */
function unescapeNal(nal, limit) {
  const out = [];
  for (let i = 0; i < nal.length && out.length < limit; i++) {
    if (i >= 2 && nal[i] === 3 && nal[i - 1] === 0 && nal[i - 2] === 0) continue;
    out.push(nal[i]);
  }
  return out;
}

const hex2 = (v) => v.toString(16).padStart(2, '0');

/**
 * The name a browser's decoder wants for the stream, read from its parameter sets.
 * @returns {string[]} names to try, the most fitting first; empty if the parameter sets make no sense
 */
export function codecStrings(codec, config) {
  const units = nalUnits(config);
  if (codec === CODEC_H264) {
    const sps = units.find((u) => (u[0] & 0x1f) === 7);
    if (!sps || sps.length < 4) return [];
    return [`avc1.${hex2(sps[1])}${hex2(sps[2])}${hex2(sps[3])}`];
  }
  const sps = units.find((u) => ((u[0] >> 1) & 0x3f) === 33);
  if (!sps) return [];
  const b = unescapeNal(sps, 16);
  if (b.length < 15) return [];
  // After the two bytes of the unit's header and one byte of ids: profile, tier and level
  const space = ['', 'A', 'B', 'C'][b[3] >> 6];
  const tier = (b[3] >> 5) & 1 ? 'H' : 'L';
  const profile = b[3] & 0x1f;
  let compat = 0;
  const flags = ((b[4] << 24) | (b[5] << 16) | (b[6] << 8) | b[7]) >>> 0;
  for (let i = 0; i < 32; i++) if (flags & (1 << i)) compat |= 1 << (31 - i);
  compat >>>= 0;
  const constraints = b.slice(8, 14);
  while (constraints.length && constraints[constraints.length - 1] === 0) constraints.pop();
  const level = b[14];
  const tail = `${space}${profile}.${compat.toString(16)}.${tier}${level}`
    + (constraints.length ? '.' + constraints.map((c) => c.toString(16)).join('.') : '');
  // hev1: the parameter sets travel with the stream, which is what the console sends
  return [`hev1.${tail}`, `hvc1.${tail}`];
}

/**
 * Decodes the frames and draws them. Each frame is shown as soon as it is
 * decoded; when frames arrive faster than the screen can show them, the
 * older ones are skipped.
 */
export class VideoPlayer {
  /**
   * @param {HTMLCanvasElement} canvas
   * @param {{onNeedKeyframe: function(): void, onSize: function(number, number): void, onError: function(string): void}} handlers
   */
  constructor(canvas, handlers) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d', { alpha: false, desynchronized: true });
    this.handlers = handlers;
    this.decoder = null;
    this.codec = CODEC_H264;
    this.config = null;
    this.codecName = '';
    this.configured = false;
    this.needKey = true;
    this.pendingFrame = null;
    this.rafId = 0;
    this.timestamp = 0;
    this.unsupported = '';
    this.stats = { decoded: 0, shown: 0, skipped: 0, errors: 0, queue: 0 };
  }

  static supported() {
    return typeof VideoDecoder === 'function' && typeof EncodedVideoChunk === 'function';
  }

  setCodec(codec) {
    if (codec !== this.codec) {
      this.codec = codec;
      this.configured = false;
      if (this.config) this.setConfig(this.config);
    }
  }

  /** New parameter sets: the decoder starts over. */
  async setConfig(config) {
    this.config = config.slice();
    this.configured = false;
    this.needKey = true;
    const names = codecStrings(this.codec, this.config);
    const tried = [];
    for (const name of names) {
      for (const hardwareAcceleration of ['no-preference', 'prefer-software']) {
        const settings = { codec: name, optimizeForLatency: true, hardwareAcceleration };
        let ok = false;
        try {
          ok = (await VideoDecoder.isConfigSupported(settings)).supported;
        } catch (e) {
          ok = false;
        }
        tried.push(name);
        if (!ok) continue;
        this.closeDecoder();
        this.decoder = new VideoDecoder({
          output: (frame) => this.onFrame(frame),
          error: (e) => this.onDecoderError(e),
        });
        this.decoder.configure(settings);
        this.codecName = name;
        this.configured = true;
        this.unsupported = '';
        return true;
      }
    }
    this.unsupported = names.length ? names[0] : 'unknown';
    this.handlers.onError(this.codec === CODEC_H264
      ? `This browser cannot play the host's picture (H.264, ${this.unsupported}).`
      : `This browser cannot play the host's picture: it is H.265 (${this.unsupported}). Chrome, Edge and Safari can on most computers; otherwise the host can switch the app to H.264 under Settings → Video.`);
    return false;
  }

  /** @param {{key: boolean, data: Uint8Array, captureMs: number}} frame one whole frame as the console sent it */
  push(frame) {
    if (!this.configured || !this.decoder || this.decoder.state !== 'configured') return;
    if (this.needKey && !frame.key) return;
    let data = frame.data;
    if (frame.key) {
      // The parameter sets in front, so that the decoder can start right here
      data = new Uint8Array(this.config.length + frame.data.length);
      data.set(this.config);
      data.set(frame.data, this.config.length);
      this.needKey = false;
    }
    // The decoder falls behind (a slow machine, a tab in the background): skip to the next key frame
    if (this.decoder.decodeQueueSize > 8) {
      this.needKey = true;
      this.stats.skipped++;
      this.handlers.onNeedKeyframe();
      return;
    }
    this.timestamp += 1;
    try {
      this.decoder.decode(new EncodedVideoChunk({
        type: frame.key ? 'key' : 'delta',
        timestamp: this.timestamp * 1000,
        data,
      }));
    } catch (e) {
      this.onDecoderError(e);
    }
    this.stats.queue = this.decoder ? this.decoder.decodeQueueSize : 0;
  }

  onFrame(frame) {
    this.stats.decoded++;
    if (this.pendingFrame) {
      this.pendingFrame.close();
      this.stats.skipped++;
    }
    this.pendingFrame = frame;
    if (!this.rafId) this.rafId = requestAnimationFrame(() => this.draw());
  }

  draw() {
    this.rafId = 0;
    const frame = this.pendingFrame;
    this.pendingFrame = null;
    if (!frame) return;
    const w = frame.displayWidth;
    const h = frame.displayHeight;
    if (this.canvas.width !== w || this.canvas.height !== h) {
      this.canvas.width = w;
      this.canvas.height = h;
      this.handlers.onSize(w, h);
    }
    this.ctx.drawImage(frame, 0, 0, w, h);
    frame.close();
    this.stats.shown++;
  }

  onDecoderError(e) {
    this.stats.errors++;
    console.warn('video decoder:', e);
    // Start over with the same parameter sets and a fresh key frame
    this.configured = false;
    this.needKey = true;
    if (this.config && this.stats.errors < 50) {
      this.setConfig(this.config).then((ok) => {
        if (ok) this.handlers.onNeedKeyframe();
      });
    }
  }

  closeDecoder() {
    if (this.decoder && this.decoder.state !== 'closed') {
      try {
        this.decoder.close();
      } catch (e) {
        // already gone
      }
    }
    this.decoder = null;
  }

  close() {
    this.closeDecoder();
    if (this.rafId) cancelAnimationFrame(this.rafId);
    this.rafId = 0;
    if (this.pendingFrame) this.pendingFrame.close();
    this.pendingFrame = null;
    this.configured = false;
  }
}
