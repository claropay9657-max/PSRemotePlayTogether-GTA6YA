// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Runs on the browser's audio thread: plays the sound that arrives from the
// host (the game, and each player's voice) and collects the microphone.

/**
 * One stream of sound that arrives in pieces over the network. It starts
 * playing once a little is buffered, so that a late piece does not make a
 * gap, and skips ahead when too much has piled up, so that the sound never
 * falls behind the picture.
 */
class Stream {
  constructor(channels, startSamples, maxSamples) {
    this.channels = channels;
    this.size = 48000; // one second
    this.buffers = [];
    for (let c = 0; c < channels; c++) this.buffers.push(new Float32Array(this.size));
    this.read = 0;
    this.write = 0;
    this.buffered = 0;
    this.started = false;
    this.startSamples = startSamples;
    this.maxSamples = maxSamples;
    this.gain = 1;
  }

  push(planes) {
    const n = planes[0].length;
    for (let c = 0; c < this.channels; c++) {
      const src = planes[Math.min(c, planes.length - 1)];
      const dst = this.buffers[c];
      let w = this.write;
      for (let i = 0; i < n; i++) {
        dst[w] = src[i];
        w = w + 1 === this.size ? 0 : w + 1;
      }
    }
    this.write = (this.write + n) % this.size;
    this.buffered += n;
    if (this.buffered > this.maxSamples) {
      // Too far behind: jump to where playing started from
      const skip = this.buffered - this.startSamples;
      this.read = (this.read + skip) % this.size;
      this.buffered -= skip;
    }
    if (!this.started && this.buffered >= this.startSamples) this.started = true;
  }

  /** Adds this stream to the output. */
  mix(left, right) {
    if (!this.started) return;
    const n = left.length;
    if (this.buffered < n) {
      // Ran dry: collect a little again before going on
      this.started = false;
      this.read = this.write;
      this.buffered = 0;
      return;
    }
    const a = this.buffers[0];
    const b = this.buffers[this.channels > 1 ? 1 : 0];
    let r = this.read;
    const g = this.gain;
    for (let i = 0; i < n; i++) {
      left[i] += a[r] * g;
      right[i] += b[r] * g;
      r = r + 1 === this.size ? 0 : r + 1;
    }
    this.read = r;
    this.buffered -= n;
  }
}

class Mixer extends AudioWorkletProcessor {
  constructor() {
    super();
    /** @type {Map<string, Stream>} */
    this.streams = new Map();
    this.port.onmessage = (e) => {
      const m = e.data;
      if (m.type === 'pcm') {
        let s = this.streams.get(m.id);
        if (!s) {
          s = new Stream(m.planes.length, m.start || 1920, m.max || 7200);
          s.gain = m.gain === undefined ? 1 : m.gain;
          this.streams.set(m.id, s);
        }
        s.push(m.planes);
      } else if (m.type === 'gain') {
        const s = this.streams.get(m.id);
        if (s) s.gain = m.gain;
      } else if (m.type === 'remove') this.streams.delete(m.id);
    };
  }

  process(inputs, outputs) {
    const out = outputs[0];
    const left = out[0];
    const right = out.length > 1 ? out[1] : out[0];
    for (const s of this.streams.values()) s.mix(left, right);
    if (out.length === 1) for (let i = 0; i < left.length; i++) left[i] *= 0.5;
    return true;
  }
}

/** Cuts the microphone into pieces of 20 ms, which is what the voice chat sends. */
class Microphone extends AudioWorkletProcessor {
  constructor() {
    super();
    this.frame = new Float32Array(960);
    this.fill = 0;
  }

  process(inputs) {
    const input = inputs[0];
    if (!input || !input.length) return true;
    const src = input[0];
    for (let i = 0; i < src.length; i++) {
      this.frame[this.fill++] = src[i];
      if (this.fill === this.frame.length) {
        const copy = this.frame.slice();
        this.port.postMessage(copy, [copy.buffer]);
        this.fill = 0;
      }
    }
    return true;
  }
}

registerProcessor('psrpt-mixer', Mixer);
registerProcessor('psrpt-microphone', Microphone);
