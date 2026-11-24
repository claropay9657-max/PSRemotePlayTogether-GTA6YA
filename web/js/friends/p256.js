// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The public key of a P-256 private key, for restoring a backup file. The app's backup carries
// an identity's private keys as the scalar d alone (gui/src/friends.cpp); WebCrypto can import
// such a key only with its point. Most browsers work the point out themselves from a PKCS #8
// key without one; where a browser does not, it is computed here: d·G, plain BigInt arithmetic.
// It runs once per restore, and the result is checked against the identity's fingerprint.

import { base64UrlDecode, concat, hex } from '../bytes.js';

const P = 0xffffffff00000001000000000000000000000000ffffffffffffffffffffffffn;
const N = 0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551n;
const A = P - 3n;
const G = [
  0x6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296n,
  0x4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5n,
];
// PKCS #8 of a P-256 key (RFC 5915 inside) up to d, without the optional public key
const PKCS8_HEAD = new Uint8Array([
  0x30, 0x41, 0x02, 0x01, 0x00, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a,
  0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x04, 0x27, 0x30, 0x25, 0x02, 0x01, 0x01, 0x04, 0x20,
]);

function mod(a) {
  const r = a % P;
  return r < 0n ? r + P : r;
}

function inverse(a) {
  let [t, nextT, r, nextR] = [0n, 1n, P, mod(a)];
  while (nextR !== 0n) {
    const q = r / nextR;
    [t, nextT] = [nextT, t - q * nextT];
    [r, nextR] = [nextR, r - q * nextR];
  }
  return mod(t);
}

function add(p, q) {
  if (!p) return q;
  if (!q) return p;
  let slope;
  if (p[0] === q[0]) {
    if (mod(p[1] + q[1]) === 0n) return null;
    slope = mod((3n * p[0] * p[0] + A) * inverse(2n * p[1]));
  } else {
    slope = mod((q[1] - p[1]) * inverse(q[0] - p[0]));
  }
  const x = mod(slope * slope - p[0] - q[0]);
  return [x, mod(slope * (p[0] - x) - p[1])];
}

function bytes32(v) {
  return Uint8Array.from(v.toString(16).padStart(64, '0').match(/../g), (b) => parseInt(b, 16));
}

/** d·G as 0x04 || x || y, computed here; null if d is no private key. */
export function p256Public(d) {
  if (!(d instanceof Uint8Array) || d.length !== 32) return null;
  let k = BigInt('0x' + hex(d));
  if (k === 0n || k >= N) return null;
  let result = null;
  let point = G;
  while (k > 0n) {
    if (k & 1n) result = add(result, point);
    point = add(point, point);
    k >>= 1n;
  }
  return result ? concat(new Uint8Array([4]), bytes32(result[0]), bytes32(result[1])) : null;
}

/** The public key of d: from WebCrypto where it can, computed here where not. */
export async function publicFromPrivate(d) {
  if (!(d instanceof Uint8Array) || d.length !== 32) return null;
  try {
    const key = await globalThis.crypto.subtle.importKey('pkcs8', concat(PKCS8_HEAD, d), { name: 'ECDH', namedCurve: 'P-256' },
      true, ['deriveBits']);
    const jwk = await globalThis.crypto.subtle.exportKey('jwk', key);
    const x = base64UrlDecode(jwk.x || '');
    const y = base64UrlDecode(jwk.y || '');
    if (x && y && x.length === 32 && y.length === 32) return concat(new Uint8Array([4]), x, y);
  } catch (e) {
    // This browser wants the point in the key: it is computed below
  }
  return p256Public(d);
}
