// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Trying the microphone, the speakers and the webcam before a session, as the app does
// (gui/src/devicecheck.cpp): how loud the microphone hears the player, with the mark from
// which the others would hear them; a chime on the left, then on the right, then on both;
// and the webcam's picture as the others would get it. Nothing of it is sent anywhere.

import { GATE_RMS } from './audio.js';
import { Camera } from './camera.js';

const RATE = 48000;
/** A test ends by itself after this long: nobody leaves a microphone or a camera open by forgetting it. */
const TEST_MS = 2 * 60 * 1000;

/** When each part of the chime starts, in ms: left, right, both, the end. */
export const CHIME_STEPS_MS = [0, 850, 1700, 3200];

/** One soft mallet note (a marimba's partials, the high ones dying first) added into `left` and `right` at `startMs`. */
function addNote(left, right, startMs, freq, gain, pan) {
  const partials = [[1.0, 1.0, 1.9], [3.93, 0.16, 7.5], [9.24, 0.05, 16.0]];
  const start = Math.floor((startMs * RATE) / 1000);
  const length = Math.floor(RATE * 1.6);
  const attack = 0.006 * RATE;
  // Constant power across the sides
  const gainLeft = gain * Math.cos((pan * Math.PI) / 2);
  const gainRight = gain * Math.sin((pan * Math.PI) / 2);
  for (let i = 0; i < length && start + i < left.length; i++) {
    const t = i / RATE;
    let v = 0;
    for (const [ratio, amp, decay] of partials) {
      if (freq * ratio >= RATE / 2 - 2000) continue;
      v += amp * Math.exp(-decay * t) * Math.sin(2 * Math.PI * freq * ratio * t);
    }
    if (i < attack) v *= 0.5 - 0.5 * Math.cos((Math.PI * i) / attack);
    left[start + i] += Math.fround(v * gainLeft);
    right[start + i] += Math.fround(v * gainRight);
  }
}

/**
 * The app's test chime, sample for sample (devicecheck::Chime): 3.2 s at 48 kHz.
 * @returns {{left: Float32Array, right: Float32Array, rate: number}} between -1 and 1
 */
export function chime() {
  const total = (CHIME_STEPS_MS[3] * RATE) / 1000;
  const left = new Float32Array(total);
  const right = new Float32Array(total);
  // Left: G4 then C5. Right: E5 then G5. Both: C major, softer, a little apart
  addNote(left, right, 0, 392.00, 0.30, 0.0);
  addNote(left, right, 240, 523.25, 0.30, 0.0);
  addNote(left, right, 850, 659.25, 0.30, 1.0);
  addNote(left, right, 1090, 783.99, 0.30, 1.0);
  addNote(left, right, 1700, 523.25, 0.16, 0.25);
  addNote(left, right, 1740, 659.25, 0.14, 0.75);
  addNote(left, right, 1780, 783.99, 0.13, 0.4);
  addNote(left, right, 1820, 1046.50, 0.11, 0.6);

  let peak = 0;
  for (let i = 0; i < total; i++) peak = Math.max(peak, Math.abs(left[i]), Math.abs(right[i]));
  // About -9 dBFS at its loudest: clear, never harsh
  const scale = peak > 0 ? Math.fround(Math.fround(0.35) / peak) : 0;
  const fadeLength = (RATE * 60) / 1000;
  for (let i = 0; i < total; i++) {
    // The last 60 ms fade out, so the end never clicks
    const leftOver = total - i;
    const fade = leftOver < fadeLength ? Math.fround(leftOver / fadeLength) : 1;
    left[i] = Math.fround(Math.fround(left[i] * scale) * fade);
    right[i] = Math.fround(Math.fround(right[i] * scale) * fade);
  }
  return { left, right, rate: RATE };
}

/** How loud, from 0 to 100, as the app counts it: -60 dBFS to 0 dBFS of the samples' root mean square (samples from -1 to 1). */
export function levelOf(rms) {
  if (!(rms * 32768 >= 1)) return 0;
  const db = 20 * Math.log10(rms);
  return Math.trunc(Math.max(0, Math.min(100, ((db + 60) / 60) * 100)));
}

/** Where on that scale the voice chat starts to send: from here on the others hear the player. */
export const MIC_GATE = Math.round(((20 * Math.log10(GATE_RMS) + 60) / 60) * 100);

export class DeviceCheck {
  /** @param {() => void} onChange something about the tests changed */
  constructor(onChange) {
    this.onChange = onChange || (() => {});
    this.micTesting = false;
    this.micLevel = 0;
    this.micError = '';
    /** What the browser calls the microphone it opened; "" until one was. */
    this.micName = '';
    this.speakerTesting = false;
    /** 1 the left, 2 the right, 3 both; 0 while nothing plays. */
    this.speakerSide = 0;
    this.speakerError = '';
    this.cameraTesting = false;
    this.cameraError = '';
    /** The newest picture as the others would get it (an address of a JPEG), "" until there is one. */
    this.cameraPicture = '';
    this.camera = null;
    this.timers = [];
  }

  // ------------------------------------------------------------------ the microphone

  /** Opens the microphone and listens: the browser asks for permission. Call it from a click. */
  async startMic() {
    this.stopMic();
    this.micError = '';
    if (!(navigator.mediaDevices && navigator.mediaDevices.getUserMedia) || !(window.AudioContext || window.webkitAudioContext)) {
      this.micError = 'this browser cannot use a microphone';
      this.onChange();
      return;
    }
    this.micTesting = true;
    this.onChange();
    try {
      // The same microphone, treated the same way, as the voice chat opens
      const stream = await navigator.mediaDevices.getUserMedia({
        audio: { echoCancellation: true, noiseSuppression: true, autoGainControl: true, channelCount: 1 },
        video: false,
      });
      if (!this.micTesting) {
        for (const track of stream.getTracks()) track.stop();
        return;
      }
      this.micStream = stream;
      const track = stream.getAudioTracks()[0];
      this.micName = (track && track.label) || '';
      if (track) track.addEventListener('ended', () => this.stopMic());
      this.micContext = new (window.AudioContext || window.webkitAudioContext)();
      const source = this.micContext.createMediaStreamSource(stream);
      // Only listening: it goes to nobody, and to the speakers only through a gain of nothing
      // (some browsers work out no sound that leads nowhere)
      const analyser = this.micContext.createAnalyser();
      analyser.fftSize = 1024;
      analyser.smoothingTimeConstant = 0;
      const silent = this.micContext.createGain();
      silent.gain.value = 0;
      source.connect(analyser);
      analyser.connect(silent);
      silent.connect(this.micContext.destination);
      if (this.micContext.state === 'suspended') await this.micContext.resume().catch(() => {});
      const samples = new Float32Array(analyser.fftSize);
      this.micPoll = setInterval(() => {
        analyser.getFloatTimeDomainData(samples);
        let sum = 0;
        for (let i = 0; i < samples.length; i++) sum += samples[i] * samples[i];
        const level = levelOf(Math.sqrt(sum / samples.length));
        if (level === this.micLevel) return;
        this.micLevel = level;
        this.onChange();
      }, 50);
      this.micLimit = setTimeout(() => this.stopMic(), TEST_MS);
      this.onChange();
    } catch (e) {
      this.stopMic();
      this.micError = e && e.name === 'NotAllowedError' ? 'the browser was not allowed to use it'
        : e && e.name === 'NotFoundError' ? 'none was found' : String((e && e.message) || e);
      this.onChange();
    }
  }

  stopMic() {
    clearInterval(this.micPoll);
    clearTimeout(this.micLimit);
    if (this.micStream) for (const track of this.micStream.getTracks()) track.stop();
    this.micStream = null;
    if (this.micContext) this.micContext.close().catch(() => {});
    this.micContext = null;
    const was = this.micTesting || this.micLevel > 0;
    this.micTesting = false;
    this.micLevel = 0;
    if (was) this.onChange();
  }

  // ------------------------------------------------------------------ the speakers

  /** A chime on the left, then on the right, then on both. Call it from a click. */
  async playTestSound() {
    this.stopSpeakers();
    this.speakerError = '';
    const AudioContextClass = window.AudioContext || window.webkitAudioContext;
    if (!AudioContextClass) {
      this.speakerError = 'this browser cannot play sound';
      this.onChange();
      return;
    }
    try {
      const context = new AudioContextClass();
      this.speakerContext = context;
      const sound = chime();
      const buffer = context.createBuffer(2, sound.left.length, sound.rate);
      buffer.copyToChannel(sound.left, 0);
      buffer.copyToChannel(sound.right, 1);
      const source = context.createBufferSource();
      source.buffer = buffer;
      source.connect(context.destination);
      await context.resume();
      if (this.speakerContext !== context) return;
      source.start();
      this.speakerTesting = true;
      this.speakerSide = 1;
      this.timers = [
        setTimeout(() => this.setSide(2), CHIME_STEPS_MS[1]),
        setTimeout(() => this.setSide(3), CHIME_STEPS_MS[2]),
        setTimeout(() => this.stopSpeakers(), CHIME_STEPS_MS[3]),
      ];
      this.onChange();
    } catch (e) {
      this.stopSpeakers();
      this.speakerError = String((e && e.message) || e);
      this.onChange();
    }
  }

  setSide(side) {
    this.speakerSide = side;
    this.onChange();
  }

  stopSpeakers() {
    for (const timer of this.timers) clearTimeout(timer);
    this.timers = [];
    if (this.speakerContext) this.speakerContext.close().catch(() => {});
    this.speakerContext = null;
    const was = this.speakerTesting;
    this.speakerTesting = false;
    this.speakerSide = 0;
    if (was) this.onChange();
  }

  // ------------------------------------------------------------------ the webcam

  /** Shows the webcam the way the others would get it, at this size (an index into CAMERA_SIZES). */
  async startCamera(sizeIndex) {
    this.stopCamera();
    this.cameraError = '';
    this.cameraTesting = true;
    this.onChange();
    const camera = new Camera();
    this.camera = camera;
    camera.onFrame = (jpeg) => {
      if (this.camera !== camera) return;
      const old = this.cameraPicture;
      this.cameraPicture = URL.createObjectURL(new Blob([jpeg], { type: 'image/jpeg' }));
      this.onChange();
      if (old) URL.revokeObjectURL(old);
    };
    camera.onEnded = () => {
      this.cameraError = 'The camera went away.';
      this.stopCamera();
    };
    const problem = await camera.start(sizeIndex);
    if (this.camera !== camera) {
      camera.stop();
      return;
    }
    if (problem) {
      this.stopCamera();
      this.cameraError = problem;
      this.onChange();
      return;
    }
    this.cameraLimit = setTimeout(() => this.stopCamera(), TEST_MS);
  }

  stopCamera() {
    clearTimeout(this.cameraLimit);
    const was = this.cameraTesting;
    if (this.camera) this.camera.stop();
    this.camera = null;
    this.cameraTesting = false;
    if (this.cameraPicture) URL.revokeObjectURL(this.cameraPicture);
    this.cameraPicture = '';
    if (was) this.onChange();
  }

  stopAll() {
    this.stopMic();
    this.stopSpeakers();
    this.stopCamera();
  }
}
