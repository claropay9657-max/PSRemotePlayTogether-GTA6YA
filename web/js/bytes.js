// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Big-endian reading and writing, like coplay/include/coplay/bytes.h.

const utf8Decoder = new TextDecoder('utf-8', { fatal: false });
const utf8Encoder = new TextEncoder();

/** Reads big-endian values. Reading past the end yields zeros and clears ok. */
export class Reader {
  /** @param {Uint8Array} bytes */
  constructor(bytes) {
    this.b = bytes;
    this.view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    this.p = 0;
    this.ok = true;
  }

  get remaining() {
    return this.ok ? this.b.length - this.p : 0;
  }

  need(n) {
    if (!this.ok || n > this.b.length - this.p) {
      this.ok = false;
      return false;
    }
    return true;
  }

  u8() {
    return this.need(1) ? this.b[this.p++] : 0;
  }

  i8() {
    const v = this.u8();
    return v > 127 ? v - 256 : v;
  }

  u16() {
    if (!this.need(2)) return 0;
    const v = this.view.getUint16(this.p);
    this.p += 2;
    return v;
  }

  i16() {
    if (!this.need(2)) return 0;
    const v = this.view.getInt16(this.p);
    this.p += 2;
    return v;
  }

  u32() {
    if (!this.need(4)) return 0;
    const v = this.view.getUint32(this.p);
    this.p += 4;
    return v;
  }

  /** As a number: exact up to 2^53, which covers times and counters. */
  u64() {
    const hi = this.u32();
    const lo = this.u32();
    return hi * 4294967296 + lo;
  }

  f32() {
    if (!this.need(4)) return 0;
    const v = this.view.getFloat32(this.p);
    this.p += 4;
    return v;
  }

  /** A view of the next n bytes, not a copy. */
  bytes(n) {
    if (!this.need(n)) return new Uint8Array(0);
    const v = this.b.subarray(this.p, this.p + n);
    this.p += n;
    return v;
  }

  rest() {
    return this.bytes(this.remaining);
  }

  str8() {
    return utf8Decoder.decode(this.bytes(this.u8()));
  }

  str16() {
    return utf8Decoder.decode(this.bytes(this.u16()));
  }
}

/** Collects big-endian values into a byte array. */
export class Writer {
  constructor(capacity = 64) {
    this.b = new Uint8Array(capacity);
    this.view = new DataView(this.b.buffer);
    this.n = 0;
  }

  grow(extra) {
    if (this.n + extra <= this.b.length) return;
    let size = this.b.length * 2;
    while (size < this.n + extra) size *= 2;
    const next = new Uint8Array(size);
    next.set(this.b.subarray(0, this.n));
    this.b = next;
    this.view = new DataView(next.buffer);
  }

  u8(v) {
    this.grow(1);
    this.b[this.n++] = v & 0xff;
    return this;
  }

  i8(v) {
    return this.u8(v < 0 ? v + 256 : v);
  }

  u16(v) {
    this.grow(2);
    this.view.setUint16(this.n, v & 0xffff);
    this.n += 2;
    return this;
  }

  i16(v) {
    this.grow(2);
    this.view.setInt16(this.n, v);
    this.n += 2;
    return this;
  }

  u32(v) {
    this.grow(4);
    this.view.setUint32(this.n, v >>> 0);
    this.n += 4;
    return this;
  }

  /** A number up to 2^53. */
  u64(v) {
    this.u32(Math.floor(v / 4294967296));
    return this.u32(v % 4294967296);
  }

  f32(v) {
    this.grow(4);
    this.view.setFloat32(this.n, v);
    this.n += 4;
    return this;
  }

  bytes(u8) {
    this.grow(u8.length);
    this.b.set(u8, this.n);
    this.n += u8.length;
    return this;
  }

  /** Length-prefixed (one byte) text, cut to max bytes at a character boundary. */
  str8(text, max = 255) {
    const data = truncateUtf8(text, Math.min(max, 255));
    this.u8(data.length);
    return this.bytes(data);
  }

  str16(text, max = 65535) {
    const data = truncateUtf8(text, Math.min(max, 65535));
    this.u16(data.length);
    return this.bytes(data);
  }

  done() {
    return this.b.slice(0, this.n);
  }
}

/** The UTF-8 bytes of a text, at most max of them, never ending inside a character. */
export function truncateUtf8(text, max) {
  const data = utf8Encoder.encode(text);
  if (data.length <= max) return data;
  let end = max;
  while (end > 0 && (data[end] & 0xc0) === 0x80) end--;
  return data.subarray(0, end);
}

const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_';

export function base64UrlEncode(bytes) {
  let out = '';
  let i = 0;
  for (; i + 3 <= bytes.length; i += 3) {
    const v = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
    out += B64[(v >> 18) & 63] + B64[(v >> 12) & 63] + B64[(v >> 6) & 63] + B64[v & 63];
  }
  if (bytes.length - i === 1) {
    const v = bytes[i] << 16;
    out += B64[(v >> 18) & 63] + B64[(v >> 12) & 63];
  } else if (bytes.length - i === 2) {
    const v = (bytes[i] << 16) | (bytes[i + 1] << 8);
    out += B64[(v >> 18) & 63] + B64[(v >> 12) & 63] + B64[(v >> 6) & 63];
  }
  return out;
}

/** @returns {Uint8Array|null} null if the text is not base64 (either alphabet, padding optional) */
export function base64UrlDecode(text) {
  const out = [];
  let acc = 0;
  let bits = 0;
  for (const c of text) {
    let v;
    if (c >= 'A' && c <= 'Z') v = c.charCodeAt(0) - 65;
    else if (c >= 'a' && c <= 'z') v = c.charCodeAt(0) - 97 + 26;
    else if (c >= '0' && c <= '9') v = c.charCodeAt(0) - 48 + 52;
    else if (c === '-' || c === '+') v = 62;
    else if (c === '_' || c === '/') v = 63;
    else if (c === '=') break;
    else return null;
    acc = ((acc << 6) | v) & 0xffffff;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push((acc >> bits) & 0xff);
    }
  }
  return bits >= 6 ? null : new Uint8Array(out);
}

export function hex(bytes, separator = '') {
  return Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join(separator);
}

export function concat(...parts) {
  let size = 0;
  for (const p of parts) size += p.length;
  const out = new Uint8Array(size);
  let at = 0;
  for (const p of parts) {
    out.set(p, at);
    at += p.length;
  }
  return out;
}

export function bytesEqual(a, b) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}
