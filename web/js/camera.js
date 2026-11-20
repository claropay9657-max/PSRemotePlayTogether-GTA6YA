// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The player's own webcam: pictures of it go to the host as JPEG, a few per
// second, and from there to the other players. Each picture stands on its
// own, so a lost one is simply skipped.

import { MAX_CAM_FRAME } from './proto.js';

/** The same three sizes the app offers. */
export const CAMERA_SIZES = [
  { name: 'Small', width: 320, height: 180, intervalMs: 125, quality: 0.5 },
  { name: 'Medium', width: 480, height: 270, intervalMs: 83, quality: 0.55 },
  { name: 'Large', width: 640, height: 360, intervalMs: 66, quality: 0.6 },
];

export class Camera {
  constructor() {
    this.stream = null;
    this.ownsStream = false;
    this.video = null;
    this.canvas = document.createElement('canvas');
    this.ctx = this.canvas.getContext('2d', { alpha: false });
    this.size = CAMERA_SIZES[1];
    this.timer = 0;
    this.busy = false;
    this.frames = 0;
    /** @type {function(Uint8Array): void} called with every picture */
    this.onFrame = null;
  }

  static supported() {
    return !!(navigator.mediaDevices && navigator.mediaDevices.getUserMedia);
  }

  get on() {
    return !!this.stream;
  }

  /**
   * Switches the webcam on (the browser asks for permission).
   * @param {number} sizeIndex into CAMERA_SIZES
   * @param {MediaStream} [source] pictures from somewhere else than a camera, for trying things out
   * @returns {Promise<string>} empty if it worked, otherwise why not
   */
  async start(sizeIndex, source) {
    this.stop();
    this.size = CAMERA_SIZES[sizeIndex] || CAMERA_SIZES[1];
    try {
      if (source) {
        this.stream = source;
        this.ownsStream = false;
      } else {
        if (!Camera.supported()) return 'This browser cannot use a webcam.';
        this.stream = await navigator.mediaDevices.getUserMedia({
          audio: false,
          video: {
            width: { ideal: this.size.width },
            height: { ideal: this.size.height },
            aspectRatio: { ideal: 16 / 9 },
            frameRate: { ideal: 15, max: 30 },
          },
        });
        this.ownsStream = true;
      }
    } catch (e) {
      this.stream = null;
      return e && e.name === 'NotAllowedError' ? 'The browser was not allowed to use the webcam.'
        : e && e.name === 'NotFoundError' ? 'No webcam was found.'
          : 'The webcam could not be opened: ' + (e && e.message ? e.message : e);
    }
    this.video = document.createElement('video');
    this.video.muted = true;
    this.video.playsInline = true;
    this.video.srcObject = this.stream;
    try {
      await this.video.play();
    } catch (e) {
      // starts by itself once there is a picture
    }
    this.canvas.width = this.size.width;
    this.canvas.height = this.size.height;
    this.timer = setInterval(() => this.capture(), this.size.intervalMs);
    // Somebody pulls the plug, or takes the permission back
    const track = this.stream.getVideoTracks()[0];
    if (track) track.addEventListener('ended', () => this.stop(true));
    return '';
  }

  capture() {
    const video = this.video;
    if (!video || this.busy || video.readyState < 2 || !video.videoWidth) return;
    // Fill the 16:9 picture, cutting off what does not fit
    const w = this.canvas.width;
    const h = this.canvas.height;
    const scale = Math.max(w / video.videoWidth, h / video.videoHeight);
    const sw = w / scale;
    const sh = h / scale;
    this.ctx.drawImage(video, (video.videoWidth - sw) / 2, (video.videoHeight - sh) / 2, sw, sh, 0, 0, w, h);
    this.busy = true;
    this.encode(this.size.quality);
  }

  encode(quality) {
    this.canvas.toBlob(async (blob) => {
      if (!blob || !this.stream) {
        this.busy = false;
        return;
      }
      // A picture that is too large for the protocol is made again with less detail
      if (blob.size > MAX_CAM_FRAME && quality > 0.2) {
        this.encode(quality - 0.15);
        return;
      }
      this.busy = false;
      if (blob.size > MAX_CAM_FRAME) return;
      const jpeg = new Uint8Array(await blob.arrayBuffer());
      this.frames++;
      if (this.onFrame && this.stream) this.onFrame(jpeg);
    }, 'image/jpeg', quality);
  }

  /** @param {boolean} [byItself] the camera went away without the player switching it off */
  stop(byItself) {
    clearInterval(this.timer);
    this.timer = 0;
    if (this.stream && this.ownsStream) for (const track of this.stream.getTracks()) track.stop();
    const was = !!this.stream;
    this.stream = null;
    if (this.video) this.video.srcObject = null;
    this.video = null;
    this.busy = false;
    if (was && byItself && this.onEnded) this.onEnded();
  }
}

/**
 * Makes a player's picture out of an image file: a square of at most 256
 * pixels, as JPEG, small enough for the protocol.
 * @param {Blob} file
 * @param {number} maxBytes
 * @returns {Promise<Uint8Array|null>} null if the file is no picture
 */
export async function makeAvatar(file, maxBytes) {
  let bitmap;
  try {
    bitmap = await createImageBitmap(file);
  } catch (e) {
    return null;
  }
  const canvas = document.createElement('canvas');
  for (const size of [256, 192, 128, 96]) {
    canvas.width = size;
    canvas.height = size;
    const ctx = canvas.getContext('2d', { alpha: false });
    const side = Math.min(bitmap.width, bitmap.height);
    ctx.drawImage(bitmap, (bitmap.width - side) / 2, (bitmap.height - side) / 2, side, side, 0, 0, size, size);
    for (const quality of [0.85, 0.7, 0.5]) {
      const blob = await new Promise((resolve) => canvas.toBlob(resolve, 'image/jpeg', quality));
      if (blob && blob.size <= maxBytes) return new Uint8Array(await blob.arrayBuffer());
    }
  }
  return null;
}
