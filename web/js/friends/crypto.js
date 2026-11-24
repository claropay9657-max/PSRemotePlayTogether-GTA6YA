// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The cryptography of friends, chats and status (docs/friends.md, docs/friends-protocol.md):
// P-256 keys, Signal's X3DH and Double Ratchet, Sender Keys for groups, the card, the status
// note, linking a device and the backup file. Byte for byte what coplay/src/friends_crypto.cpp
// does; both are held to test/web/friends-vectors.json.
//
// Only WebCrypto. Nothing here talks to a network or keeps anything: the page does that with
// what comes out. Whatever needs new key pairs or random bytes makes them itself unless it is
// given a source (an async function), so that the test vectors can be made again with fixed ones.
//
// A key pair is { pub, ecdh } or { pub, ecdsa }: the public point as 65 bytes and the private
// half as a CryptoKey for the one thing the key is for. The page's own keys are made so that
// they cannot be exported: a script that got into the page could use them while it runs, but
// not carry them off. Only the identity's two key pairs (and a group's owner key) are made
// exportable, because a backup and linking a device have to carry them, as d || pub (97 bytes).
//
// What a conversation keeps (the ratchet, a sender chain) is a plain object with bytes, numbers,
// Maps and CryptoKeys in it, which IndexedDB stores as it is. No classes: what comes back out
// of IndexedDB has lost its prototype.

import { Reader, Writer, base64UrlDecode, base64UrlEncode, bytesEqual, concat, hex } from '../bytes.js';

export const PROTOCOL_VERSION = 1;
export const MAX_DEVICES = 10;
export const MAX_SKIPPED_KEYS = 1000;
export const SKIPPED_KEY_MAX_AGE_MS = 30 * 24 * 3600 * 1000;
export const MAX_GROUP_SKIPPED_KEYS = 2000;
export const BACKUP_ITERATIONS = 600000;
export const HEADER_SIZE = 65 + 4 + 4;

const BACKUP_HEAD = 'PSRPT-BACKUP 1\n';
const ECDH = { name: 'ECDH', namedCurve: 'P-256' };
const ECDSA = { name: 'ECDSA', namedCurve: 'P-256' };
const ECDSA_SHA256 = { name: 'ECDSA', hash: 'SHA-256' };

const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8', { fatal: false });

// Looked up when used: a page that is not served over HTTPS has no crypto.subtle, and should
// still load the rest
function subtle() {
  return globalThis.crypto.subtle;
}

/** A label or text as its bytes; bytes stay as they are. */
function utf8(text) {
  return typeof text === 'string' ? encoder.encode(text) : text;
}

function u32(v) {
  return new Writer(4).u32(v).done();
}

function u32At(bytes, at) {
  return new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength).getUint32(at);
}

/** Byte order, as C++ compares arrays: negative, 0 or positive. */
export function compareBytes(a, b) {
  const n = Math.min(a.length, b.length);
  for (let i = 0; i < n; i++) if (a[i] !== b[i]) return a[i] - b[i];
  return a.length - b.length;
}

export function randomBytes(n) {
  return globalThis.crypto.getRandomValues(new Uint8Array(n));
}

async function random(rnd, n) {
  const out = rnd ? await rnd(n) : randomBytes(n);
  if (!(out instanceof Uint8Array) || out.length !== n) throw new Error(`the random source gave no ${n} bytes`);
  return out;
}

async function newPair(keys, usage, extractable) {
  const out = keys ? await keys(usage) : await generateKeyPair({ usage, extractable });
  if (!out || !(usage === 'ecdsa' ? out.ecdsa : out.ecdh)) throw new Error(`the key source gave no ${usage} key pair`);
  return out;
}

// ------------------------------------------------------------------ primitives

/**
 * A new P-256 key pair for key agreement ('ecdh') or for signing ('ecdsa').
 * @returns {Promise<{pub: Uint8Array, ecdh?: CryptoKey, ecdsa?: CryptoKey}>}
 */
export async function generateKeyPair({ usage = 'ecdh', extractable = false } = {}) {
  const signs = usage === 'ecdsa';
  const pair = await subtle().generateKey(signs ? ECDSA : ECDH, extractable, signs ? ['sign', 'verify'] : ['deriveBits']);
  const pub = new Uint8Array(await subtle().exportKey('raw', pair.publicKey));
  return signs ? { pub, ecdsa: pair.privateKey } : { pub, ecdh: pair.privateKey };
}

/**
 * A key pair from d || pub (97 bytes), as backups and linking carry it. A browser cannot work
 * out the point from d, so it comes along; WebCrypto takes both as JWK. Identity keys come back
 * with extractable: true, so that the next backup can carry them again.
 * @param {'ecdh'|'ecdsa'|'both'} usage
 * @returns {Promise<{pub: Uint8Array, ecdh?: CryptoKey, ecdsa?: CryptoKey}|null>} null if it is no key
 */
export async function importKeyPair(data, { usage = 'ecdh', extractable = false } = {}) {
  if (!(data instanceof Uint8Array) || data.length !== 97 || data[32] !== 0x04) return null;
  const pub = data.slice(32);
  const jwk = {
    kty: 'EC', crv: 'P-256', d: base64UrlEncode(data.subarray(0, 32)),
    x: base64UrlEncode(pub.subarray(1, 33)), y: base64UrlEncode(pub.subarray(33)),
  };
  const out = { pub };
  try {
    if (usage !== 'ecdsa') out.ecdh = await subtle().importKey('jwk', jwk, ECDH, extractable, ['deriveBits']);
    if (usage !== 'ecdh') out.ecdsa = await subtle().importKey('jwk', jwk, ECDSA, extractable, ['sign']);
  } catch (e) {
    return null;
  }
  return out;
}

/** d || pub (97 bytes) of a key pair that was made or imported exportable. */
export async function exportPrivate(pair) {
  const jwk = await subtle().exportKey('jwk', pair.ecdh || pair.ecdsa);
  const d = base64UrlDecode(jwk.d || '');
  if (!d || d.length > 32) throw new Error('no private key came out');
  const out = new Uint8Array(97);
  out.set(d, 32 - d.length);
  out.set(pair.pub, 32);
  return out;
}

/** The CryptoKey of a public key, or null if it is no point on P-256. */
async function publicKey(pub, algorithm, usages) {
  if (!(pub instanceof Uint8Array) || pub.length !== 65 || pub[0] !== 0x04) return null;
  try {
    return await subtle().importKey('raw', pub, algorithm, true, usages);
  } catch (e) {
    return null;
  }
}

/** Whether a public key is a point on P-256. */
export async function validPublic(pub) {
  return (await publicKey(pub, ECDH, [])) !== null;
}

/** ECDH: the x coordinate of d·pub, 32 bytes; null if the other key is no point. */
export async function dh(own, other) {
  if (!own || !own.ecdh) throw new Error('no key pair for key agreement');
  const key = await publicKey(other, ECDH, []);
  if (!key) return null;
  try {
    return new Uint8Array(await subtle().deriveBits({ name: 'ECDH', public: key }, own.ecdh, 256));
  } catch (e) {
    return null;
  }
}

/** ECDSA with SHA-256, as r || s (64 bytes). */
export async function sign(pair, message) {
  if (!pair || !pair.ecdsa) throw new Error('no key pair for signing');
  return new Uint8Array(await subtle().sign(ECDSA_SHA256, pair.ecdsa, message));
}

export async function verify(pub, message, sig) {
  if (!(sig instanceof Uint8Array) || sig.length !== 64) return false;
  const key = await publicKey(pub, ECDSA, ['verify']);
  if (!key) return false;
  try {
    return await subtle().verify(ECDSA_SHA256, key, sig, message);
  } catch (e) {
    return false;
  }
}

export async function sha256(data) {
  return new Uint8Array(await subtle().digest('SHA-256', data));
}

export async function hmac(key, data) {
  // HMAC fills a short key up with zeros to 64 bytes, so an empty key is 64 zeros; WebCrypto
  // refuses an empty one
  const k = await subtle().importKey('raw', key.length ? key : new Uint8Array(64), { name: 'HMAC', hash: 'SHA-256' },
    false, ['sign']);
  return new Uint8Array(await subtle().sign('HMAC', k, utf8(data)));
}

/** HKDF-SHA256, size bytes out; an empty salt counts as 32 zero bytes. */
export async function hkdf(salt, ikm, info, size) {
  const key = await subtle().importKey('raw', ikm, 'HKDF', false, ['deriveBits']);
  const bits = await subtle().deriveBits(
    { name: 'HKDF', hash: 'SHA-256', salt: salt && salt.length ? salt : new Uint8Array(32), info: utf8(info) }, key, size * 8);
  return new Uint8Array(bits);
}

function aesKey(key, use) {
  if (key.length !== 32) throw new Error('AES-256 takes a 32-byte key');
  return subtle().importKey('raw', key, 'AES-GCM', false, [use]);
}

/** AES-256-GCM: the sealed text with the 16 tag bytes at the end. */
export async function seal(key, nonce, ad, plain) {
  const sealed = await subtle().encrypt({ name: 'AES-GCM', iv: nonce, additionalData: utf8(ad), tagLength: 128 },
    await aesKey(key, 'encrypt'), utf8(plain));
  return new Uint8Array(sealed);
}

/** @returns {Promise<Uint8Array|null>} null if it does not open */
export async function open(key, nonce, ad, sealed) {
  if (sealed.length < 16) return null;
  try {
    return new Uint8Array(await subtle().decrypt({ name: 'AES-GCM', iv: nonce, additionalData: utf8(ad), tagLength: 128 },
      await aesKey(key, 'decrypt'), sealed));
  } catch (e) {
    return null;
  }
}

/** PBKDF2-SHA256, 32 bytes. */
export async function pbkdf2(passphrase, salt, iterations) {
  const key = await subtle().importKey('raw', utf8(passphrase), 'PBKDF2', false, ['deriveBits']);
  return new Uint8Array(await subtle().deriveBits({ name: 'PBKDF2', hash: 'SHA-256', salt, iterations }, key, 256));
}

// ------------------------------------------------------------------ names

async function name16(...parts) {
  return (await sha256(concat(...parts.map(utf8)))).slice(0, 16);
}

/** FP = H("psrpt id" || IKs || IKd)[0..16] */
export function fingerprint(iks, ikd) {
  return name16('psrpt id', iks, ikd);
}

/** RB = H("psrpt requests" || LK)[0..16] */
export function requestBox(linkKey) {
  return name16('psrpt requests', linkKey);
}

/** IB = H("psrpt inbox" || FP || device)[0..16] */
export function inboxName(fp, device) {
  return name16('psrpt inbox', fp, device);
}

/** GB = H("psrpt group" || group_id)[0..16] */
export function groupBox(groupId) {
  return name16('psrpt group', groupId);
}

/** LB = H("psrpt link" || L)[0..16] */
export function linkBox(linkSecret) {
  return name16('psrpt link', linkSecret);
}

/**
 * SB = H("psrpt status box" || FK_epoch || u32 utc_day)[0..16]: where a status note is kept on a
 * day (unix ms / 86 400 000); only friends, who have the friends key, can work it out.
 */
export function statusBox(epochKey, utcDay) {
  return name16('psrpt status box', epochKey, u32(utcDay));
}

/** Five groups of five digits, the same for both friends: "12345 67890 ..." */
export async function safetyCode(a, b) {
  const aFirst = compareBytes(a, b) < 0;
  const h = await sha256(concat(utf8('psrpt safety'), aFirst ? a : b, aFirst ? b : a));
  const groups = [];
  for (let i = 0; i < 5; i++) groups.push(String(u32At(h, 4 * i) % 100000).padStart(5, '0'));
  return groups.join(' ');
}

// ------------------------------------------------------------------ identity and card

/**
 * Fills ikdSig and fp of an identity whose two key pairs are there.
 * @param {{iks: object, ikd: object}} id
 */
export async function completeIdentity(id) {
  id.ikdSig = await sign(id.iks, concat(utf8('psrpt ikd'), id.ikd.pub));
  id.fp = await fingerprint(id.iks.pub, id.ikd.pub);
  return id;
}

/**
 * A new identity: { iks, ikd, ikdSig, fp }. Its key pairs can be exported, because a backup and
 * a newly linked device need them.
 */
export async function makeIdentity(keys) {
  const iks = await newPair(keys, 'ecdsa', true);
  const ikd = await newPair(keys, 'ecdh', true);
  return completeIdentity({ iks, ikd });
}

function spkMessage(fp, device, spkId, spk) {
  return concat(utf8('psrpt spk'), fp, device, u32(spkId), spk);
}

/** spk_sig for a device's signed prekey. */
export function signPrekey(id, device, spkId, spk) {
  return sign(id.iks, spkMessage(id.fp, device, spkId, spk));
}

/** Everything of a card that card_sig covers. */
function cardBody(card) {
  const w = new Writer(512);
  w.u8(PROTOCOL_VERSION).bytes(card.iks).bytes(card.ikd).bytes(card.ikdSig).u32(card.seq).u8(card.devices.length);
  for (const d of card.devices) w.bytes(d.id).u8(d.kind).u32(d.spkId).bytes(d.spk).bytes(d.spkSig);
  const profile = card.profile || new Uint8Array(0);
  w.u16(profile.length).bytes(profile);
  return w.done();
}

/**
 * The card as bytes, signed: iks, ikd, ikdSig, fp and cardSig of the card are filled in.
 * @param {{seq: number, devices: {id, kind, spkId, spk, spkSig}[], profile: Uint8Array}} card
 * @returns {Promise<Uint8Array|null>} null for no devices, too many, or a profile too big
 */
export async function encodeCard(id, card) {
  if (!card.devices.length || card.devices.length > MAX_DEVICES || (card.profile && card.profile.length > 65535)) return null;
  if (card.devices.some((d) => d.kind !== 1 && d.kind !== 2)) return null;
  card.iks = id.iks.pub;
  card.ikd = id.ikd.pub;
  card.ikdSig = id.ikdSig;
  card.fp = id.fp;
  const body = cardBody(card);
  card.cardSig = await sign(id.iks, concat(utf8('psrpt card'), body));
  return concat(body, card.cardSig);
}

/**
 * Reads a card and checks every signature in it; fp is worked out.
 * @returns {Promise<object|null>} null if it is no card, or any signature in it is wrong
 */
export async function decodeCard(data) {
  const r = new Reader(data);
  if (r.u8() !== PROTOCOL_VERSION) return null;
  const c = { iks: r.bytes(65).slice(), ikd: r.bytes(65).slice(), ikdSig: r.bytes(64).slice(), seq: r.u32(), devices: [] };
  const count = r.u8();
  if (!r.ok || count === 0 || count > MAX_DEVICES) return null;
  for (let i = 0; i < count; i++) {
    const d = { id: r.bytes(8).slice(), kind: r.u8(), spkId: r.u32(), spk: r.bytes(65).slice(), spkSig: r.bytes(64).slice() };
    if (!r.ok || (d.kind !== 1 && d.kind !== 2)) return null;
    c.devices.push(d);
  }
  c.profile = r.bytes(r.u16()).slice();
  c.cardSig = r.bytes(64).slice();
  if (!r.ok || r.remaining !== 0) return null;
  // Every signature: the identity's own, the card's, each prekey's
  if (!(await verify(c.iks, concat(utf8('psrpt ikd'), c.ikd), c.ikdSig)) || !(await validPublic(c.ikd))) return null;
  c.fp = await fingerprint(c.iks, c.ikd);
  if (!(await verify(c.iks, concat(utf8('psrpt card'), data.subarray(0, data.length - 64)), c.cardSig))) return null;
  for (const d of c.devices) {
    if (!(await validPublic(d.spk)) || !(await verify(c.iks, spkMessage(c.fp, d.id, d.spkId, d.spk), d.spkSig))) return null;
  }
  return c;
}

async function profileKey(linkKey) {
  return hkdf(null, linkKey, 'psrpt profile', 32);
}

/** The sealed profile for a card: nonce || Seal(HKDF(-, LK, "psrpt profile"), nonce, FP, json). */
export async function sealProfile(linkKey, fp, json, rnd) {
  const nonce = await random(rnd, 12);
  return concat(nonce, await seal(await profileKey(linkKey), nonce, fp, utf8(json)));
}

/** @returns {Promise<string|null>} the profile's JSON text, null if it does not open */
export async function openProfile(linkKey, fp, sealed) {
  if (sealed.length < 12 + 16) return null;
  const plain = await open(await profileKey(linkKey), sealed.subarray(0, 12), fp, sealed.subarray(12));
  return plain ? decoder.decode(plain) : null;
}

// ------------------------------------------------------------------ X3DH

async function x3dhKey(dhs) {
  return hkdf(null, concat(new Uint8Array(32).fill(0xff), ...dhs), 'psrpt x3dh', 32);
}

/**
 * The one who starts: SK from its identity, a fresh ephemeral key and the other's bundle.
 * @param {{fp, ikd, device, spkId, spk, opkId, opk}} other opkId 0: no one-time prekey
 * @returns {Promise<{sk: Uint8Array, ad: Uint8Array, ek: object}|null>}
 */
export async function x3dhInitiate(own, ownDevice, other, keys) {
  const ek = await newPair(keys, 'ecdh', false);
  const dhs = [await dh(own.ikd, other.spk), await dh(ek, other.ikd), await dh(ek, other.spk)];
  if (other.opkId) dhs.push(await dh(ek, other.opk));
  if (dhs.includes(null)) return null;
  return { sk: await x3dhKey(dhs), ad: concat(own.fp, ownDevice, other.fp, other.device), ek };
}

/**
 * The one who answers: SK from its identity, its prekeys and the first message's keys.
 * @param {object|null} opk the one-time prekey's pair, null if the message named none
 * @returns {Promise<{sk: Uint8Array, ad: Uint8Array}|null>}
 */
export async function x3dhRespond(own, ownDevice, spk, opk, otherFp, otherIkd, otherDevice, otherEk) {
  const dhs = [await dh(spk, otherIkd), await dh(own.ikd, otherEk), await dh(spk, otherEk)];
  if (opk) dhs.push(await dh(opk, otherEk));
  if (dhs.includes(null)) return null;
  return { sk: await x3dhKey(dhs), ad: concat(otherFp, otherDevice, own.fp, ownDevice) };
}

// ------------------------------------------------------------------ the Double Ratchet

/** KDF_CK: the next chain key and the message key. */
async function chainStep(ck) {
  return { mk: await hmac(ck, new Uint8Array([1])), ck: await hmac(ck, new Uint8Array([2])) };
}

/** KDF_RK: the new root key and a chain key. */
async function rootStep(rk, dhOut) {
  const x = await hkdf(rk, dhOut, 'psrpt ratchet', 64);
  return [x.slice(0, 32), x.slice(32)];
}

/** Opens or seals with a message key: key and nonce from HKDF(-, mk, info, 44). */
async function messageKey(mk, info) {
  const x = await hkdf(null, mk, info, 44);
  return { key: x.subarray(0, 32), nonce: x.subarray(32, 44) };
}

/** bytes ratchet.pub (65) || u32 pn || u32 n */
export function encodeHeader(h) {
  return new Writer(HEADER_SIZE).bytes(h.dh).u32(h.pn).u32(h.n).done();
}

/** @returns {{dh: Uint8Array, pn: number, n: number}|null} */
export function decodeHeader(data) {
  if (!data || data.length !== HEADER_SIZE) return null;
  const r = new Reader(data);
  const h = { dh: r.bytes(65).slice(), pn: r.u32(), n: r.u32() };
  return r.ok ? h : null;
}

function emptyRatchet() {
  return { dhs: null, dhr: null, rk: new Uint8Array(32), cks: null, ckr: null, ns: 0, nr: 0, pn: 0, skipped: new Map() };
}

/**
 * The one who starts, after X3DH: towards the other's signed prekey. The ratchet is a plain
 * object: { dhs (key pair), dhr, rk, cks, ckr (bytes or null), ns, nr, pn, skipped (a Map of
 * "hex(dh).n" to { dh, n, mk, at }) }. Root and chain keys are bytes: every step's output passes
 * through the page anyway. The ratchet's own key pairs cannot be exported.
 */
export async function ratchetInitInitiator(sk, otherSpk, keys) {
  const s = emptyRatchet();
  s.dhs = await newPair(keys, 'ecdh', false);
  s.dhr = otherSpk.slice();
  const d = await dh(s.dhs, s.dhr);
  if (!d) return null;
  [s.rk, s.cks] = await rootStep(sk, d);
  return s;
}

/** The one who answers, after X3DH: with its signed prekey's key pair. */
export function ratchetInitResponder(sk, spk) {
  const s = emptyRatchet();
  s.dhs = spk;
  s.rk = sk.slice();
  return s;
}

/**
 * Seals a payload and moves the sending chain on. Save the state before the envelope goes out,
 * so that a crash never makes the device send twice with the same key.
 * @returns {Promise<{header: {dh, pn, n}, sealed: Uint8Array}|null>} null before there is a sending chain
 */
export async function ratchetEncrypt(s, ad, plain) {
  if (!s.cks) return null;
  const { ck, mk } = await chainStep(s.cks);
  const header = { dh: s.dhs.pub.slice(), pn: s.pn, n: s.ns };
  const { key, nonce } = await messageKey(mk, 'psrpt message');
  const sealed = await seal(key, nonce, concat(ad, encodeHeader(header)), utf8(plain));
  s.cks = ck;
  s.ns++;
  return { header, sealed };
}

function skippedId(dhPub, n) {
  return `${hex(dhPub)}.${n}`;
}

/** Keeps the message keys up to `until` of the current receiving chain. */
async function skipKeys(s, until, nowMs) {
  if (!s.ckr || until <= s.nr) return true;
  // A message that claims to be far ahead is refused rather than costing thousands of steps
  if (until - s.nr > MAX_SKIPPED_KEYS) return false;
  while (s.nr < until) {
    const { ck, mk } = await chainStep(s.ckr);
    s.ckr = ck;
    s.skipped.set(skippedId(s.dhr, s.nr), { dh: s.dhr, n: s.nr, mk, at: nowMs });
    s.nr++;
  }
  // The oldest go first when there are too many (among the same age, as the C++ map orders them)
  const extra = s.skipped.size - MAX_SKIPPED_KEYS;
  if (extra > 0) {
    const order = [...s.skipped].sort(([, a], [, b]) => a.at - b.at || compareBytes(a.dh, b.dh) || a.n - b.n);
    for (let i = 0; i < extra; i++) s.skipped.delete(order[i][0]);
  }
  return true;
}

/**
 * Opens a payload. The state changes only if it opens (apart from skipped keys older than 30
 * days, which go either way). Save the state after.
 * @param {{dh, pn, n}} header
 * @returns {Promise<Uint8Array|null>} null if it does not open, was opened before, or skips too much
 */
export async function ratchetDecrypt(s, ad, header, sealed, nowMs, keys) {
  // Keys of messages that are too old go
  for (const [id, e] of s.skipped) {
    if (nowMs > e.at && nowMs - e.at > SKIPPED_KEY_MAX_AGE_MS) s.skipped.delete(id);
  }
  const fullAd = concat(ad, encodeHeader(header));
  // A message that was skipped before
  const id = skippedId(header.dh, header.n);
  const found = s.skipped.get(id);
  if (found) {
    const { key, nonce } = await messageKey(found.mk, 'psrpt message');
    const plain = await open(key, nonce, fullAd, sealed);
    if (!plain) return null;
    s.skipped.delete(id);
    return plain;
  }
  // The rest on a copy: the state changes only if the message opens
  const t = { ...s, skipped: new Map(s.skipped) };
  if (!t.dhr || !bytesEqual(header.dh, t.dhr)) {
    if (!(await skipKeys(t, header.pn, nowMs))) return null;
    // DHRatchet
    t.pn = t.ns;
    t.ns = 0;
    t.nr = 0;
    t.dhr = header.dh.slice();
    let d = await dh(t.dhs, t.dhr);
    if (!d) return null;
    [t.rk, t.ckr] = await rootStep(t.rk, d);
    t.dhs = await newPair(keys, 'ecdh', false);
    d = await dh(t.dhs, t.dhr);
    if (!d) return null;
    [t.rk, t.cks] = await rootStep(t.rk, d);
  }
  if (!(await skipKeys(t, header.n, nowMs))) return null;
  const { ck, mk } = await chainStep(t.ckr);
  t.ckr = ck;
  t.nr++;
  const { key, nonce } = await messageKey(mk, 'psrpt message');
  const plain = await open(key, nonce, fullAd, sealed);
  if (!plain) return null;
  Object.assign(s, t);
  return plain;
}

// ------------------------------------------------------------------ envelopes

/**
 * @param {{kind, fromFp, fromDevice, toDevice, ek, spkId, opkId, header, sealed}} e kind 1 a first message (with ek,
 *   spkId, opkId), 2 a message
 * @returns {Uint8Array|null} null if the sealed payload is too big for its length
 */
export function encodeEnvelope(e) {
  if (e.sealed.length > 65535) return null;
  const w = new Writer(256 + e.sealed.length);
  w.u8(PROTOCOL_VERSION).u8(e.kind).bytes(e.fromFp).bytes(e.fromDevice).bytes(e.toDevice);
  if (e.kind === 1) w.bytes(e.ek).u32(e.spkId).u32(e.opkId);
  w.bytes(encodeHeader(e.header)).u16(e.sealed.length).bytes(e.sealed);
  return w.done();
}

/** @returns {object|null} */
export function decodeEnvelope(data) {
  const r = new Reader(data);
  if (r.u8() !== PROTOCOL_VERSION) return null;
  const e = { kind: r.u8() };
  if (!r.ok || (e.kind !== 1 && e.kind !== 2)) return null;
  e.fromFp = r.bytes(16).slice();
  e.fromDevice = r.bytes(8).slice();
  e.toDevice = r.bytes(8).slice();
  if (e.kind === 1) {
    e.ek = r.bytes(65).slice();
    e.spkId = r.u32();
    e.opkId = r.u32();
  } else {
    e.ek = new Uint8Array(65);
    e.spkId = 0;
    e.opkId = 0;
  }
  e.header = decodeHeader(r.bytes(HEADER_SIZE));
  e.sealed = r.bytes(r.u16()).slice();
  if (!r.ok || !e.header || r.remaining !== 0) return null;
  return e;
}

// ------------------------------------------------------------------ groups (Sender Keys)

/**
 * The own sender key of a device in a group: { chain, iter, ck, sign (key pair), signPub, skipped }.
 * Its signing key cannot be exported: a linked device makes its own.
 */
export async function newSenderChain(chainNumber, keys, rnd) {
  const ck = await random(rnd, 32);
  const signPair = await newPair(keys, 'ecdsa', false);
  return { chain: chainNumber, iter: 0, ck, sign: signPair, signPub: signPair.pub, skipped: new Map() };
}

/** Somebody else's sender key, from their senderkey payload: { chain, iter, ck, signPub, skipped }. */
export function senderChainOf(chainNumber, iter, ck, signPub) {
  return { chain: chainNumber, iter, ck: ck.slice(), signPub: signPub.slice(), skipped: new Map() };
}

function groupAd(m) {
  return concat(m.group, m.fp, m.device, u32(m.chain), u32(m.iter));
}

function groupSigned(m) {
  const w = new Writer(64 + m.sealed.length);
  w.u8(PROTOCOL_VERSION).bytes(m.group).bytes(m.fp).bytes(m.device).u32(m.chain).u32(m.iter);
  return w.u16(m.sealed.length).bytes(m.sealed).done();
}

/** @param {{group, fp, device, chain, iter, sealed, sig}} m */
export function encodeGroupMessage(m) {
  return concat(groupSigned(m), m.sig);
}

/** @returns {object|null} */
export function decodeGroupMessage(data) {
  const r = new Reader(data);
  if (r.u8() !== PROTOCOL_VERSION) return null;
  const m = { group: r.bytes(16).slice(), fp: r.bytes(16).slice(), device: r.bytes(8).slice(), chain: r.u32(), iter: r.u32() };
  m.sealed = r.bytes(r.u16()).slice();
  m.sig = r.bytes(64).slice();
  if (!r.ok || r.remaining !== 0) return null;
  return m;
}

/** Seals with the own chain and moves it on. @returns {Promise<Uint8Array|null>} the group message */
export async function groupSeal(own, group, fp, device, plain) {
  const m = { group, fp, device, chain: own.chain, iter: own.iter };
  const { ck, mk } = await chainStep(own.ck);
  const { key, nonce } = await messageKey(mk, 'psrpt group message');
  m.sealed = await seal(key, nonce, groupAd(m), utf8(plain));
  if (m.sealed.length > 65535) return null;
  m.sig = await sign(own.sign, groupSigned(m));
  own.ck = ck;
  own.iter++;
  return encodeGroupMessage(m);
}

/**
 * Opens with the writer's chain, which moves on (keeping the keys of skipped messages, at most
 * 2000) only if the message opens.
 * @returns {Promise<Uint8Array|null>}
 */
export async function groupOpen(theirs, m) {
  if (m.chain !== theirs.chain || !(await verify(theirs.signPub, groupSigned(m), m.sig))) return null;
  const s = { ...theirs, skipped: new Map(theirs.skipped) };
  let mk = s.skipped.get(m.iter);
  if (mk) {
    s.skipped.delete(m.iter);
  } else {
    if (m.iter < s.iter || m.iter - s.iter > MAX_GROUP_SKIPPED_KEYS) return null;
    while (s.iter < m.iter) {
      const step = await chainStep(s.ck);
      s.ck = step.ck;
      s.skipped.set(s.iter, step.mk);
      s.iter++;
    }
    const step = await chainStep(s.ck);
    s.ck = step.ck;
    mk = step.mk;
    s.iter++;
    // The lowest numbers go first
    if (s.skipped.size > MAX_GROUP_SKIPPED_KEYS) {
      const order = [...s.skipped.keys()].sort((a, b) => a - b);
      for (let i = 0; s.skipped.size > MAX_GROUP_SKIPPED_KEYS; i++) s.skipped.delete(order[i]);
    }
  }
  const { key, nonce } = await messageKey(mk, 'psrpt group message');
  const plain = await open(key, nonce, groupAd(m), m.sealed);
  if (!plain) return null;
  Object.assign(theirs, s);
  return plain;
}

// ------------------------------------------------------------------ status

/** FK_day(d) from the epoch's key: d steps of HMAC(k, "psrpt next day"). */
export async function friendsKeyForDay(epochKey, day) {
  let k = epochKey.slice();
  for (let i = 0; i < day; i++) k = await hmac(k, 'psrpt next day');
  return k;
}

/** u32 epoch || u32 day || nonce || Seal(FK_day, nonce, FP, json) */
export async function sealStatus(epochKey, epoch, day, fp, json, rnd) {
  const nonce = await random(rnd, 12);
  const sealed = await seal(await friendsKeyForDay(epochKey, day), nonce, fp, utf8(json));
  return concat(u32(epoch), u32(day), nonce, sealed);
}

/**
 * Opens a status note with the epoch key the friend gave.
 * @returns {Promise<{json: string, day: number}|null>}
 */
export async function openStatus(epochKey, epoch, fp, note) {
  if (note.length < 8 + 12 + 16 || u32At(note, 0) !== epoch) return null;
  const day = u32At(note, 4);
  // A note from more than a year after the epoch is no note of ours
  if (day > 366) return null;
  const plain = await open(await friendsKeyForDay(epochKey, day), note.subarray(8, 20), fp, note.subarray(20));
  return plain ? { json: decoder.decode(plain), day } : null;
}

// ------------------------------------------------------------------ linking a device, backups

/** K = HKDF(L, DH(e_self, E_other), "psrpt link key", 32) */
export function linkKey(linkSecret, dhOut) {
  return hkdf(linkSecret, dhOut, 'psrpt link key', 32);
}

/** The six digits both screens show: "123 456" */
export async function linkCode(key) {
  const code = u32At(await sha256(concat(utf8('psrpt link code'), key)), 0) % 1000000;
  return `${String(Math.floor(code / 1000)).padStart(3, '0')} ${String(code % 1000).padStart(3, '0')}`;
}

/** "PSRPT-BACKUP 1" and the sealed bundle, as text. */
export async function sealBackup(passphrase, json, iterations = BACKUP_ITERATIONS, rnd) {
  const salt = await random(rnd, 16);
  const nonce = await random(rnd, 12);
  // The same passphrase typed on another system may come in another Unicode form
  const key = await pbkdf2(passphrase.normalize('NFC'), salt, iterations);
  const sealed = await seal(key, nonce, utf8('psrpt backup'), utf8(json));
  return BACKUP_HEAD + base64UrlEncode(concat(salt, u32(iterations), nonce, sealed));
}

/** @returns {Promise<string|null>} the bundle's JSON text; null for a wrong passphrase or no backup */
export async function openBackup(passphrase, text) {
  // The first line, also when an editor saved it with Windows line ends
  const head = /^PSRPT-BACKUP 1\r?\n/.exec(typeof text === 'string' ? text : '');
  if (!head) return null;
  const all = base64UrlDecode(text.slice(head[0].length).replace(/[\n\r ]+$/, ''));
  if (!all || all.length < 16 + 4 + 12 + 16) return null;
  const iterations = u32At(all, 16);
  // Far fewer rounds than ever written would make a weak key; far more would hang the page
  if (iterations < 100000 || iterations > 10000000) return null;
  const key = await pbkdf2(passphrase.normalize('NFC'), all.subarray(0, 16), iterations);
  const plain = await open(key, all.subarray(20, 32), utf8('psrpt backup'), all.subarray(32));
  return plain ? decoder.decode(plain) : null;
}

// ------------------------------------------------------------------ the server's signed requests

/**
 * "psrpt friends 1\n" op "\n" t "\n" and every other field sorted by name (as UTF-8 bytes, like
 * the C++ map): name "=" value "\n"
 * @param {Object<string, string>|Map<string, string>} fields
 */
export function requestMessage(op, t, fields) {
  const entries = [...(fields instanceof Map ? fields : Object.entries(fields))];
  entries.sort(([a], [b]) => compareBytes(encoder.encode(a), encoder.encode(b)));
  let text = `psrpt friends 1\n${op}\n${t}\n`;
  for (const [name, value] of entries) text += `${name}=${value}\n`;
  return encoder.encode(text);
}
