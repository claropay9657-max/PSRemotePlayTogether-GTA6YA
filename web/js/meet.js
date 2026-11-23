// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// How the page and the host find each other: sealed notes at the meeting point
// (coplay/include/coplay/web.h and meet.h). The meeting point only passes the
// notes on. It cannot read them, and nothing of a session goes through it.

import { Reader, Writer, base64UrlDecode, base64UrlEncode, hex } from './bytes.js';
import { PROTOCOL_VERSION } from './proto.js';

export const DEFAULT_MEET_URL = 'https://gamernova.net/api/psrpt_meet.php';

const NOTE_MAGIC = 0x57;
const NOTE_VERSION = 1;
const NOTE_OFFER = 1;
const NOTE_ANSWERS = 2;
const MAX_CANDIDATES = 8;
const MAX_NOTE_TEXT = 1024;

export const CAND_HOST = 1;
export const CAND_REFLEXIVE = 2;

const encoder = new TextEncoder();

async function hkdf(secret, info) {
  const key = await crypto.subtle.importKey('raw', secret, 'HKDF', false, ['deriveBits']);
  const bits = await crypto.subtle.deriveBits(
    { name: 'HKDF', hash: 'SHA-256', salt: new Uint8Array(32), info: encoder.encode(info) }, key, 256);
  return new Uint8Array(bits);
}

/**
 * What an invite's key gives: the name of the session at the meeting point
 * (which cannot be turned back into the key) and the key that seals the notes.
 * @param {Uint8Array} secret 16 bytes
 */
export async function deriveSession(secret) {
  const room = base64UrlEncode((await hkdf(secret, 'psrpt coplay meet room')).subarray(0, 16));
  const noteKey = await crypto.subtle.importKey('raw', await hkdf(secret, 'psrpt coplay web note'),
    'AES-GCM', false, ['encrypt', 'decrypt']);
  return { room, noteKey };
}

async function seal(noteKey, body) {
  const nonce = crypto.getRandomValues(new Uint8Array(12));
  const sealed = new Uint8Array(await crypto.subtle.encrypt({ name: 'AES-GCM', iv: nonce }, noteKey, body));
  const out = new Uint8Array(12 + sealed.length);
  out.set(nonce);
  out.set(sealed, 12);
  return base64UrlEncode(out);
}

async function open(noteKey, text) {
  if (!text || text.length > MAX_NOTE_TEXT) return null;
  const raw = base64UrlDecode(text);
  if (!raw || raw.length < 12 + 16 + 3) return null;
  try {
    return new Uint8Array(await crypto.subtle.decrypt({ name: 'AES-GCM', iv: raw.subarray(0, 12) }, noteKey, raw.subarray(12)));
  } catch (e) {
    return null; // somebody else's note, or not one for a browser
  }
}

function writeSide(w, side) {
  w.str8(side.ufrag);
  w.str8(side.pwd);
  const list = side.candidates.slice(0, MAX_CANDIDATES);
  w.u8(list.length);
  for (const c of list) {
    w.u8(c.type);
    w.u8(c.address.length);
    w.bytes(c.address);
    w.u16(c.port);
  }
}

function readSide(r) {
  const side = { ufrag: r.str8(), pwd: r.str8(), candidates: [] };
  const count = r.u8();
  if (!r.ok || count > MAX_CANDIDATES) return null;
  for (let i = 0; i < count; i++) {
    const type = r.u8();
    const size = r.u8();
    if (!r.ok || (size !== 4 && size !== 16)) return null;
    const address = r.bytes(size).slice();
    const port = r.u16();
    if (!r.ok) return null;
    if (port && (type === CAND_HOST || type === CAND_REFLEXIVE)) side.candidates.push({ type, address, port });
  }
  return side;
}

/**
 * The note a browser leaves: how its side of the connection can be reached.
 * @param {{peerId: Uint8Array, fingerprint: Uint8Array, mid: string, side: object}} offer
 */
export async function sealOffer(noteKey, offer) {
  const w = new Writer(256);
  w.u8(NOTE_MAGIC).u8(NOTE_VERSION).u8(NOTE_OFFER).u8(PROTOCOL_VERSION);
  w.bytes(offer.peerId);
  w.bytes(offer.fingerprint);
  w.str8(offer.mid);
  writeSide(w, offer.side);
  return seal(noteKey, w.done());
}

/**
 * A host's note with its answers to the browsers that are on their way in.
 * @returns {Promise<{protocol: number, fingerprint: Uint8Array, answers: object[]}|null>}
 */
export async function openAnswers(noteKey, text) {
  const body = await open(noteKey, text);
  if (!body) return null;
  const r = new Reader(body);
  if (r.u8() !== NOTE_MAGIC || r.u8() !== NOTE_VERSION || r.u8() !== NOTE_ANSWERS) return null;
  const out = { protocol: r.u8(), fingerprint: r.bytes(32).slice(), answers: [] };
  const count = r.u8();
  if (!r.ok) return null;
  for (let i = 0; i < count; i++) {
    const peerId = hex(r.bytes(8));
    const active = (r.u8() & 1) !== 0;
    const side = r.ok ? readSide(r) : null;
    if (!side) return null;
    out.answers.push({ peerId, active, side });
  }
  return out;
}

/**
 * One visit to the meeting point: leaves the note (an empty one takes it away)
 * and brings back the notes of the hosts.
 * @returns {Promise<{notes: string[], pollMs: number}>}
 */
export async function visit(url, room, peerId, note, signal) {
  const body = `v=1&room=${room}&id=${hex(peerId)}&role=g&note=${note}`;
  const response = await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body,
    cache: 'no-store',
    credentials: 'omit',
    referrerPolicy: 'no-referrer',
    signal,
  });
  if (!response.ok) throw new Error(`the meeting point answered with HTTP ${response.status}`);
  const lines = (await response.text()).split('\n').map((l) => l.trim()).filter((l) => l);
  if (!lines.length || !lines[0].startsWith('psrpt-meet ')) throw new Error('the answer does not come from a meeting point');
  const out = { notes: [], pollMs: 0 };
  for (const line of lines.slice(1)) {
    const space = line.indexOf(' ');
    const word = space < 0 ? line : line.slice(0, space);
    const value = space < 0 ? '' : line.slice(space + 1);
    if (word === 'error') throw new Error('the meeting point says: ' + value.slice(0, 120));
    if (word === 'poll' && /^\d{1,7}$/.test(value)) out.pollMs = parseInt(value, 10);
    else if (word === 'note' && value && value.length <= MAX_NOTE_TEXT && out.notes.length < 32) out.notes.push(value);
  }
  return out;
}

// ------------------------------------------------------------------ addresses

function parseIPv4(text) {
  const m = /^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/.exec(text);
  if (!m) return null;
  const out = new Uint8Array(4);
  for (let i = 0; i < 4; i++) {
    const v = parseInt(m[i + 1], 10);
    if (v > 255) return null;
    out[i] = v;
  }
  return out;
}

function parseIPv6(text) {
  if (!/^[0-9a-fA-F:]+$/.test(text) || text.indexOf(':') < 0) return null;
  const halves = text.split('::');
  if (halves.length > 2) return null;
  const groups = (part) => (part === '' ? [] : part.split(':'));
  const head = groups(halves[0]);
  const tail = halves.length === 2 ? groups(halves[1]) : [];
  const missing = 8 - head.length - tail.length;
  if (halves.length === 1 ? missing !== 0 : missing < 1) return null;
  const all = head.concat(new Array(halves.length === 2 ? missing : 0).fill('0'), tail);
  const out = new Uint8Array(16);
  for (let i = 0; i < 8; i++) {
    if (!/^[0-9a-fA-F]{1,4}$/.test(all[i])) return null;
    const v = parseInt(all[i], 16);
    out[i * 2] = v >> 8;
    out[i * 2 + 1] = v & 0xff;
  }
  return out;
}

/** @returns {Uint8Array|null} 4 or 16 bytes; null for a name (browsers hide addresses in the own network behind one) */
export function parseAddress(text) {
  return parseIPv4(text) || parseIPv6(text);
}

export function addressToString(bytes) {
  if (bytes.length === 4) return Array.from(bytes).join('.');
  const groups = [];
  for (let i = 0; i < 16; i += 2) groups.push(((bytes[i] << 8) | bytes[i + 1]).toString(16));
  return groups.join(':');
}

// ------------------------------------------------------------------ session descriptions

/**
 * What the meeting point has to carry of a browser's own session description.
 * @returns {{mid: string, fingerprint: Uint8Array, side: object}|null}
 */
export function describeOffer(sdp) {
  const lines = sdp.split(/\r?\n/);
  const value = (prefix) => {
    const line = lines.find((l) => l.startsWith(prefix));
    return line ? line.slice(prefix.length).trim() : '';
  };
  const ufrag = value('a=ice-ufrag:');
  const pwd = value('a=ice-pwd:');
  const mid = value('a=mid:') || '0';
  const print = /^sha-256\s+([0-9A-Fa-f:]+)$/.exec(value('a=fingerprint:'));
  if (!ufrag || !pwd || !print) return null;
  const fingerprint = new Uint8Array(print[1].split(':').map((h) => parseInt(h, 16)));
  if (fingerprint.length !== 32) return null;
  const candidates = [];
  for (const line of lines) {
    if (line.startsWith('a=candidate:')) addCandidate(candidates, line.slice(2));
  }
  return { mid, fingerprint, side: { ufrag, pwd, candidates } };
}

/**
 * Adds an address of this browser to the list, if it is one the host can use.
 * @param {string} text "candidate:842163049 1 udp 1677729535 203.0.113.5 54321 typ srflx ..."
 * @returns {boolean} whether the list changed
 */
export function addCandidate(list, text) {
  const parts = text.replace(/^a=/, '').replace(/^candidate:/, '').trim().split(/\s+/);
  if (parts.length < 8 || parts[1] !== '1' || parts[2].toLowerCase() !== 'udp' || parts[6] !== 'typ') return false;
  const type = parts[7] === 'host' ? CAND_HOST : parts[7] === 'srflx' ? CAND_REFLEXIVE : 0;
  const address = parseAddress(parts[4]);
  const port = parseInt(parts[5], 10);
  if (!type || !address || !(port > 0 && port < 65536)) return false;
  if (list.some((c) => c.port === port && c.type === type && addressToString(c.address) === addressToString(address))) {
    return false;
  }
  if (list.length >= MAX_CANDIDATES) return false;
  // The address on the internet first: it is the one a host elsewhere needs
  if (type === CAND_REFLEXIVE) list.unshift({ type, address, port });
  else list.push({ type, address, port });
  return true;
}

export function candidateLine(c, index) {
  const reflexive = c.type === CAND_REFLEXIVE;
  const priority = (reflexive ? 100 : 126) * 16777216 + (65535 - index) * 256 + 255;
  let line = `candidate:${index + 1} 1 udp ${priority} ${addressToString(c.address)} ${c.port} typ ${reflexive ? 'srflx' : 'host'}`;
  if (reflexive) line += c.address.length === 16 ? ' raddr :: rport 0' : ' raddr 0.0.0.0 rport 0';
  return line;
}

/** The session description the host would have sent, put together from its note. */
export function answerSdp(mid, fingerprint, answer) {
  const lines = [
    'v=0',
    `o=- ${Math.floor(Math.random() * 1e15)} 2 IN IP4 127.0.0.1`,
    's=-',
    't=0 0',
    `a=group:BUNDLE ${mid}`,
    'm=application 9 UDP/DTLS/SCTP webrtc-datachannel',
    'c=IN IP4 0.0.0.0',
    `a=mid:${mid}`,
    `a=ice-ufrag:${answer.side.ufrag}`,
    `a=ice-pwd:${answer.side.pwd}`,
    'a=ice-options:trickle',
    `a=fingerprint:sha-256 ${hex(fingerprint, ':').toUpperCase()}`,
    `a=setup:${answer.active ? 'active' : 'passive'}`,
    'a=sctp-port:5000',
    'a=max-message-size:262144',
  ];
  return lines.join('\r\n') + '\r\n';
}

// ------------------------------------------------------------------ the link

/**
 * Reads what stands behind the "#" of the page's address, or what a friend
 * pasted: the whole link, the key alone, "KEY?meet=example.org/meet.php", or a
 * whole invite of the app ("psrpt://203.0.113.7:9410/KEY?meet=...").
 * The host can also say whom the browser may ask for its address on the
 * internet: "&stun=stun.example.org:3478", or "&stun=off" for nobody.
 * @returns {{secret: Uint8Array, meetUrl: string, stun: string}|null}
 */
export function parseInvite(text, pageIsLocal = false) {
  let s = (text || '').trim();
  // The whole link a friend sent ("https://…/#KEY?meet=…"): what stands behind the "#"
  const hash = s.lastIndexOf('#');
  if (hash >= 0) s = s.slice(hash + 1);
  try {
    s = decodeURIComponent(s);
  } catch (e) {
    // taken as it is
  }
  const scheme = s.indexOf('psrpt://');
  if (scheme >= 0) {
    const rest = s.slice(scheme + 8);
    const slash = rest.indexOf('/');
    if (slash < 0) return null;
    s = rest.slice(slash + 1);
  }
  let meet = '';
  let stun = '';
  const query = s.indexOf('?');
  if (query >= 0) {
    for (const part of s.slice(query + 1).split('&')) {
      if (part.startsWith('meet=')) meet = part.slice(5);
      else if (part.startsWith('stun=')) stun = part.slice(5);
    }
    s = s.slice(0, query);
  }
  // A name or address with or without a port, nothing else
  if (stun !== 'off' && !/^[A-Za-z0-9.\-\[\]:]{1,200}$/.test(stun)) stun = '';
  const secret = base64UrlDecode(s);
  if (!secret || secret.length !== 16) return null;

  let meetUrl = DEFAULT_MEET_URL;
  if (meet) {
    if (/^https:\/\//.test(meet)) meetUrl = meet;
    // A meeting point without HTTPS only for somebody trying things out on their own machine
    else if (/^http:\/\//.test(meet)) meetUrl = pageIsLocal ? meet : '';
    else if (/^[A-Za-z0-9._~\/:%\[\]-]+$/.test(meet) && !meet.includes('//')) meetUrl = 'https://' + meet;
    else meetUrl = '';
  }
  return meetUrl ? { secret, meetUrl, stun } : null;
}
