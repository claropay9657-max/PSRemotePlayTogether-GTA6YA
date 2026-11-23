// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Sessions joined before, newest first, as the app keeps them (gui/src/coplayrecent.cpp):
// who hosted it, on what, and when, so that a friend's session can be joined again
// with a click. A host keeps its invite from one session to the next.
//
// Whether a host is sharing right now is asked at the meeting point while the list is
// seen, as a guest that leaves no note (coplay::LookForHost): the host learns nothing
// of it. The invites stay in this browser, like the app keeps them on its computer.

import { base64UrlDecode, base64UrlEncode } from './bytes.js';
import { deriveSession, parseInvite, visit } from './meet.js';

export const MAX_RECENT = 8;
export const RECENT_KEY = 'psrpt.recent';
/** The same numbers as the app's CoPlayRecent::Status. */
export const UNKNOWN = 0;
export const LOOKING = 1;
export const OPEN = 2;
export const CLOSED = 3;

const CONSOLES = ['ps5', 'ps4', 'pc', 'xbox'];
const NAME_MAX = 32;
// A note an app leaves (coplay/protocol.h): a header of 14 bytes, PacketType::MeetNote first
const MEET_NOTE = 0x05;
const NOTE_HEADER = 14;
const NOTE_TAG = 16;

/** The invite's key, which tells the sessions apart; empty for what is no invite. */
export function keyOf(text, pageIsLocal = false) {
  const invite = parseInvite(text, pageIsLocal);
  return invite ? base64UrlEncode(invite.secret) : '';
}

function cut(value, most) {
  return typeof value === 'string' ? value.slice(0, most) : '';
}

/** Only Sony's image server, as for the rooms (rooms.js). */
function cover(value) {
  return typeof value === 'string' && /^https:\/\/image\.api\.playstation\.com\/[^\s"'<>]{1,400}$/.test(value) ? value : '';
}

/** The list as it was stored: what does not read as it should is left out rather than shown half. */
export function readRecent(raw, pageIsLocal = false) {
  let items = [];
  try {
    items = JSON.parse(raw || '[]');
  } catch (e) {
    return [];
  }
  if (!Array.isArray(items)) return [];
  const out = [];
  const seen = new Set();
  for (const item of items) {
    if (!item || typeof item !== 'object') continue;
    const invite = cut(item.invite, 600).trim();
    const key = keyOf(invite, pageIsLocal);
    if (!key || seen.has(key)) continue;
    const console = CONSOLES.includes(item.console) ? item.console : '';
    seen.add(key);
    out.push({
      key,
      invite,
      host: cut(item.host, NAME_MAX),
      console,
      title: cut(item.title, 80),
      game: cut(item.game, 80),
      cover: cover(item.cover),
      joined: Number.isFinite(item.joined) ? item.joined : 0,
    });
    if (out.length === MAX_RECENT) break;
  }
  return out;
}

/** How the list is stored. */
export function writeRecent(list) {
  return JSON.stringify(list.map(({ key, ...entry }) => entry));
}

/**
 * Joined: first in the list from now on. A public room's title, game and cover come with it
 * (room), and stay when the same session is joined again with the invite.
 */
export function addJoined(list, invite, host, console, room, now = Date.now(), pageIsLocal = false) {
  const text = (invite || '').trim();
  const key = keyOf(text, pageIsLocal);
  if (!key) return list;
  const before = list.find((e) => e.key === key);
  const entry = {
    key,
    invite: text,
    host: cut(host, NAME_MAX).trim(),
    console: CONSOLES.includes(console) ? console : '',
    title: room ? cut(room.title, 80) : (before ? before.title : ''),
    game: room ? cut(room.game, 80) : (before ? before.game : ''),
    cover: room ? cover(room.cover) : (before ? before.cover : ''),
    joined: now,
  };
  return [entry, ...list.filter((e) => e.key !== key)].slice(0, MAX_RECENT);
}

export function removeRecent(list, key) {
  return list.filter((e) => e.key !== key);
}

/** "ps5", "ps4", "pc" or "xbox" for what a host's welcome says about its game (proto.js readStreamInfo). */
export function consoleOf(stream) {
  if (!stream) return '';
  if (stream.source === 1) return 'pc';
  if (stream.source === 2) return 'xbox';
  return stream.ps5 ? 'ps5' : 'ps4';
}

/**
 * Whether a note at the meeting point is a host's own one, which the app keeps there for as
 * long as it shares. Sealed in a way a browser cannot open (ChaCha20-Poly1305), but its
 * header says what it is, and only who has the invite knows the room to look in.
 */
export function isHostNote(text) {
  const raw = base64UrlDecode(text);
  return !!raw && raw.length >= NOTE_HEADER + NOTE_TAG && raw[0] === MEET_NOTE;
}

/**
 * One look at the meeting point for the host of this session.
 * @returns {Promise<number>} OPEN, CLOSED, or UNKNOWN when the meeting point did not answer
 */
export async function lookForHost(entry, pageIsLocal = false, signal = undefined, visitFn = visit) {
  const invite = parseInvite(entry.invite, pageIsLocal);
  if (!invite) return UNKNOWN;
  try {
    const { room } = await deriveSession(invite.secret);
    // A new id every time, and an empty note: nothing of ours stays there
    const id = crypto.getRandomValues(new Uint8Array(8));
    const answer = await visitFn(invite.meetUrl, room, id, '', signal);
    return answer.notes.some(isHostNote) ? OPEN : CLOSED;
  } catch (e) {
    return UNKNOWN;
  }
}

/** When a session was joined, as the app says it. */
export function joinedText(ms, now = Date.now()) {
  if (!(ms > 0)) return 'Joined before';
  const minutes = Math.floor((now - ms) / 60000);
  if (minutes < 1) return 'Joined just now';
  if (minutes < 60) return `Joined ${minutes} min ago`;
  const hours = Math.floor(minutes / 60);
  if (hours < 24) return `Joined ${hours} h ago`;
  const days = Math.floor(hours / 24);
  if (days === 1) return 'Joined yesterday';
  if (days < 30) return `Joined ${days} days ago`;
  return `Joined on ${new Date(ms).toLocaleDateString()}`;
}

