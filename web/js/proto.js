// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The co-play protocol as a guest in a web browser speaks it: the packets of
// coplay/include/coplay/protocol.h without header and seal (see web.h there).
// Every message on the data channel is one byte of type and the payload.

import { Reader, Writer } from './bytes.js';

export const PROTOCOL_VERSION = 5;

export const PT = {
  Hello: 0x01,
  /** The host heard the Hello and lets this player in by hand: keep asking, and wait. */
  Waiting: 0x06,
  Welcome: 0x02,
  Reject: 0x03,
  Input: 0x10,
  Ping: 0x11,
  Pong: 0x12,
  VoiceUp: 0x13,
  CamUp: 0x14,
  ChatUp: 0x15,
  ChatAck: 0x16,
  KeyMouse: 0x17,
  Video: 0x20,
  Audio: 0x21,
  Haptics: 0x22,
  State: 0x23,
  VideoConfig: 0x24,
  Inputs: 0x25,
  Voice: 0x26,
  Cam: 0x27,
  Chat: 0x28,
  ChatUpAck: 0x29,
  Nack: 0x30,
  KeyframeRequest: 0x31,
  Bye: 0x40,
  BlobRequest: 0x41,
  BlobChunk: 0x42,
};

export const REJECT_FULL = 1;
export const REJECT_VERSION = 2;
/** The host did not let this player in, or removed and blocked it. */
export const REJECT_REFUSED = 3;
/** The host has closed the session to newcomers. */
export const REJECT_LOCKED = 4;

export const BYE_QUIT = 0;
export const BYE_KICKED = 1;
export const BYE_HOST_ENDED = 2;
export const BYE_RECONNECTING = 3;
/** A guest left and asks the host to put the console to sleep; only where PARTICIPANT_MAY_SLEEP says it may. */
export const BYE_SLEEP = 4;

export const WANT_VIDEO = 1 << 0;
export const WANT_AUDIO = 1 << 1;
export const WANT_HAPTICS = 1 << 2;
export const WANT_INPUTS = 1 << 3;
export const WANT_VOICE = 1 << 4;
export const WANT_CAMS = 1 << 5;

export const MEDIA_VOICE = 1 << 0;
export const MEDIA_MUTED = 1 << 1;
export const MEDIA_CAMERA = 1 << 2;
export const MEDIA_SILENCED = 1 << 3;
/** Streams the session live (Twitch, say) right now; shown next to the name. */
export const MEDIA_LIVE = 1 << 4;
/** Asks to be left out of everybody else's live streams (voice, webcam, name). */
export const MEDIA_NO_STREAMS = 1 << 5;

export const PARTICIPANT_HOST = 1 << 0;
export const PARTICIPANT_CAN_CONTROL = 1 << 1;
export const PARTICIPANT_IN_CONTROL = 1 << 2;
/** May put the console to sleep when leaving. The host always may; guests if the host has allowed it. */
export const PARTICIPANT_MAY_SLEEP = 1 << 3;
/** The host lets this player use mouse and keyboard in the game on its computer. */
export const PARTICIPANT_KEY_MOUSE = 1 << 4;

export const BATTERY_UNKNOWN = 0xff;
export const BATTERY_WIRED = 0xfe;

export const CONTROLLER_NONE = 0;
export const CONTROLLER_DUALSENSE = 1;
export const CONTROLLER_XBOX = 2;
export const CONTROLLER_OTHER = 3;
export const CONTROLLER_DUALSHOCK4 = 4;

export const CODEC_H264 = 0;
export const CODEC_H265 = 1;
export const CODEC_H265_HDR = 2;

export const MIX_TOGETHER = 0;
export const MIX_LAST_ACTIVE = 1;
export const MIX_HOST_ONLY = 2;
/** Every player on a controller of their own: a game on the host's computer. */
export const MIX_EACH_OWN = 3;

/** Where the game runs (StreamInfo source). */
export const SOURCE_CONSOLE = 0;
export const SOURCE_COMPUTER = 1;
export const SOURCE_XBOX_APP = 2;

export const MAX_PAYLOAD = 1232 - 14 - 16;
export const VIDEO_FRAGMENT_SIZE = 1160;
export const MAX_VIDEO_FRAME_SIZE = 2 * 1024 * 1024;
export const MAX_NAME = 32;
export const MAX_GUESTS = 8;
export const MAX_VOICE_FRAME = 1000;
export const CAM_FRAGMENT_SIZE = 1150;
export const MAX_CAM_FRAME = 256 * 1024;
export const BLOB_CHUNK_SIZE = 1024;
export const MAX_AVATAR_SIZE = 48 * 1024;
export const MAX_CHAT_TEXT = 400;

export const VIDEO_KEY = 1 << 0;
export const VIDEO_RECOVERED = 1 << 1;
export const VIDEO_RETRANSMIT = 1 << 2;

export const NACK_WHOLE_FRAME = 0xffff;
export const CHAT_HISTORY = 1 << 0;

/* Same bit values as the app's controller buttons. */
export const BTN = {
  Cross: 1 << 0,
  Circle: 1 << 1,
  Square: 1 << 2,
  Triangle: 1 << 3,
  DpadLeft: 1 << 4,
  DpadRight: 1 << 5,
  DpadUp: 1 << 6,
  DpadDown: 1 << 7,
  L1: 1 << 8,
  R1: 1 << 9,
  L3: 1 << 10,
  R3: 1 << 11,
  Options: 1 << 12,
  Share: 1 << 13,
  Touchpad: 1 << 14,
  Ps: 1 << 15,
};

/** A message: the type and its payload. */
export function frame(type, payload) {
  const out = new Uint8Array(1 + (payload ? payload.length : 0));
  out[0] = type;
  if (payload) out.set(payload, 1);
  return out;
}

function readBlobRef(r) {
  const hash = r.bytes(8).slice();
  const size = r.u16();
  return { hash, size, valid: size !== 0 };
}

function writeBlobRef(w, ref) {
  w.bytes(ref && ref.size ? ref.hash : new Uint8Array(8));
  w.u16(ref && ref.size ? ref.size : 0);
}

function readStreamInfo(r) {
  const codec = r.u8();
  const width = r.u16();
  const height = r.u16();
  const fps = r.u8();
  const flags = r.u8();
  return { codec, width, height, fps, ps5: (flags & 1) !== 0, dualsense: (flags & 2) !== 0, source: (flags >> 2) & 3 };
}

function readAudioHeader(r) {
  return {
    valid: r.u8() !== 0,
    channels: r.u8(),
    bits: r.u8(),
    rate: r.u32(),
    frameSize: r.u32(),
    unknown: r.u32(),
  };
}

function readFeedback(r) {
  const f = {};
  f.rumbleSeq = r.u16();
  f.rumbleLeft = r.u8();
  f.rumbleRight = r.u8();
  f.triggerSeq = r.u16();
  f.triggerTypeLeft = r.u8();
  f.triggerTypeRight = r.u8();
  f.triggerLeft = r.bytes(10).slice();
  f.triggerRight = r.bytes(10).slice();
  f.ledSeq = r.u16();
  f.led = r.bytes(3).slice();
  f.playerIndexSeq = r.u16();
  f.playerIndex = r.u8();
  f.hapticIntensitySeq = r.u16();
  f.hapticIntensity = r.u8();
  f.triggerIntensitySeq = r.u16();
  f.triggerIntensity = r.u8();
  f.motionResetSeq = r.u16();
  return f;
}

/** The first message: the version in front, then what a guest says about itself. */
export function helloFrame({ nonce, want, controller, name, avatar, media, battery, playerId }) {
  const w = new Writer(96);
  w.u8(PT.Hello);
  w.u8(PROTOCOL_VERSION);
  w.bytes(nonce);
  w.u8(want);
  w.u8(controller);
  w.str8(name, MAX_NAME);
  writeBlobRef(w, avatar);
  w.u8(media);
  w.u8(battery);
  w.bytes(playerId); // 8 random bytes, the same for as long as the page is open
  return w.done();
}

/** @returns {object|null} */
export function readWelcome(payload) {
  const r = new Reader(payload);
  r.bytes(16); // the guest's nonce, of no use on this connection
  r.bytes(16);
  const connId = r.u32();
  const slot = r.u8();
  const hostName = r.str8();
  const stream = readStreamInfo(r);
  const audio = readAudioHeader(r);
  return r.ok ? { connId, slot, hostName, stream, audio } : null;
}

/** @returns {number} the reason, 0 if the message is too short */
export function readReject(payload) {
  return payload.length > 16 ? payload[16] : 0;
}

/** @returns {object|null} */
export function readState(payload) {
  const r = new Reader(payload);
  const s = {};
  s.seq = r.u32();
  s.stream = readStreamInfo(r);
  s.audio = readAudioHeader(r);
  s.videoConfigId = r.u16();
  s.feedback = readFeedback(r);
  s.mixMode = r.u8();
  s.yourSlot = r.u8();
  const count = r.u8();
  if (!r.ok || count > MAX_GUESTS + 1) return null;
  s.participants = [];
  for (let i = 0; i < count; i++) {
    const p = {};
    p.slot = r.u8();
    p.flags = r.u8();
    p.name = r.str8();
    p.avatar = readBlobRef(r);
    p.controller = r.u8();
    p.media = r.u8();
    p.battery = r.u8();
    // Each their own controller: the player the game sees (1 the host), 0 if none
    p.player = r.u8();
    if (!r.ok) return null;
    p.host = (p.flags & PARTICIPANT_HOST) !== 0;
    p.canControl = (p.flags & PARTICIPANT_CAN_CONTROL) !== 0;
    p.inControl = (p.flags & PARTICIPANT_IN_CONTROL) !== 0;
    p.maySleep = (p.flags & PARTICIPANT_MAY_SLEEP) !== 0;
    s.participants.push(p);
  }
  return r.ok ? s : null;
}

/** The touchpad's size as the console counts it. */
export const TOUCHPAD_WIDTH = 1920;
export const TOUCHPAD_HEIGHT = 1079;

/**
 * A controller state at rest. Sticks are -32768..32767, triggers 0..255.
 * touches: up to two fingers on the touchpad, each {id, x, y}, id -1 = none.
 * motion: null, or {gyro: [x, y, z] in rad/s, accel: [x, y, z] in g, orient: [x, y, z, w]}.
 */
export function emptyPad() {
  return {
    buttons: 0, l2: 0, r2: 0, lx: 0, ly: 0, rx: 0, ry: 0,
    touchIdNext: 0,
    touches: [{ id: -1, x: 0, y: 0 }, { id: -1, x: 0, y: 0 }],
    motion: null,
  };
}

function touchesEqual(a, b) {
  for (let i = 0; i < 2; i++) {
    const p = a[i];
    const q = b[i];
    if (p.id !== q.id || (p.id >= 0 && (p.x !== q.x || p.y !== q.y))) return false;
  }
  return true;
}

function motionEquals(a, b) {
  if (!a || !b) return a === b;
  for (let i = 0; i < 3; i++) if (a.gyro[i] !== b.gyro[i] || a.accel[i] !== b.accel[i]) return false;
  for (let i = 0; i < 4; i++) if (a.orient[i] !== b.orient[i]) return false;
  return true;
}

export function padEquals(a, b) {
  return a.buttons === b.buttons && a.l2 === b.l2 && a.r2 === b.r2 && a.lx === b.lx && a.ly === b.ly
    && a.rx === b.rx && a.ry === b.ry && touchesEqual(a.touches, b.touches) && motionEquals(a.motion, b.motion);
}

export function copyPad(pad) {
  return {
    buttons: pad.buttons, l2: pad.l2, r2: pad.r2, lx: pad.lx, ly: pad.ly, rx: pad.rx, ry: pad.ry,
    touchIdNext: pad.touchIdNext,
    touches: pad.touches.map((t) => ({ id: t.id, x: t.x, y: t.y })),
    motion: pad.motion
      ? { gyro: pad.motion.gyro.slice(), accel: pad.motion.accel.slice(), orient: pad.motion.orient.slice() } : null,
  };
}

export function inputFrame(seq, pad) {
  const w = new Writer(72);
  w.u8(PT.Input);
  w.u32(seq);
  // Motion data only when there is some: most controllers in a browser have none
  w.u8(pad.motion ? 1 : 0);
  w.u32(pad.buttons);
  w.u8(pad.l2);
  w.u8(pad.r2);
  w.i16(pad.lx);
  w.i16(pad.ly);
  w.i16(pad.rx);
  w.i16(pad.ry);
  w.u8(pad.touchIdNext & 0xff);
  for (let i = 0; i < 2; i++) {
    const t = pad.touches[i];
    w.u16(t.id >= 0 ? t.x : 0);
    w.u16(t.id >= 0 ? t.y : 0);
    w.i8(t.id >= 0 ? t.id & 0x7f : -1);
  }
  if (pad.motion) {
    for (let i = 0; i < 3; i++) w.f32(pad.motion.gyro[i]);
    for (let i = 0; i < 3; i++) w.f32(pad.motion.accel[i]);
    for (let i = 0; i < 4; i++) w.f32(pad.motion.orient[i]);
  }
  return w.done();
}

/** Mouse and keyboard: movement and wheel as running totals, keys held as USB HID usages. */
export function keyMouseFrame(seq, km) {
  const keys = km.keys.slice(0, 16);
  const w = new Writer(24 + keys.length);
  w.u8(PT.KeyMouse);
  w.u32(seq);
  w.u8(km.buttons & 0xff);
  w.u32(km.moveX >>> 0);
  w.u32(km.moveY >>> 0);
  w.u32(km.wheelY >>> 0);
  w.u32(km.wheelX >>> 0);
  w.u8(keys.length);
  for (const k of keys) w.u8(k);
  return w.done();
}

function readPlayerInput(r) {
  return {
    slot: r.u8(),
    buttons: r.u32(),
    l2: r.u8(),
    r2: r.u8(),
    lx: r.i16(),
    ly: r.i16(),
    rx: r.i16(),
    ry: r.i16(),
  };
}

/** What every player is pressing right now, and what the console gets out of it. */
export function readInputs(payload) {
  const r = new Reader(payload);
  const seq = r.u32();
  const count = r.u8();
  if (!r.ok || count > MAX_GUESTS + 1) return null;
  const players = [];
  for (let i = 0; i < count; i++) players.push(readPlayerInput(r));
  const combined = readPlayerInput(r);
  return r.ok ? { seq, players, combined } : null;
}

/**
 * The packets for one picture of the own webcam.
 * @param {number} seq counts up with every picture
 * @param {Uint8Array} jpeg at most MAX_CAM_FRAME bytes
 * @returns {Uint8Array[]}
 */
export function camUpFrames(seq, jpeg) {
  const out = [];
  if (!jpeg.length || jpeg.length > MAX_CAM_FRAME) return out;
  const count = Math.ceil(jpeg.length / CAM_FRAGMENT_SIZE);
  for (let i = 0; i < count; i++) {
    const part = jpeg.subarray(i * CAM_FRAGMENT_SIZE, Math.min(jpeg.length, (i + 1) * CAM_FRAGMENT_SIZE));
    const w = new Writer(part.length + 16);
    w.u8(PT.CamUp);
    w.u32(seq);
    w.u16(i);
    w.u16(count);
    w.u32(jpeg.length);
    w.bytes(part);
    out.push(w.done());
  }
  return out;
}

export function pingFrame({ id, sentUs, want, rtt, controller, media, battery }) {
  const w = new Writer(24);
  w.u8(PT.Ping);
  w.u32(id);
  w.u64(sentUs);
  w.u8(want);
  w.u16(Math.min(0xffff, rtt));
  w.u8(controller);
  w.u8(media);
  w.u8(battery);
  return w.done();
}

/** @returns {{id: number, sentUs: number}|null} */
export function readPong(payload) {
  const r = new Reader(payload);
  const id = r.u32();
  const sentUs = r.u64();
  return r.ok ? { id, sentUs } : null;
}

/** @returns {object|null} the header, with data: the fragment's bytes */
export function readVideoFragment(payload) {
  const r = new Reader(payload);
  const h = {};
  h.frameSeq = r.u32();
  h.fragIndex = r.u16();
  h.fragCount = r.u16();
  h.flags = r.u8();
  h.framesLost = r.u8();
  h.totalSize = r.u32();
  h.captureMs = r.u32();
  if (!r.ok) return null;
  h.data = r.rest();
  return h;
}

/**
 * Nack packets for the entries, as many per packet as fit.
 * @param {{frameSeq: number, fragments: number[]}[]} entries an empty list of fragments asks for the whole frame
 * @returns {Uint8Array[]}
 */
export function nackFrames(entries) {
  const out = [];
  let w = null;
  let used = 0;
  for (const e of entries) {
    const need = 6 + e.fragments.length * 2;
    if (need > MAX_PAYLOAD) continue;
    if (!w || used + need > MAX_PAYLOAD) {
      if (w) out.push(w.done());
      w = new Writer(256);
      w.u8(PT.Nack);
      used = 0;
    }
    w.u32(e.frameSeq);
    if (e.fragments.length === 0) w.u16(NACK_WHOLE_FRAME);
    else {
      w.u16(e.fragments.length);
      for (const f of e.fragments) w.u16(f);
    }
    used += need;
  }
  if (w) out.push(w.done());
  return out;
}

/** @returns {object|null} */
export function readChat(payload) {
  const r = new Reader(payload);
  const m = {};
  m.seq = r.u32();
  m.upId = r.u32();
  m.slot = r.u8();
  m.flags = r.u8();
  m.timeMs = r.u64();
  m.name = r.str8();
  m.text = r.str16();
  const hasEmbed = r.u8();
  if (!r.ok) return null;
  m.history = (m.flags & CHAT_HISTORY) !== 0;
  m.embed = null;
  if (hasEmbed) {
    const e = {};
    e.url = r.str16();
    e.site = r.str8();
    e.title = r.str8();
    e.description = r.str16();
    e.image = readBlobRef(r);
    if (!r.ok || !e.url) return null;
    m.embed = e;
  } else if (!m.text) return null;
  return m;
}

/** What a guest writes: only its own number and the text count, the host fills in the rest. */
export function chatUpFrame(id, text) {
  const w = new Writer(64 + text.length * 3);
  w.u8(PT.ChatUp);
  w.u32(id);
  w.u32(0);
  w.u8(0);
  w.u8(0);
  w.u64(0);
  w.str8('');
  w.str16(text, MAX_CHAT_TEXT);
  w.u8(0); // no link preview
  return w.done();
}

export function u32Frame(type, value) {
  const w = new Writer(8);
  w.u8(type);
  w.u32(value);
  return w.done();
}

export function byeFrame(reason = BYE_QUIT) {
  return new Uint8Array([PT.Bye, reason]);
}

export function blobRequestFrame(hash, index) {
  const w = new Writer(16);
  w.u8(PT.BlobRequest);
  w.bytes(hash);
  w.u16(index);
  return w.done();
}

/** @returns {{hash: Uint8Array, index: number}|null} */
export function readBlobRequest(payload) {
  const r = new Reader(payload);
  const hash = r.bytes(8).slice();
  const index = r.u16();
  return r.ok ? { hash, index } : null;
}

/** @returns {object|null} */
export function readBlobChunk(payload) {
  const r = new Reader(payload);
  const c = {};
  c.hash = r.bytes(8).slice();
  c.index = r.u16();
  c.count = r.u16();
  c.totalSize = r.u32();
  if (!r.ok) return null;
  c.data = r.rest();
  return c;
}

export function blobChunkFrame(hash, data, index) {
  const count = Math.ceil(data.length / BLOB_CHUNK_SIZE);
  if (index >= count) return null;
  const offset = index * BLOB_CHUNK_SIZE;
  const w = new Writer(BLOB_CHUNK_SIZE + 24);
  w.u8(PT.BlobChunk);
  w.bytes(hash);
  w.u16(index);
  w.u16(count);
  w.u32(data.length);
  w.bytes(data.subarray(offset, Math.min(data.length, offset + BLOB_CHUNK_SIZE)));
  return w.done();
}

export function voiceUpFrame(seq, opus) {
  const w = new Writer(opus.length + 8);
  w.u8(PT.VoiceUp);
  w.u32(seq);
  w.bytes(opus);
  return w.done();
}

/** @returns {{slot: number, seq: number, data: Uint8Array}|null} */
export function readVoice(payload) {
  const r = new Reader(payload);
  const slot = r.u8();
  const seq = r.u32();
  if (!r.ok || slot > MAX_GUESTS || r.remaining === 0 || r.remaining > MAX_VOICE_FRAME) return null;
  return { slot, seq, data: r.rest() };
}

/** @returns {object|null} one piece of somebody's webcam picture */
export function readCam(payload) {
  const r = new Reader(payload);
  const c = {};
  c.slot = r.u8();
  c.frameSeq = r.u32();
  c.fragIndex = r.u16();
  c.fragCount = r.u16();
  c.totalSize = r.u32();
  if (!r.ok || c.slot > MAX_GUESTS || c.totalSize === 0 || c.totalSize > MAX_CAM_FRAME) return null;
  if (c.fragCount !== Math.ceil(c.totalSize / CAM_FRAGMENT_SIZE) || c.fragIndex >= c.fragCount) return null;
  const expected = c.fragIndex + 1 === c.fragCount ? c.totalSize - c.fragIndex * CAM_FRAGMENT_SIZE : CAM_FRAGMENT_SIZE;
  if (r.remaining !== expected) return null;
  c.data = r.rest();
  return c;
}

export function hashKey(hash) {
  let s = '';
  for (const b of hash) s += b.toString(16).padStart(2, '0');
  return s;
}
