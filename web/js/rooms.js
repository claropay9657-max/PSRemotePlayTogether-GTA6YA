// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Public rooms: sessions whose hosts want them to be found
// (server/rooms/psrpt_rooms.php). The list only says how to find a room; joining
// one works like a link from a friend, straight to the host. A listed invite names
// no address, and a cover can only be a picture on Sony's image server.

import { PROTOCOL_VERSION } from './proto.js';

export const DEFAULT_ROOMS_URL = 'https://gamernova.net/api/psrpt_rooms.php';
// As the list itself takes it: nothing that could end the address inside CSS or HTML
const COVER = /^https:\/\/image\.api\.playstation\.com\/[A-Za-z0-9._~\/%-]{1,300}$/;

/** The languages a room can say it speaks, as the app offers them: [code, name in that language]. */
export const LANGUAGES = [
  ['en', 'English'], ['de', 'Deutsch'], ['fr', 'Français'], ['es', 'Español'], ['it', 'Italiano'],
  ['pt', 'Português'], ['nl', 'Nederlands'], ['pl', 'Polski'], ['tr', 'Türkçe'], ['ru', 'Русский'],
  ['uk', 'Українська'], ['cs', 'Čeština'], ['sv', 'Svenska'], ['da', 'Dansk'], ['no', 'Norsk'],
  ['fi', 'Suomi'], ['hu', 'Magyar'], ['ro', 'Română'], ['el', 'Ελληνικά'], ['ar', 'العربية'],
  ['hi', 'हिन्दी'], ['ja', '日本語'], ['ko', '한국어'], ['zh', '中文'],
];

export function languageName(code) {
  const found = LANGUAGES.find((l) => l[0] === code);
  return found ? found[1] : code ? code.toUpperCase() : 'Any language';
}

const text = (value, most) => (typeof value === 'string' ? value.slice(0, most) : '');

/**
 * Reads the list.
 * @returns {Promise<{rooms: object[], poll: number}>}
 */
export async function fetchRooms(url) {
  const address = new URL(url);
  address.searchParams.set('v', '1');
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 10000);
  let answer;
  try {
    const reply = await fetch(address, { signal: controller.signal, credentials: 'omit', cache: 'no-store' });
    answer = await reply.json();
  } finally {
    clearTimeout(timer);
  }
  if (!answer || !answer.ok || !Array.isArray(answer.rooms)) {
    throw new Error(answer && answer.error ? answer.error : 'The list of public rooms gave an answer this page does not understand.');
  }
  const rooms = [];
  const ROOM_ID = /^[0-9a-f]{16}$/;
  for (const r of answer.rooms) {
    const invite = text(r.invite, 300);
    if (!invite.startsWith('psrpt://')) continue;
    const players = Number(r.players) || 0;
    const max = Number(r.max) || 0;
    const cover = text(r.cover, 400);
    const id = text(r.id, 16);
    // The game as it is now: a picture the host sent to the list, served by the list itself
    const taken = Number(r.preview) || 0;
    let picture = '';
    if (taken > 0 && ROOM_ID.test(id)) {
      const pictureAddress = new URL(url);
      pictureAddress.searchParams.set('v', '1');
      pictureAddress.searchParams.set('preview', id);
      pictureAddress.searchParams.set('t', String(Math.floor(taken)));
      picture = pictureAddress.toString();
    }
    // Who plays: an initial for each player, the host first, and their pictures where they have one
    const initials = Array.from(text(r.initials, 40)).slice(0, 9).map((c) => (/^[\p{L}\p{N}]$/u.test(c) ? c : '?'));
    const faces = initials.map((_, i) => {
      const name = Array.isArray(r.faces) ? text(r.faces[i], 16) : '';
      if (!/^[0-9a-f]{16}$/.test(name) || !ROOM_ID.test(id)) return '';
      const faceAddress = new URL(url);
      faceAddress.searchParams.set('v', '1');
      faceAddress.searchParams.set('face', id);
      faceAddress.searchParams.set('h', name);
      return faceAddress.toString();
    });
    rooms.push({
      id,
      title: text(r.title, 60),
      lang: text(r.lang, 2),
      game: text(r.game, 80),
      host: text(r.host, 32),
      players,
      max,
      full: players >= max,
      policy: ['ask', 'watch', 'open'].includes(r.policy) ? r.policy : 'ask',
      control: ['all', 'turns', 'host'].includes(r.control) ? r.control : 'all',
      web: !!r.web,
      invite,
      compatible: Number(r.proto) === PROTOCOL_VERSION,
      // Sony's image server makes a small one
      cover: COVER.test(cover) ? `${cover}?w=192` : '',
      picture,
      initials,
      faces,
      // Live on Twitch: a channel name only, the page builds the link itself
      twitch: /^[a-z0-9_]{3,25}$/.test(text(r.twitch, 25)) ? text(r.twitch, 25) : '',
      since: Number(r.since) || 0,
    });
  }
  return { rooms, poll: Math.min(60000, Math.max(5000, Number(answer.poll) || 15000)) };
}

/**
 * The rooms a search shows: every word in the title, the game, the host or the
 * language, in the chosen language, the ones a browser can join first.
 */
export function filterRooms(rooms, { search = '', lang = '', hideFull = false, openOnly = false, twitchOnly = false } = {}) {
  const words = search.toLowerCase().split(/\s+/).filter((w) => w);
  return rooms
    .filter((r) => {
      if (lang && r.lang && r.lang !== lang) return false;
      if (hideFull && r.full) return false;
      if (openOnly && r.policy === 'ask') return false;
      if (twitchOnly && !r.twitch) return false;
      const hay = `${r.title} ${r.game} ${r.host} ${languageName(r.lang)}`.toLowerCase();
      return words.every((w) => hay.includes(w));
    })
    .sort((a, b) => {
      const can = (r) => (r.web && r.compatible && !r.full ? 0 : 1);
      return can(a) - can(b) || b.players - a.players || b.since - a.since;
    });
}
