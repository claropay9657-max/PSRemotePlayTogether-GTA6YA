// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The page: reads the invite, lets the player join, and puts picture, sound,
// controller, players, webcams and chat together while a session runs. What
// lies on the game picture is the app's, piece by piece: the display (hud.js),
// the stream menu (menu.js), the chat and the options (options.js).

import { Sound } from './audio.js';
import { base64UrlDecode, base64UrlEncode } from './bytes.js';
import { CAMERA_SIZES, Camera, makeAvatar } from './camera.js';
import { appendWithLinks, button, el } from './dom.js';
import { DualSense } from './dualsense.js';
import { Hud } from './hud.js';
import { parseInvite } from './meet.js';
import { StreamMenu } from './menu.js';
import { OverlayOptions } from './options.js';
import { KEYBOARD_MAP, Pad, controllerName } from './pad.js';
import { HUD_KEYS, HUD_PRESETS, PresetSettings } from './presets.js';
import * as P from './proto.js';
import { DEFAULT_ROOMS_URL, LANGUAGES, fetchRooms, filterRooms, languageName } from './rooms.js';
import { JoinError, Session } from './session.js';
import { TouchControls } from './touch.js';
import { VideoPlayer } from './video.js';

const $ = (id) => document.getElementById(id);
const LOCAL = ['localhost', '127.0.0.1', '[::1]'].includes(location.hostname);
const MAC = /Mac|iPhone|iPad/.test(navigator.platform || navigator.userAgent || '');
// Somebody trying things out on their own machine can point the page at a list and a
// meeting point of their own: ?rooms=…&meet=… (only there)
const PARAMS = new URLSearchParams(location.search);
const ROOMS_URL = (LOCAL && PARAMS.get('rooms')) || DEFAULT_ROOMS_URL;
const LOCAL_MEET = LOCAL ? PARAMS.get('meet') || '' : '';
/** What opens the stream menu, as the app names it. */
const MENU_KEY = MAC ? '⌘O' : 'Ctrl+O';
/** The same buttons as in the app, held together. */
const MENU_COMBO = P.BTN.L1 | P.BTN.R1 | P.BTN.L3 | P.BTN.R3;
/** After a lost connection the page keeps trying to get back in for this long. */
const REJOIN_MS = 60000;

const ui = {
  front: $('front'),
  landing: $('card-landing'),
  rooms: $('card-rooms'),
  join: $('card-join'),
  connecting: $('card-connecting'),
  message: $('card-message'),
  stage: $('stage'),
  screen: $('screen'),
  note: $('stage-note'),
  notices: $('notices'),
  banner: $('banner'),
  handle: $('menu-handle'),
  stats: $('stats'),
  touch: $('touch'),
  chat: $('chat'),
  chatList: $('chat-list'),
  chatInput: $('chat-input'),
  emoji: $('emoji'),
};

/** The same :shortcodes: and faces the app turns into emoji. */
const SHORTCODES = {
  ':smile:': '😄', ':joy:': '😂', ':wink:': '😉', ':heart:': '❤️', ':fire:': '🔥', ':thumbsup:': '👍', ':+1:': '👍',
  ':thumbsdown:': '👎', ':clap:': '👏', ':gg:': '🎮', ':skull:': '💀', ':eyes:': '👀', ':cry:': '😢', ':rage:': '😡',
  ':party:': '🥳', ':trophy:': '🏆', ':100:': '💯', ':rocket:': '🚀', ':ok:': '👌', ':wave:': '👋',
  ':thinking:': '🤔', ':sweat:': '😅', ':cool:': '😎', ':pray:': '🙏',
};
const FACES = [
  [/(^|\s):-?\)(?=\s|$)/g, '$1🙂'], [/(^|\s):-?D(?=\s|$)/g, '$1😄'], [/(^|\s);-?\)(?=\s|$)/g, '$1😉'],
  [/(^|\s):-?\((?=\s|$)/g, '$1🙁'], [/(^|\s):-?[pP](?=\s|$)/g, '$1😛'], [/(^|\s)<3(?=\s|$)/g, '$1❤️'],
  [/(^|\s)[xX]D(?=\s|$)/g, '$1😆'],
];
/** The app's emoji, in its groups. */
const EMOJI_GROUPS = [
  ['😀', '😀 😃 😄 😁 😆 😅 😂 🤣 🙂 🙃 😉 😊 😇 🥰 😍 🤩 😘 😋 😛 😜 🤪 🤗 🤭 🤫 🤔 🤐 🤨 😐 😑 😶 😏 😒 🙄 😬 😌 😔 😪 😴 😷 🤒 🥵 🥶 🥴 😵 🤯 🤠 🥳 😎 🤓 🧐 😕 😟 🙁 😮 😯 😲 😳 🥺 😦 😨 😰 😥 😢 😭 😱 😖 😣 😞 😓 😩 😫 🥱 😤 😡 😠 🤬 😈 💀 ☠️ 💩 🤡 👻 👽 🤖'],
  ['👍', '👍 👎 👌 🤌 🤏 ✌️ 🤞 🤟 🤘 🤙 👈 👉 👆 👇 ☝️ 👋 🤚 🖐️ ✋ 👏 🙌 👐 🤲 🙏 ✍️ 💪 🦾 🫡 🤝 👀 🧠 🫶'],
  ['❤️', '❤️ 🧡 💛 💚 💙 💜 🖤 🤍 🤎 💔 ❤️‍🔥 💕 💞 💓 💗 💖 💘 💝 💯 💢 💥 💫 💦 💨 💤 ✨ ⭐ 🌟 🔥 ⚡ 🌈'],
  ['🎮', '🎮 🕹️ 👾 🏆 🥇 🥈 🥉 🏅 🎯 🎲 ♟️ 🧩 🎉 🎊 🎁 🎈 🍕 🍔 🍟 🌮 🍿 🍩 🍪 🍺 🍻 🥤 ☕ 🚗 🏎️ 🚀 ✈️ 🛸 ⚔️ 🛡️ 🏹 🔫 💣 🗡️ 🪓 💰 💎 🔑 🗺️'],
  ['✅', '✅ ❌ ❓ ❗ ‼️ ⁉️ ⚠️ 🚫 ⛔ 🔴 🟠 🟡 🟢 🔵 🟣 ⚫ ⚪ ⬆️ ⬇️ ⬅️ ➡️ 🔄 ⏸️ ▶️ ⏩ ⏪ 🆗 🆒 🆕 🆙 🔝 💬 💭 🗯️ 📢 🔔 🎵 🎶 ⏰ ⌛'],
];
/** One colour per place in the session, so that names are told apart at a glance. */
const SLOT_COLORS = ['#b18cff', '#00f2ff', '#34d399', '#f472b6', '#fbbf24', '#60a5fa', '#fb923c', '#a3e635', '#e879f9'];

let invite = null;
let session = null;
let player = null;
let pad = null;
let touch = null;
let hud = null;
let menu = null;
let options = null;
const hudSettings = new PresetSettings('hud', HUD_KEYS, HUD_PRESETS);
const dualsense = new DualSense();
const sound = new Sound();
const camera = new Camera();
let avatar = null; // {ref, data}
let voiceOn = false;
let micMuted = false;
let deafened = false;
let controllerOnly = false;
let lastState = null;
let feedback = { rumble: 0, trigger: 0, led: 0, player: 0 };
let padTimer = 0;
let slowTimer = 0;
let talkTimer = 0;
let idleTimer = 0;
let wakeLock = null;
let leaving = false;
let reconnecting = '';
let unread = 0;
let comboHeld = false;
let lastChat = null; // the newest message, to tell a run by the same player
let playersKey = ''; // who was drawn last, see showPlayers()
const mySent = new Set();
const blobUrls = new Map(); // picture name -> object URL
const cams = new Map(); // slot -> what is collected of that player's webcam
const camShown = new Map(); // slot -> the pictures on screen, to give their memory back
let statsLast = { at: 0, bytes: 0, shown: 0, fragments: 0, nacks: 0 };
let numbers = { mbps: '0.0', fps: 0, loss: 0 };
const prefs = { volume: 100, voiceVolume: 100, haptics: 1, camSize: 1, mode: 'fit', zoom: 0, stats: false, noStreams: false };

// ------------------------------------------------------------------ small helpers

function show(card) {
  // Another card: from its top, not from where the list of rooms was scrolled to
  if (card.hidden) window.scrollTo(0, 0);
  for (const c of [ui.landing, ui.join, ui.connecting, ui.message]) c.hidden = c !== card;
  ui.front.hidden = false;
  ui.stage.hidden = true;
  // The public rooms go with the card for a link: two ways to find a session
  ui.rooms.hidden = card !== ui.landing;
  if (card === ui.landing) startRooms();
  else stopRooms();
}

// ------------------------------------------------------------------ public rooms

let roomList = [];
let roomTimer = 0;
let roomPoll = 15000;
let roomsLoading = false;
let roomsReadAt = 0; // when the list was last read, for "Updated …"
let roomsDrawn = { list: null, key: '' }; // what the cards show now, see renderRooms()

function startRooms() {
  if (roomTimer) return;
  refreshRooms();
  roomTimer = setInterval(refreshRooms, roomPoll);
}

function stopRooms() {
  clearInterval(roomTimer);
  roomTimer = 0;
}

async function refreshRooms() {
  if (roomsLoading) return;
  roomsLoading = true;
  // Never switched off while it reads, so that the focus stays on it
  const refresh = $('rooms-refresh');
  refresh.classList.add('loading');
  refresh.setAttribute('aria-busy', 'true');
  showRoomsUpdated();
  try {
    const answer = await fetchRooms(ROOMS_URL);
    roomList = answer.rooms;
    roomsReadAt = Date.now();
    $('rooms-problem').hidden = true;
    if (answer.poll !== roomPoll && roomTimer) {
      roomPoll = answer.poll;
      clearInterval(roomTimer);
      roomTimer = setInterval(refreshRooms, roomPoll);
    }
  } catch (e) {
    const problem = $('rooms-problem');
    problem.textContent = `The list of public rooms could not be read${e && e.message ? ': ' + e.message : '.'}`;
    problem.hidden = false;
  } finally {
    roomsLoading = false;
    refresh.classList.remove('loading');
    refresh.removeAttribute('aria-busy');
    showRoomsUpdated();
  }
  renderRooms();
}

/** "Updated …", as over the app's list: when the list was last read. */
function showRoomsUpdated() {
  const minutes = Math.floor((Date.now() - roomsReadAt) / 60000);
  $('rooms-updated').textContent = roomsReadAt ? (minutes < 1 ? 'Updated just now' : `Updated ${minutes} min ago`)
    : roomsLoading ? 'Loading…' : '';
}

const CONTROL = { all: 'Everybody plays together', turns: 'One plays at a time', host: 'Only the host plays' };
const POLICY = { ask: 'Ask to join', watch: 'Join as viewer', open: 'Join and play' };

/** How long a room has been open, as the app says it. */
function openFor(seconds) {
  const minutes = Math.floor(seconds / 60);
  if (minutes < 1) return 'just opened';
  if (minutes < 60) return `open for ${minutes} min`;
  const hours = Math.floor(minutes / 60);
  return minutes % 60 ? `open for ${hours} h ${minutes % 60} min` : `open for ${hours} h`;
}

/** The line under a room's title: who hosts, what is played, for how long. */
function roomLine(room) {
  const who = room.game ? `${room.host} plays ${room.game}` : `Hosted by ${room.host}`;
  return room.since > 0 ? `${who} · ${openFor(room.since)}` : who;
}

/**
 * The game as it is now where the host sends pictures, otherwise the game's cover where the
 * host's app found one, otherwise its initial on the brand gradient.
 */
function roomCover(room) {
  const cover = el('div', 'cover', (room.game || room.title || '?').trim().charAt(0).toUpperCase());
  cover.setAttribute('aria-hidden', 'true');
  if (room.cover) {
    // Only ever Sony's image server (rooms.js), as the page's Content-Security-Policy says
    const img = el('img');
    img.alt = '';
    img.decoding = 'async';
    img.addEventListener('error', () => img.remove());
    img.src = room.cover;
    cover.append(img);
  }
  if (room.picture) {
    livePicture(room.picture).then((address) => {
      if (!address) return;
      const img = el('img', 'live-picture');
      img.alt = '';
      img.src = address;
      const badge = el('span', 'live-badge', 'Live');
      badge.prepend(el('i'));
      cover.classList.add('live');
      cover.append(img, badge);
      // Over the picture, the game's cover small in the corner: which game it is
      if (room.cover) {
        const small = el('img', 'cover-badge');
        small.alt = '';
        small.decoding = 'async';
        small.addEventListener('error', () => small.remove());
        small.src = room.cover;
        cover.append(small);
      }
    });
  }
  return cover;
}

/** The players of a room as small circles: up to five, or four and "+N". */
const FACE_COLOURS = ['#a77bff', '#f472b6', '#34d399', '#fbbf24', '#60a5fa', '#fb923c'];

function roomFaces(room) {
  const row = el('span', 'faces');
  row.setAttribute('aria-hidden', 'true');
  const many = room.initials.length > 5;
  const shown = many ? room.initials.slice(0, 4) : room.initials;
  shown.forEach((initial, i) => {
    const face = el('span', 'face', initial);
    face.style.background = FACE_COLOURS[i % FACE_COLOURS.length];
    const address = room.faces[i];
    if (address) {
      livePicture(address).then((picture) => {
        if (!picture) return;
        const img = el('img');
        img.alt = '';
        img.src = picture;
        face.append(img);
      });
    }
    row.append(face);
  });
  if (many) row.append(el('span', 'face more', `+${room.initials.length - 4}`));
  return row;
}

/**
 * Room pictures as blob: addresses, one fetch for each picture (its address names the
 * room and when the picture was taken). Fetched rather than loaded by an <img>, so that
 * the list may live on any server without widening the page's Content-Security-Policy;
 * the list answers with Access-Control-Allow-Origin as for the list itself.
 */
const roomPictures = new Map();

function livePicture(address) {
  if (roomPictures.has(address)) return roomPictures.get(address);
  const promise = (async () => {
    try {
      const reply = await fetch(address, { credentials: 'omit' });
      if (!reply.ok) return '';
      const blob = await reply.blob();
      if (blob.type !== 'image/jpeg' || blob.size > 64 * 1024) return '';
      return URL.createObjectURL(blob);
    } catch {
      return '';
    }
  })();
  roomPictures.set(address, promise);
  // The newest few dozen are enough: a room's next picture has another address
  while (roomPictures.size > 60) {
    const [oldest] = roomPictures.keys();
    const old = roomPictures.get(oldest);
    roomPictures.delete(oldest);
    old.then((url) => { if (url) URL.revokeObjectURL(url); });
  }
  return promise;
}

/** What kind of room it is: its language, how it is played, how one gets in, whether browsers can come. */
function fillBadges(badges, room) {
  badges.replaceChildren(el('span', 'badge', languageName(room.lang)),
    el('span', 'badge play', CONTROL[room.control]),
    el('span', 'badge door', POLICY[room.policy]));
  if (room.web) badges.append(el('span', 'badge web', 'Browsers welcome'));
  return badges;
}

/** One room of the list, as on the app's Join page. */
function roomCard(room) {
  const can = room.web && room.compatible && !room.full;
  const card = el('article', 'room');
  const main = el('div', 'room-main');

  const body = el('div', 'room-body');
  body.append(el('h3', 'room-title', room.title), el('p', 'room-sub', roomLine(room)),
    fillBadges(el('div', 'badges'), room));

  // Who plays (their pictures, or their initials where they have none) and how many, of how many
  const players = el('div', 'room-players' + (room.full ? ' full' : ''));
  const count = el('b', '', `${room.players} / ${room.max}`);
  const label = el('span', 'players-word', 'players');
  const line = el('span', 'players-count');
  if (!room.initials.length) {
    // An older host says how many only
    line.append(el('span', 'people'));
  } else {
    players.append(roomFaces(room));
  }
  line.append(count, label);
  line.setAttribute('aria-hidden', 'true');
  players.append(line, el('span', 'sr-only', `${room.players} of ${room.max} players${room.full ? ', full' : ''}`));

  const action = el('div', 'room-action');
  const join = button(can ? 'primary' : 'ghost', room.full ? 'Full' : 'Join', () => joinRoom(room));
  join.disabled = !can;
  join.dataset.room = room.invite;
  join.setAttribute('aria-label', `${room.full ? 'Full' : 'Join'}: ${room.title}`);
  action.append(join);
  if (!room.compatible || !room.web) {
    action.append(el('small', 'room-note', !room.compatible ? 'Needs another version of the app' : 'App only'));
  }

  main.append(roomCover(room), body, players, action);
  card.append(main);
  return card;
}

/** Every room again: no words, any language, full ones and those that ask. */
function showAllRooms() {
  $('rooms-search').value = '';
  $('rooms-lang').value = '';
  $('rooms-hide-full').checked = false;
  $('rooms-open-only').checked = false;
  renderRooms();
  $('rooms-search').focus();
}

function renderRooms() {
  const list = $('rooms-list');
  const filters = {
    search: $('rooms-search').value,
    lang: $('rooms-lang').value,
    hideFull: $('rooms-hide-full').checked,
    openOnly: $('rooms-open-only').checked,
  };
  // Nothing new: the cards stay. Leaving the search field says "change" once more, and a list
  // drawn anew under the pointer would swallow the click on one of its buttons.
  const key = JSON.stringify([roomsReadAt, filters]);
  if (roomsDrawn.list === roomList && roomsDrawn.key === key) return;
  roomsDrawn = { list: roomList, key };
  const shown = filterRooms(roomList, filters);
  $('rooms-count').textContent = !roomsReadAt ? ''
    : shown.length === roomList.length ? `${roomList.length} open` : `${shown.length} of ${roomList.length}`;
  // The list is drawn anew every few seconds: the keys find the same room's button again
  const focused = list.contains(document.activeElement) ? document.activeElement.dataset.room || '' : null;
  const items = shown.map(roomCard);
  if (!shown.length && roomsReadAt) {
    items.push(el('div', 'rooms-empty', roomList.length
      ? 'No room matches. Try other words, another language, or show all rooms.'
      : 'No public rooms right now. Anybody with the app can host one.'));
  }
  // Rooms the search and the filters leave out, and a way to see them all
  const hidden = roomList.length - shown.length;
  if (hidden > 0) {
    items.push(button('ghost small rooms-show-all', `${hidden} ${hidden === 1 ? 'room' : 'rooms'} hidden by your filters · show all`, showAllRooms));
  }
  list.replaceChildren(...items);
  if (focused !== null) {
    const again = Array.from(list.querySelectorAll('button')).find((b) => (b.dataset.room || '') === focused && !b.disabled);
    if (again) again.focus();
    else $('rooms-search').focus();
  }
}

/** A room from the list: like a link from a friend, to its host. */
function joinRoom(room) {
  let text = room.invite;
  // A meeting point of one's own, for trying things out on this machine
  if (LOCAL_MEET && !text.includes('?meet=')) text += '?meet=' + LOCAL_MEET;
  const parsed = parseInvite(text, LOCAL);
  if (!parsed) return;
  invite = parsed;
  invite.room = room;
  try {
    sessionStorage.setItem('psrpt.invite', text);
  } catch (e) {
    // only for this visit, then
  }
  showJoin();
}

/** Something the player has to know, at the top of the picture for a few seconds. */
function notice(text) {
  const n = el('div', 'notice', text);
  ui.notices.append(n);
  while (ui.notices.children.length > 3) ui.notices.firstElementChild.remove();
  setTimeout(() => n.remove(), 7700);
}

/** @param {boolean} [bad] something went wrong (red), rather than simply being over */
function message(title, text, retryLabel = 'Try again', bad = true) {
  $('message-title').textContent = title;
  $('message-text').textContent = text;
  $('message-text').classList.toggle('problem', bad);
  $('message-retry').textContent = retryLabel;
  show(ui.message);
  $('message-retry').focus();
}

/** Something on the page has the keys and the controller right now, as the stream menu has in the app. */
function grabbed() {
  return !!(menu && (menu.open || menu.closing)) || !!(hud && hud.editing);
}

/** The keyboard is not the game's: somebody writes, or a window of the page is open. */
function typing() {
  const a = document.activeElement;
  const field = !!(a && (a.tagName === 'INPUT' || a.tagName === 'TEXTAREA' || a.tagName === 'SELECT')
    && a.type !== 'checkbox' && a.type !== 'range');
  return field || $('keys').open || grabbed() || !ui.chat.hidden || !!(options && options.isOpen) || !$('sleep-ask').hidden;
}

function remember(key, value) {
  try {
    localStorage.setItem(key, String(value));
  } catch (e) {
    // not kept, then
  }
}

function recall(key, fallback = '') {
  try {
    const v = localStorage.getItem(key);
    return v === null ? fallback : v;
  } catch (e) {
    return fallback;
  }
}

function forget(key) {
  try {
    localStorage.removeItem(key);
  } catch (e) {
    // was not there
  }
}

function playerId() {
  let stored = null;
  try {
    stored = sessionStorage.getItem('psrpt.player');
  } catch (e) {
    stored = null;
  }
  if (stored && /^[0-9a-f]{16}$/.test(stored)) {
    return new Uint8Array(stored.match(/../g).map((h) => parseInt(h, 16)));
  }
  const id = crypto.getRandomValues(new Uint8Array(8));
  id[0] |= 1;
  try {
    sessionStorage.setItem('psrpt.player', Array.from(id, (b) => b.toString(16).padStart(2, '0')).join(''));
  } catch (e) {
    // private mode: a new one after every reload, which only costs the old place
  }
  return id;
}

// ------------------------------------------------------------------ the invite

function readInvite() {
  let text = location.hash.length > 1 ? location.hash : '';
  if (text) {
    // Out of the address bar: a link in a screenshot or a stream lets anybody in
    try {
      sessionStorage.setItem('psrpt.invite', text);
    } catch (e) {
      // then it stays where it is
    }
    try {
      history.replaceState(null, '', location.pathname + location.search);
    } catch (e) {
      // a file opened from disk
    }
  } else {
    try {
      text = sessionStorage.getItem('psrpt.invite') || '';
    } catch (e) {
      text = '';
    }
  }
  return text ? parseInvite(text, LOCAL) : null;
}

// ------------------------------------------------------------------ the player's picture

async function setAvatar(data, store) {
  if (!data || !data.length || data.length > P.MAX_AVATAR_SIZE) {
    avatar = null;
    if (store) forget('psrpt.avatar');
  } else {
    // A picture goes by the first bytes of its SHA-256, as everywhere in the protocol
    const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', data));
    avatar = { ref: { hash: digest.slice(0, 8), size: data.length, valid: true }, data };
    if (store) remember('psrpt.avatar', base64UrlEncode(data));
  }
  showAvatar();
}

function showAvatar() {
  const b = $('join-avatar');
  const old = b.dataset.url;
  if (old) URL.revokeObjectURL(old);
  if (avatar) {
    const url = URL.createObjectURL(new Blob([avatar.data], { type: 'image/jpeg' }));
    b.dataset.url = url;
    b.style.backgroundImage = `url("${url}")`;
    b.textContent = '';
  } else {
    b.dataset.url = '';
    b.style.backgroundImage = '';
    b.textContent = ($('join-name').value.trim()[0] || '+').toUpperCase();
  }
  $('join-avatar-remove').hidden = !avatar;
}

// ------------------------------------------------------------------ before joining

/**
 * The head of the join and the waiting card: a room from the list shows its cover, what it is
 * and its badges, as its card in the list; an invite from a friend the app's Join door.
 */
function fillHead(prefix) {
  const room = invite.room;
  $(`${prefix}-title`).textContent = room ? `Join “${room.title}”` : 'You are invited to play';
  $(`${prefix}-tile`).hidden = !!room;
  const cover = $(`${prefix}-cover`);
  cover.replaceChildren(...(room ? [roomCover(room)] : []));
  cover.hidden = !room;
  const sub = $(`${prefix}-sub`);
  sub.textContent = room ? roomLine(room) : '';
  sub.hidden = !room;
  const badges = $(`${prefix}-badges`);
  if (badges) {
    if (room) fillBadges(badges, room);
    else badges.replaceChildren();
    badges.hidden = !room;
  }
}

function showJoin() {
  if (!invite) {
    show(ui.landing);
    return;
  }
  fillHead('join');
  $('join-name').value = recall('psrpt.name');
  $('join-voice').checked = recall('psrpt.voice') === '1';
  $('join-muted').checked = recall('psrpt.muted') === '1';
  $('join-controller-only').checked = recall('psrpt.controllerOnly') === '1';
  const warning = $('join-warning');
  const problems = [];
  if (typeof RTCPeerConnection !== 'function') {
    problems.push('This browser has no WebRTC, which the connection to the host needs. A current Chrome, Edge, Firefox or Safari has it.');
    $('join-button').disabled = true;
  } else if (!VideoPlayer.supported()) {
    problems.push('This browser cannot show the picture (it lacks WebCodecs). You can still join with your controller only, for example when you sit in front of the same TV. For picture and sound use a current Chrome, Edge, Firefox or Safari.');
  } else if (!Sound.supported()) {
    problems.push('This browser can show the picture but not play the sound. A current Chrome, Edge, Firefox or Safari can do both.');
  }
  if (!window.isSecureContext) {
    problems.push('This page was not opened over HTTPS, so the browser keeps controllers and the microphone from it.');
  }
  warning.hidden = problems.length === 0;
  warning.textContent = problems.join(' ');
  syncJoinOptions();
  showAvatar();
  show(ui.join);
  $('join-name').focus();
}

function syncJoinOptions() {
  $('join-voice').disabled = !Sound.supported();
  $('join-muted').closest('label').hidden = !$('join-voice').checked || $('join-voice').disabled;
}

// ------------------------------------------------------------------ joining

/** Whether this browser shows the console's picture in this session. */
function hasPicture() {
  return !controllerOnly && VideoPlayer.supported();
}

/** What the host is asked to send. */
function wantFlags() {
  let want = P.WANT_CAMS;
  // Controller only: no picture and no sound, but still rumble, voices and webcams, as in the app
  if (hasPicture()) {
    want |= P.WANT_VIDEO;
    if (Sound.supported()) want |= P.WANT_AUDIO;
  }
  if (voiceOn && Sound.supported()) want |= P.WANT_VOICE;
  if (prefs.haptics > 0) want |= P.WANT_HAPTICS;
  // A guest only gets the others' inputs when it asks for them
  if (hud && hud.wantPads) want |= P.WANT_INPUTS;
  return want;
}

function mediaFlags() {
  let media = camera.on ? P.MEDIA_CAMERA : 0;
  if (voiceOn) media |= P.MEDIA_VOICE | (micMuted || !sound.micOn ? P.MEDIA_MUTED : 0);
  // Players who go live from the app leave us out of their streams
  if (prefs.noStreams) media |= P.MEDIA_NO_STREAMS;
  return media;
}

function applyWant() {
  if (session) session.setWant(wantFlags());
}

function makeSession() {
  const s = new Session({
    secret: invite.secret,
    meetUrl: invite.meetUrl,
    stun: invite.stun,
    name: $('join-name').value.trim() || 'Guest',
    playerId: playerId(),
    avatar,
    want: wantFlags(),
  }, {
    onStatus: connectingStatus,
    onState,
    onVideoConfig: (config) => {
      if (player) player.setConfig(config);
    },
    onVideoFrame: (frame) => {
      if (player) player.push(frame);
    },
    onAudio: (opus) => sound.pushGame(opus),
    onVoice: (slot, opus) => sound.pushVoice(slot, opus),
    onHaptics: (pcm) => {
      if (pad) pad.haptics(pcm);
    },
    onInputs,
    onChat,
    onBlob,
    onCamFragment,
    onClosed,
  });
  s.setMedia(mediaFlags());
  if (pad) {
    pad.read();
    s.setController(pad.shownKind, pad.battery);
  }
  return s;
}

/** What happens on the way in; for a room from the list the host has a name. */
function connectingStatus(text) {
  const host = invite && invite.room ? invite.room.host : '';
  $('connecting-text').textContent = host && text === 'Waiting for the host to let you in…'
    ? `Waiting for ${host} to let you in…` : text;
}

/** The microphone, for a player who is in the voice chat. */
async function openMic() {
  const problem = await sound.startMic();
  if (problem) notice(`Voice chat: ${problem} You can listen but not talk.`);
  sound.setMicMuted(micMuted);
}

async function join() {
  if (!invite) {
    // Nothing to join with: back to where a link can be pasted
    show(ui.landing);
    return;
  }
  remember('psrpt.name', $('join-name').value.trim());
  controllerOnly = $('join-controller-only').checked;
  voiceOn = $('join-voice').checked && !$('join-voice').disabled;
  micMuted = voiceOn && $('join-muted').checked;
  remember('psrpt.voice', $('join-voice').checked ? '1' : '0');
  remember('psrpt.muted', $('join-muted').checked ? '1' : '0');
  remember('psrpt.controllerOnly', controllerOnly ? '1' : '0');
  leaving = false;
  reconnecting = '';
  fillHead('connecting');
  $('connecting-text').textContent = 'Getting ready…';
  $('connecting-hint').textContent = `Press ${pad && pad.kind ? 'L1+R1+L3+R3' : MENU_KEY} to open stream menu`;
  show(ui.connecting);
  // The Join button is gone: the keys go on to the way out, as in the app
  $('connecting-cancel').focus();

  // Now, while this is still the click on "Join": browsers start sound only then
  const soundOk = await sound.start();
  if (voiceOn && soundOk) await openMic();
  else if (voiceOn) voiceOn = false;

  session = makeSession();
  const mine = session;
  try {
    await mine.connect();
  } catch (e) {
    if (mine !== session) return; // cancelled, or another attempt has taken over
    session = null;
    sound.stopMic();
    if (e instanceof JoinError && e.kind === 'cancelled') showJoin();
    else message('That did not work', e && e.message ? e.message : String(e));
    return;
  }
  if (mine !== session) {
    mine.close();
    return;
  }
  enterStage();
  if (hasPicture() && !soundOk && sound.problem) notice(sound.problem);
}

function enterStage() {
  ui.front.hidden = true;
  ui.stage.hidden = false;
  ui.banner.hidden = true;
  reconnecting = '';
  lastState = null;
  playersKey = '';
  feedback = { rumble: 0, trigger: 0, led: 0, player: 0 };
  statsLast = { at: performance.now(), bytes: 0, shown: 0, fragments: 0, nacks: 0 };

  if (hasPicture() && !player) {
    player = new VideoPlayer(ui.screen, {
      onNeedKeyframe: () => {
        if (session) session.requestKeyframe();
      },
      onSize: () => applyPicture(),
      onError: (text) => notice(text),
    });
  }
  if (player) player.setCodec(session.welcome.stream.codec);
  sound.setGameFormat(session.welcome.audio);
  sound.onVoiceFrame = (opus) => {
    if (session) session.sendVoice(opus);
  };
  camera.onFrame = (jpeg) => {
    if (session) session.sendCamFrame(jpeg);
    // What the others get of this player is what this player sees of themselves
    if (lastState) showCamFrame(lastState.yourSlot, jpeg);
  };
  showStageNote();

  clearInterval(padTimer);
  padTimer = setInterval(padTick, 4);
  clearInterval(slowTimer);
  slowTimer = setInterval(slowTick, 250);
  clearInterval(talkTimer);
  talkTimer = setInterval(updateTalking, 100);
  hud.layout();
  applyPicture();
  syncMenu();
  wake();
  if (navigator.wakeLock) {
    navigator.wakeLock.request('screen').then((lock) => {
      wakeLock = lock;
    }).catch(() => {});
  }
}

/** Without the console's picture: what the app writes there. */
function showStageNote() {
  const off = !hasPicture();
  ui.note.hidden = !off;
  if (!off) return;
  $('stage-note-title').textContent = 'Controller only';
  const why = controllerOnly ? 'You joined without picture and sound. Your controller plays along.'
    : 'This browser cannot show the picture. Your controller plays along.';
  $('stage-note-text').textContent = `${why}\nPress ${pad && pad.kind ? 'L1+R1+L3+R3' : MENU_KEY} for the stream menu.`;
}

function leaveStage() {
  clearInterval(padTimer);
  clearInterval(slowTimer);
  clearInterval(talkTimer);
  clearTimeout(idleTimer);
  if (pad) pad.stopFeedback();
  dualsense.release();
  if (player) {
    player.close();
    player = null;
  }
  sound.reset();
  sound.stopMic();
  stopCamera();
  forgetCams();
  playersKey = '';
  hud.clear();
  menu.close();
  options.close();
  if (touch) touch.releaseAll();
  if (wakeLock) wakeLock.release().catch(() => {});
  wakeLock = null;
  if (document.fullscreenElement) document.exitFullscreen().catch(() => {});
  ui.chat.hidden = true;
  ui.emoji.hidden = true;
  ui.banner.hidden = true;
  $('sleep-ask').hidden = true;
  ui.chatList.replaceChildren();
  menu.clearChat();
  ui.notices.replaceChildren();
  lastChat = null;
  unread = 0;
}

/** @param {boolean} [sleepConsole] also asks the host to put the console to sleep */
function leave(sleepConsole = false) {
  leaving = true;
  $('sleep-ask').hidden = true;
  if (session) session.close(sleepConsole === true);
  session = null;
  leaveStage();
  showJoin();
}

/** Whether the host lets guests put the console to sleep when they leave. */
function maySleep() {
  const s = lastState;
  const me = s ? s.participants.find((p) => p.slot === s.yourSlot) : null;
  return !!(me && me.maySleep && session && session.connected && !reconnecting);
}

/** Leaving from the menu: like the app, it asks about the console's sleep first where a guest may decide that. */
function askLeave() {
  if (!maySleep()) {
    leave();
    return;
  }
  const host = lastState.participants.find((p) => p.host);
  $('sleep-ask-text').textContent = `Put ${host && host.name ? host.name : 'the host'}'s console into sleep mode?\nThat ends the session for everybody.`;
  menu.close();
  $('sleep-ask').hidden = false;
  $('sleep-ask-yes').focus();
}

/** The connection ended without the player wanting it. */
function onClosed(reason) {
  if (leaving) return;
  session = null;
  if (reason === 'kicked' || reason === 'ended') {
    leaveStage();
    message(reason === 'kicked' ? 'You were removed' : 'The session is over',
      reason === 'kicked' ? 'The host removed you from the session.' : 'The host has stopped sharing. When they share again, the same link lets you back in.',
      'Join again', reason === 'kicked');
    return;
  }
  // The connection broke, or the host is getting its console back: try to return
  rejoin(reason === 'reconnecting'
    ? 'The host lost its console — waiting for it to come back…'
    : 'Connection to the host lost — reconnecting…');
}

async function rejoin(text) {
  reconnecting = text;
  $('banner-text').textContent = text;
  ui.banner.hidden = false;
  syncMenu();
  if (pad) pad.stopFeedback();
  sound.reset();
  const deadline = performance.now() + REJOIN_MS;
  let lastProblem = '';
  while (!leaving && performance.now() < deadline) {
    const attempt = makeSession();
    attempt.h.onStatus = () => {};
    session = attempt;
    try {
      await attempt.connect();
    } catch (e) {
      if (leaving || attempt !== session) return;
      session = null;
      lastProblem = e && e.message ? e.message : String(e);
      // A full session or a wrong version does not get better by waiting
      if (e instanceof JoinError && ['full', 'version', 'unsupported', 'refused', 'locked'].includes(e.kind)) break;
      await new Promise((resolve) => setTimeout(resolve, 1500));
      continue;
    }
    if (leaving || attempt !== session) {
      attempt.close();
      return;
    }
    if (player) {
      player.close();
      player = null;
    }
    forgetCams();
    enterStage();
    return;
  }
  if (leaving) return;
  session = null;
  leaveStage();
  message('The connection was lost', lastProblem || 'The host could not be reached any more.');
}

// ------------------------------------------------------------------ while playing

function padTick() {
  if (!session || !pad) return;
  const state = pad.read();
  session.setController(pad.shownKind, pad.battery);
  // The same buttons as in the app open and close the stream menu
  const combo = (state.buttons & MENU_COMBO) === MENU_COMBO;
  if (combo && !comboHeld) menu.toggle(true);
  comboHeld = combo;
  // In the menu the controller moves through it: the D-pad or the stick, Cross presses, Circle goes back
  if (menu.open && !hud.editing) menu.padInput(state.buttons, state.lx, state.ly);
  // While the menu is open or the display is arranged, nothing goes to the game
  session.setPad(grabbed() ? P.emptyPad() : state);
}

function slowTick() {
  if (!session) return;
  if (pad) pad.tick();
  updateStats();
  // A controller that was plugged in after joining makes the controls on the screen unnecessary
  if (touch && touch.visible && touch.auto && pad && pad.kind) setTouch(false, true);
}

function applyFeedback(f, force) {
  if (force || f.rumbleSeq !== feedback.rumble) {
    feedback.rumble = f.rumbleSeq;
    if (pad) pad.rumble(f.rumbleLeft, f.rumbleRight);
  }
  if (!dualsense.connected) return;
  if (force || f.triggerSeq !== feedback.trigger) {
    feedback.trigger = f.triggerSeq;
    if (f.triggerSeq) dualsense.setTriggers(f.triggerTypeLeft, f.triggerLeft, f.triggerTypeRight, f.triggerRight);
  }
  if (force || f.ledSeq !== feedback.led) {
    feedback.led = f.ledSeq;
    if (f.ledSeq) dualsense.setLed(f.led[0], f.led[1], f.led[2]);
  }
  if (force || f.playerIndexSeq !== feedback.player) {
    feedback.player = f.playerIndexSeq;
    if (f.playerIndexSeq) dualsense.setPlayerIndex(f.playerIndex);
  }
}

/** Everybody in the session, the way the display and the menu want them. */
function playersNow() {
  const s = lastState;
  if (!s) return [];
  return s.participants.map((p) => ({
    slot: p.slot,
    name: p.name,
    you: p.slot === s.yourSlot,
    // Of the others the page cannot tell whether they came with a browser; of itself it can
    web: p.slot === s.yourSlot,
    host: p.host,
    // Guests watch while only the host plays ("Only me"), whatever they may do otherwise
    canControl: p.canControl && (p.host || s.mixMode !== P.MIX_HOST_ONLY),
    onlyHost: !p.host && s.mixMode === P.MIX_HOST_ONLY,
    inControl: p.inControl,
    voice: (p.media & P.MEDIA_VOICE) !== 0,
    muted: (p.media & P.MEDIA_MUTED) !== 0,
    silenced: (p.media & P.MEDIA_SILENCED) !== 0,
    mutedHere: sound.muted.has(p.slot),
    camera: (p.media & P.MEDIA_CAMERA) !== 0,
    live: (p.media & P.MEDIA_LIVE) !== 0,
    noStreams: (p.media & P.MEDIA_NO_STREAMS) !== 0,
    battery: p.battery,
    avatarUrl: p.avatar.valid ? (blobUrls.get(P.hashKey(p.avatar.hash)) || '') : '',
  }));
}

function showPlayers() {
  if (!lastState) return;
  const players = playersNow();
  // "In control" only says something when not everybody is
  const exclusive = lastState.mixMode !== P.MIX_TOGETHER;
  // The host says who is here several times a second; only what has changed is drawn again
  const key = JSON.stringify([players, exclusive]);
  if (key === playersKey) return;
  playersKey = key;
  hud.setPlayers(players, exclusive);
  menu.setPlayers(players);
  $('chat-sub').textContent = players.length > 1 ? `${players.length} players in this session`
    : 'Everybody who joins can read along';
}

function onState(s) {
  const before = lastState;
  lastState = s;
  if (player) player.setCodec(s.stream.codec);
  sound.setGameFormat(s.audio);
  applyFeedback(s.feedback, false);

  const here = new Set(s.participants.map((p) => p.slot));
  if (before) {
    for (const p of before.participants) {
      if (here.has(p.slot)) continue;
      sound.forgetVoice(p.slot);
      forgetCam(p.slot);
    }
  }
  for (const p of s.participants) {
    if (!(p.media & P.MEDIA_CAMERA)) forgetCam(p.slot);
  }
  showPlayers();
  if (!before) syncMenu();
}

function updateTalking() {
  if (!lastState) return;
  const talking = sound.talkingNow(sound.micOn && !micMuted ? lastState.yourSlot : undefined);
  hud.setTalking(talking);
  if (menu.open) menu.setTalking(talking);
}

/** A picture has arrived: a player's, or the one of a link in the chat. */
function onBlob(ref, data) {
  const key = P.hashKey(ref.hash);
  if (!blobUrls.has(key)) blobUrls.set(key, URL.createObjectURL(new Blob([data])));
  showPlayers();
  for (const img of ui.chatList.querySelectorAll(`[data-blob="${key}"]`)) {
    if (img.tagName === 'IMG') {
      img.src = blobUrls.get(key);
      img.hidden = false;
    } else img.style.backgroundImage = `url("${blobUrls.get(key)}")`;
  }
}

// ------------------------------------------------------------------ the numbers of the connection

function buildStats() {
  const row = (cls, label) => {
    const r = el('div', 'stat ' + cls);
    const value = el('b');
    r.append(value, el('span', '', label));
    ui.stats.append(r);
    return value;
  };
  return {
    rate: row('rate', 'Mbps'),
    fps: row('blue', 'frames per second'),
    rtt: row('blue', 'to the host and back'),
    loss: row('red', 'packet loss'),
    dropped: row('red', 'dropped frames'),
    lost: row('red', 'lost frames'),
  };
}

let statRows = null;

function updateStats() {
  const now = performance.now();
  if (now - statsLast.at < 1000) return;
  const seconds = (now - statsLast.at) / 1000;
  const shown = player ? player.stats.shown : 0;
  const a = session.assembler.stats;
  const fragments = a.fragments - statsLast.fragments;
  const nacks = a.nackFragments - statsLast.nacks;
  numbers = {
    mbps: (((session.stats.bytes - statsLast.bytes) * 8) / seconds / 1e6).toFixed(1),
    fps: Math.round((shown - statsLast.shown) / seconds),
    loss: fragments + nacks > 0 ? (nacks / (fragments + nacks)) * 100 : 0,
  };
  statsLast = { at: now, bytes: session.stats.bytes, shown, fragments: a.fragments, nacks: a.nackFragments };
  if (menu.open) syncConnection();
  if (ui.stats.hidden) return;
  if (!statRows) statRows = buildStats();
  statRows.rate.textContent = numbers.mbps;
  statRows.fps.textContent = String(numbers.fps);
  statRows.rtt.textContent = `${Math.round(session.rttMs)} ms`;
  statRows.loss.textContent = `${numbers.loss.toFixed(1)}%`;
  statRows.dropped.textContent = String(player ? player.stats.skipped : 0);
  statRows.lost.textContent = String(a.dropped);
}

/** Like in the app: not while the menu is in front, and not without a picture. */
function showStats() {
  ui.stats.hidden = !prefs.stats || !hasPicture() || menu.open || menu.closing;
  statsLast.at = 0;
  if (session && !ui.stats.hidden) updateStats();
}

// ------------------------------------------------------------------ webcams

/** Puts a webcam picture on screen and gives the one before the last back. */
function showCamFrame(slot, jpeg) {
  const url = URL.createObjectURL(new Blob([jpeg], { type: 'image/jpeg' }));
  let shown = camShown.get(slot);
  if (!shown) {
    shown = [];
    camShown.set(slot, shown);
  }
  shown.push(url);
  // The picture before this one may still be on its way onto the screen
  while (shown.length > 2) URL.revokeObjectURL(shown.shift());
  hud.setCamFrame(slot, url);
  menu.setCamFrame(slot, url);
}

function onCamFragment(c) {
  let cam = cams.get(c.slot);
  if (!cam) {
    cam = { seq: -1, data: null, got: null, count: 0, done: 0, started: false };
    cams.set(c.slot, cam);
  }
  // Only the newest picture is collected
  if (cam.started && ((c.frameSeq - cam.done) | 0) <= 0) return;
  if (c.frameSeq !== cam.seq) {
    if (cam.seq >= 0 && ((c.frameSeq - cam.seq) | 0) < 0) return;
    cam.seq = c.frameSeq;
    cam.data = new Uint8Array(c.totalSize);
    cam.got = new Uint8Array(c.fragCount);
    cam.count = 0;
  }
  if (cam.data.length !== c.totalSize || cam.got[c.fragIndex]) return;
  cam.data.set(c.data, c.fragIndex * P.CAM_FRAGMENT_SIZE);
  cam.got[c.fragIndex] = 1;
  if (++cam.count !== cam.got.length) return;
  cam.done = cam.seq;
  cam.started = true;
  showCamFrame(c.slot, cam.data);
}

function forgetCam(slot) {
  // One's own webcam is not the host's to end
  if (lastState && slot === lastState.yourSlot && camera.on) return;
  cams.delete(slot);
  const shown = camShown.get(slot);
  if (shown) for (const url of shown) URL.revokeObjectURL(url);
  camShown.delete(slot);
  hud.setCamFrame(slot, '');
  menu.setCamFrame(slot, '');
}

function forgetCams() {
  for (const slot of new Set([...cams.keys(), ...camShown.keys()])) {
    cams.delete(slot);
    for (const url of camShown.get(slot) || []) URL.revokeObjectURL(url);
    camShown.delete(slot);
    hud.setCamFrame(slot, '');
    menu.setCamFrame(slot, '');
  }
}

async function startCamera(source) {
  const problem = await camera.start(prefs.camSize, source);
  if (problem) notice(`Webcam: ${problem}`);
  syncVoice();
}

function stopCamera() {
  const was = camera.on;
  camera.stop();
  if (was && lastState) {
    const mine = lastState.yourSlot;
    for (const url of camShown.get(mine) || []) URL.revokeObjectURL(url);
    camShown.delete(mine);
    hud.setCamFrame(mine, '');
    menu.setCamFrame(mine, '');
  }
  syncVoice();
}

// ------------------------------------------------------------------ everybody's controller

function onInputs(inputs) {
  const states = { combined: inputs.combined };
  for (const p of inputs.players) states[String(p.slot)] = p;
  hud.setPadStates(states);
}

// ------------------------------------------------------------------ chat

function withEmoji(text) {
  let out = text;
  for (const [code, emoji] of Object.entries(SHORTCODES)) out = out.split(code).join(emoji);
  for (const [pattern, emoji] of FACES) out = out.replace(pattern, emoji);
  return out;
}

function chatEmpty() {
  const empty = el('div', 'chat-empty');
  empty.append(el('div', 'chat-empty-icon', '💬'), el('b', '', 'No messages yet'),
    el('span', '', 'Say hi! Links get a preview, and :fire: or :) turn into emoji.'));
  return empty;
}

function onChat(m) {
  const s = lastState;
  const mine = !!s && m.slot === s.yourSlot && mySent.has(m.upId);
  const who = s ? s.participants.find((p) => p.slot === m.slot) : null;
  const name = m.name || `Player ${m.slot + 1}`;
  // A run of messages by the same player shows the name once
  const continued = !!lastChat && lastChat.slot === m.slot && lastChat.name === m.name && m.timeMs - lastChat.timeMs < 120000;
  lastChat = { slot: m.slot, name: m.name, timeMs: m.timeMs };

  const entry = el('div', 'cmsg' + (continued ? ' continued' : ''));
  const side = el('div', 'cmsg-avatar');
  const body = el('div', 'cmsg-body');
  if (!continued) {
    const face = el('div', 'hud-avatar');
    face.style.width = '34px';
    face.style.height = '34px';
    const inner = el('div', 'hud-avatar-face');
    inner.style.inset = '0';
    inner.style.fontSize = '16px';
    const key = who && who.avatar.valid ? P.hashKey(who.avatar.hash) : '';
    const url = key ? blobUrls.get(key) : '';
    if (key) inner.dataset.blob = key;
    if (url) {
      inner.style.backgroundImage = `url("${url}")`;
      inner.classList.add('picture');
    } else inner.textContent = (name.trim().charAt(0) || '?').toUpperCase();
    face.append(inner);
    side.append(face);
    const head = el('div', 'cmsg-head');
    const label = el('span', 'cmsg-name', name);
    label.style.color = SLOT_COLORS[m.slot % SLOT_COLORS.length];
    head.append(label);
    if (who ? who.host : m.slot === 0) head.append(el('span', 'cmsg-host', 'HOST'));
    head.append(el('span', 'cmsg-time', new Date(m.timeMs).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', hour12: false })));
    body.append(head);
  }
  if (m.text) {
    const text = el('div', 'cmsg-text');
    appendWithLinks(text, m.text);
    body.append(text);
  }
  if (m.embed && /^https?:\/\//i.test(m.embed.url)) body.append(embedCard(m.embed));
  entry.append(side, body);

  const empty = ui.chatList.querySelector('.chat-empty');
  if (empty) empty.remove();
  const atEnd = ui.chatList.scrollHeight - ui.chatList.scrollTop - ui.chatList.clientHeight < 60;
  ui.chatList.append(entry);
  if (atEnd || mine) ui.chatList.scrollTop = ui.chatList.scrollHeight;
  // The last messages also show in the stream menu's panel
  menu.addChat({ slot: m.slot, name, text: m.text, embed: m.embed && /^https?:\/\//i.test(m.embed.url) ? m.embed : null });
  if (m.history) return;
  if (mine) return;
  // What somebody else wrote: for a few seconds on the game picture, unless the chat is open anyway
  hud.chatMessage({ slot: m.slot, name, text: m.text, embed: m.embed && m.embed.url ? m.embed : null });
  if (ui.chat.hidden) {
    unread++;
    menu.setUnread(unread);
  }
}

function hostOf(url) {
  try {
    return new URL(url).hostname;
  } catch (e) {
    return '';
  }
}

/** The preview of a link, as its sender made it. */
function embedCard(embed) {
  const a = el('a', 'cembed');
  a.href = embed.url;
  a.target = '_blank';
  a.rel = 'noopener noreferrer';
  const site = embed.site || hostOf(embed.url);
  if (site) a.append(el('div', 'cembed-site', site));
  a.append(el('div', 'cembed-title', embed.title || embed.url));
  if (embed.description) a.append(el('div', 'cembed-text', embed.description));
  if (embed.image.valid) {
    const key = P.hashKey(embed.image.hash);
    const img = el('img');
    img.alt = '';
    img.dataset.blob = key;
    const url = blobUrls.get(key);
    if (url) img.src = url;
    else img.hidden = true; // shows when the picture has arrived
    a.append(img);
  }
  return a;
}

function openChat(open) {
  ui.chat.hidden = !open;
  hud.chatOpen = open;
  if (open) {
    unread = 0;
    menu.setUnread(0);
    if (!ui.chatList.children.length) ui.chatList.append(chatEmpty());
    ui.chatList.scrollTop = ui.chatList.scrollHeight;
    ui.chatInput.focus();
  } else {
    ui.chatInput.blur();
    ui.emoji.hidden = true;
  }
  wake();
}

function buildEmoji() {
  const tabs = el('div', 'chat-picker-tabs');
  const grid = el('div', 'chat-picker-grid');
  const showGroup = (index) => {
    for (const [i, tab] of Array.from(tabs.children).entries()) tab.classList.toggle('selected', i === index);
    grid.replaceChildren(...EMOJI_GROUPS[index][1].split(' ').map((emoji) => button('', emoji, () => {
      const input = ui.chatInput;
      const at = input.selectionStart === null ? input.value.length : input.selectionStart;
      input.value = input.value.slice(0, at) + emoji + input.value.slice(input.selectionEnd === null ? at : input.selectionEnd);
      input.focus();
      input.selectionStart = input.selectionEnd = at + emoji.length;
      $('chat-send').disabled = false;
    })));
    grid.scrollTop = 0;
  };
  EMOJI_GROUPS.forEach(([icon], index) => tabs.append(button('', icon, () => showGroup(index))));
  ui.emoji.append(tabs, grid);
  showGroup(0);
}

// ------------------------------------------------------------------ the stream menu

/** The connection line of the menu. */
function syncConnection() {
  const host = lastState ? lastState.participants.find((p) => p.host) : null;
  menu.setConnection({
    connected: !!session && session.connected && !reconnecting,
    reconnecting,
    where: host && host.name ? host.name : 'the host',
    mbps: numbers.mbps,
    loss: numbers.loss,
    dropped: player ? player.stats.skipped : 0,
  });
}

/** One's own switches, here and for the others. */
function syncVoice() {
  if (session) {
    session.setMedia(mediaFlags());
    applyWant();
  }
  if (!menu) return;
  menu.setVoice({
    voice: Sound.supported(),
    voiceOn,
    micMuted: voiceOn && (micMuted || !sound.micOn),
    deafened,
    cam: Camera.supported(),
    camOn: camera.on,
    camSize: prefs.camSize,
    pictureOff: !hasPicture(),
    noStreams: prefs.noStreams,
  });
}

function controllerNote() {
  if (dualsense.connected) {
    return dualsense.live ? 'DualSense: touchpad, motion sensors, adaptive triggers and the light bar are in use.'
      : 'DualSense opened. Waiting for the controller to answer…';
  }
  if (pad && pad.kind) return `${controllerName(pad.kind)} connected.${pad.canRumble() ? '' : ' This browser cannot make it rumble.'}`;
  return 'No controller seen yet: press a button on it. The keyboard and the controls on the screen work without one.';
}

function syncController() {
  menu.setController({
    touch: !!(touch && touch.visible),
    haptics: prefs.haptics,
    dualsense: DualSense.supported(),
    dualsenseOn: dualsense.connected,
    note: controllerNote(),
    padKind: pad ? pad.kind : 0,
  });
}

function syncMenu() {
  if (!menu) return;
  syncConnection();
  syncVoice();
  menu.setSound(prefs.volume, prefs.voiceVolume, Sound.supported());
  menu.setPicture(prefs.mode, prefs.zoom, !!document.fullscreenElement, prefs.stats, hasPicture());
  syncController();
  menu.setPresets(hudSettings.presets, hudSettings.active);
  menu.setUnread(unread);
}

async function setVoice(on) {
  voiceOn = on && Sound.supported();
  if (voiceOn) {
    micMuted = false;
    if (await sound.start()) await openMic();
    else {
      voiceOn = false;
      if (sound.problem) notice(sound.problem);
    }
  } else sound.stopMic();
  syncVoice();
}

function setVolume(volume) {
  prefs.volume = Math.max(0, Math.min(100, Math.round(volume)));
  sound.setGameVolume(prefs.volume / 100);
  remember('psrpt.volume', prefs.volume);
  menu.setSound(prefs.volume, prefs.voiceVolume, Sound.supported());
}

function setVoiceVolume(volume) {
  prefs.voiceVolume = Math.max(0, Math.min(100, Math.round(volume)));
  sound.setVoiceVolume(prefs.voiceVolume / 100);
  remember('psrpt.voiceVolume', prefs.voiceVolume);
  menu.setSound(prefs.volume, prefs.voiceVolume, Sound.supported());
}

/** Fit, zoom or stretch, like the app's renderer does it. */
function applyPicture() {
  const c = ui.screen;
  let fit = 'contain';
  let scale = 1;
  if (prefs.mode === 'stretch') fit = 'fill';
  else if (prefs.mode === 'zoom') {
    if (prefs.zoom >= 0) scale = 1 + prefs.zoom;
    else {
      // Towards -1 the black bars go: the picture grows until it covers everything
      const W = ui.stage.clientWidth || 1;
      const H = ui.stage.clientHeight || 1;
      const sx = W / c.width;
      const sy = H / c.height;
      scale = 1 + (Math.max(sx, sy) / Math.min(sx, sy) - 1) * -prefs.zoom;
    }
  }
  c.style.objectFit = fit;
  c.style.transform = scale === 1 ? '' : `scale(${scale})`;
}

function setPicture(mode, zoom) {
  prefs.mode = mode;
  prefs.zoom = Math.max(-1, Math.min(4, Math.round(zoom * 100) / 100));
  remember('psrpt.pictureMode', prefs.mode);
  remember('psrpt.zoom', prefs.zoom);
  applyPicture();
  menu.setPicture(prefs.mode, prefs.zoom, !!document.fullscreenElement, prefs.stats, hasPicture());
}

function setTouch(on, auto) {
  if (!touch) return;
  touch.auto = !!auto;
  touch.show(on);
  ui.stage.classList.toggle('touch-on', on);
  if (menu) syncController();
}

/** Shows the mouse pointer and the way into the menu for a moment. */
function wake() {
  ui.stage.classList.remove('idle');
  clearTimeout(idleTimer);
  idleTimer = setTimeout(() => {
    if (typing() || ui.handle.matches(':hover')) {
      wake();
      return;
    }
    ui.stage.classList.add('idle');
  }, 3200);
}

/** What is in front decides what else shows: as in the app, the display and the numbers make way for the menu. */
function syncFront() {
  const menuUp = menu.open || menu.closing;
  $('hud').classList.toggle('away', menuUp);
  ui.handle.hidden = menuUp || hud.editing || !ui.chat.hidden || options.isOpen;
  showStats();
}

function keyLabel(code) {
  const special = { ArrowUp: '↑', ArrowDown: '↓', ArrowLeft: '←', ArrowRight: '→', Enter: 'Enter', Backspace: '⌫' };
  return special[code] || code.replace(/^Key/, '');
}

// ------------------------------------------------------------------ wiring

function start() {
  pad = new Pad(typing);
  touch = new TouchControls(ui.touch, ui.screen);
  pad.touch = touch;
  pad.dualsense = dualsense;
  dualsense.onChange = () => {
    if (menu) syncController();
    // What the game last asked of the triggers and lights goes to the controller that has just come
    if (dualsense.connected && lastState) applyFeedback(lastState.feedback, true);
  };
  dualsense.reconnect();

  // What the player set the last time
  prefs.volume = Number(recall('psrpt.volume', '100'));
  prefs.voiceVolume = Number(recall('psrpt.voiceVolume', '100'));
  prefs.haptics = Number(recall('psrpt.haptics', '1'));
  prefs.camSize = Number(recall('psrpt.camSize', '1'));
  prefs.noStreams = recall('psrpt.noStreams') === '1';
  prefs.mode = ['fit', 'zoom', 'stretch'].includes(recall('psrpt.pictureMode')) ? recall('psrpt.pictureMode') : 'fit';
  prefs.zoom = Number(recall('psrpt.zoom', '0')) || 0;
  prefs.stats = recall('psrpt.stats') === '1';
  sound.setGameVolume(prefs.volume / 100);
  sound.setVoiceVolume(prefs.voiceVolume / 100);
  pad.hapticsScale = prefs.haptics;
  const stored = recall('psrpt.avatar');
  if (stored) setAvatar(base64UrlDecode(stored), false);

  // ------------------------------------------------ the display, the menu, the options
  hud = new Hud($('hud'), hudSettings);
  hud.onWantChange = applyWant;
  hud.onEditingChange = () => syncFront();
  options = new OverlayOptions($('options'), hudSettings, {
    onArrange: () => hud.setEditing(true),
    onClose: () => syncFront(),
  });
  menu = new StreamMenu($('menu'), {
    onLeave: askLeave,
    onChat: () => {
      menu.close();
      openChat(true);
    },
    // A message from the panel's own field, the same as from the big chat
    onSend: (text) => {
      const clean = withEmoji(text.trim());
      if (!clean || !session) return false;
      const id = session.sendChat(clean);
      if (id) mySent.add(id);
      return !!id;
    },
    onVoice: setVoice,
    onMic: () => {
      micMuted = !micMuted;
      sound.setMicMuted(micMuted);
      syncVoice();
    },
    onVoices: () => {
      deafened = !deafened;
      sound.setDeafened(deafened);
      syncVoice();
    },
    onCam: () => (camera.on ? stopCamera() : startCamera()),
    onNoStreams: () => {
      prefs.noStreams = !prefs.noStreams;
      remember('psrpt.noStreams', prefs.noStreams ? '1' : '0');
      syncVoice();
    },
    onCamSize: (index) => {
      prefs.camSize = index;
      remember('psrpt.camSize', index);
      if (camera.on) startCamera();
    },
    onMutePlayer: (slot, muted) => {
      sound.setPlayerMuted(slot, muted);
      showPlayers();
    },
    onVolume: setVolume,
    onVoiceVolume: setVoiceVolume,
    onPictureMode: (mode) => setPicture(mode, prefs.zoom),
    onZoom: (zoom) => setPicture('zoom', zoom),
    onFullscreen: () => {
      if (document.fullscreenElement) document.exitFullscreen().catch(() => {});
      else if (ui.stage.requestFullscreen) ui.stage.requestFullscreen().catch(() => {});
    },
    onStats: () => {
      prefs.stats = !prefs.stats;
      remember('psrpt.stats', prefs.stats ? '1' : '0');
      menu.setPicture(prefs.mode, prefs.zoom, !!document.fullscreenElement, prefs.stats, hasPicture());
      showStats();
    },
    onTouch: () => setTouch(!touch.visible, false),
    onHaptics: (value) => {
      prefs.haptics = value;
      pad.hapticsScale = value;
      remember('psrpt.haptics', value);
      applyWant();
    },
    onDualSense: async () => {
      if (dualsense.connected) dualsense.detach();
      else {
        const problem = await dualsense.request();
        if (problem) notice(problem);
      }
      syncController();
    },
    onKeys: () => {
      menu.close();
      const keys = $('keys');
      if (keys.showModal) keys.showModal();
      else keys.setAttribute('open', '');
    },
    onPreset: (index) => hudSettings.setActive(index),
    onArrange: () => {
      menu.close();
      hud.setEditing(true);
    },
    onOptions: () => {
      menu.close();
      options.show();
      syncFront();
    },
  });
  menu.onOpenChange = (open) => {
    if (open) {
      // Nothing held keeps being held while the menu has the keys
      pad.keys.clear();
      if (touch) touch.releaseAll();
      syncMenu();
      updateTalking();
    }
    syncFront();
    wake();
  };
  hudSettings.onChange = () => {
    hud.settingsChanged();
    if (options.isOpen) options.sync();
  };
  hudSettings.onPresetsChange = () => {
    menu.setPresets(hudSettings.presets, hudSettings.active);
    if (options.isOpen) options.sync();
    hud.settingsChanged();
  };
  $('menu-handle-key').textContent = MENU_KEY;
  ui.handle.addEventListener('click', () => menu.show(false));

  // ------------------------------------------------ before the game
  // The public rooms: language choice, search and filters
  $('rooms-lang').replaceChildren(el('option', '', 'All languages'),
    ...LANGUAGES.map(([code, name]) => {
      const option = el('option', '', name);
      option.value = code;
      return option;
    }));
  $('rooms-lang').firstElementChild.value = '';
  for (const id of ['rooms-search', 'rooms-lang', 'rooms-hide-full', 'rooms-open-only']) {
    $(id).addEventListener('input', renderRooms);
    $(id).addEventListener('change', renderRooms);
  }
  $('rooms-refresh').addEventListener('click', refreshRooms);

  $('landing-form').addEventListener('submit', (e) => {
    e.preventDefault();
    const parsed = parseInvite($('landing-invite').value, LOCAL);
    const error = $('landing-error');
    if (!parsed) {
      error.textContent = 'That is not a link or invite of PSRemotePlayTogether. Copy the whole link your friend sent you.';
      error.hidden = false;
      return;
    }
    error.hidden = true;
    invite = parsed;
    try {
      sessionStorage.setItem('psrpt.invite', $('landing-invite').value.trim());
    } catch (err) {
      // only for this visit, then
    }
    showJoin();
  });

  $('join-form').addEventListener('submit', (e) => {
    e.preventDefault();
    join();
  });
  $('join-voice').addEventListener('change', syncJoinOptions);
  $('join-name').addEventListener('input', () => {
    if (!avatar) showAvatar();
  });
  const pickAvatar = () => $('join-avatar-file').click();
  $('join-avatar').addEventListener('click', pickAvatar);
  $('join-avatar-choose').addEventListener('click', pickAvatar);
  $('join-avatar-remove').addEventListener('click', () => setAvatar(null, true));
  $('join-avatar-file').addEventListener('change', async (e) => {
    const file = e.target.files && e.target.files[0];
    e.target.value = '';
    if (!file) return;
    const data = await makeAvatar(file, P.MAX_AVATAR_SIZE);
    const warning = $('join-warning');
    if (!data) {
      warning.textContent = 'That file is not a picture this browser can read.';
      warning.hidden = false;
      return;
    }
    await setAvatar(data, true);
  });
  $('connecting-cancel').addEventListener('click', () => {
    const s = session;
    session = null;
    if (s) s.close();
    sound.stopMic();
    showJoin();
  });
  $('message-retry').addEventListener('click', () => (invite ? showJoin() : show(ui.landing)));

  // The controller the browser sees, before joining and in the menu
  setInterval(() => {
    if (menu.open) syncController();
    if (ui.join.hidden) return;
    pad.read();
    const line = $('pad-status');
    line.classList.toggle('ok', !!pad.kind);
    const text = pad.kind
      ? `${controllerName(pad.kind)} connected.`
      : TouchControls.likely()
        ? 'No controller? You get one on the screen. Or pair one and press a button on it.'
        : 'Press any button on your controller so that the browser sees it. No controller? The keyboard works too.';
    if ($('pad-status-text').textContent !== text) $('pad-status-text').textContent = text;
  }, 400);

  // ------------------------------------------------ the chat
  $('chat-close').addEventListener('click', () => {
    openChat(false);
    syncFront();
  });
  $('chat-form').addEventListener('submit', (e) => {
    e.preventDefault();
    const text = withEmoji(ui.chatInput.value.trim());
    if (!text || !session) return;
    const id = session.sendChat(text);
    if (id) {
      mySent.add(id);
      ui.chatInput.value = '';
      $('chat-send').disabled = true;
    }
  });
  ui.chatInput.addEventListener('input', () => {
    $('chat-send').disabled = ui.chatInput.value.trim().length === 0;
  });
  buildEmoji();
  $('chat-emoji').addEventListener('click', () => {
    ui.emoji.hidden = !ui.emoji.hidden;
    if (ui.emoji.hidden) ui.chatInput.focus();
  });

  $('banner-leave').addEventListener('click', () => leave());
  $('sleep-ask-yes').addEventListener('click', () => leave(true));
  $('sleep-ask-no').addEventListener('click', () => leave(false));
  $('sleep-ask').addEventListener('keydown', (e) => {
    // Escape: stay in the session after all
    if (e.key === 'Escape') {
      e.preventDefault();
      e.stopPropagation();
      $('sleep-ask').hidden = true;
    } else if (e.key === 'ArrowLeft' || e.key === 'ArrowRight') {
      e.preventDefault();
      (document.activeElement === $('sleep-ask-yes') ? $('sleep-ask-no') : $('sleep-ask-yes')).focus();
    }
  });
  camera.onEnded = () => {
    stopCamera();
    notice('Webcam: the camera went away.');
  };

  const keys = $('keys');
  $('keys-list').replaceChildren(...KEYBOARD_MAP.map(([code, what]) => {
    const row = el('div');
    row.append(el('kbd', '', keyLabel(code)), el('span', '', what));
    return row;
  }));
  $('join-keys').addEventListener('click', () => (keys.showModal ? keys.showModal() : keys.setAttribute('open', '')));
  $('keys-close').addEventListener('click', () => keys.close());

  // ------------------------------------------------ keys and the mouse
  window.addEventListener('keydown', (e) => {
    if (ui.stage.hidden) return;
    // The stream menu: the app's own shortcut
    if (e.code === 'KeyO' && (e.ctrlKey || e.metaKey) && !e.altKey && !e.shiftKey) {
      e.preventDefault();
      if (hud.editing) hud.setEditing(false);
      menu.toggle(true);
      return;
    }
    if (e.key !== 'Escape' || e.defaultPrevented) return;
    // Escape closes what is in front; the menu, the options and arranging see to themselves
    if (hud.editing || (e.target instanceof Element && e.target.closest('#menu, #options'))) return;
    if (!ui.emoji.hidden) {
      e.preventDefault();
      ui.emoji.hidden = true;
      ui.chatInput.focus();
    } else if (!ui.chat.hidden) {
      e.preventDefault();
      openChat(false);
      syncFront();
    } else if (options.isOpen) {
      e.preventDefault();
      options.close();
    } else if (menu.open && !hud.editing) {
      // An open list first, then the menu
      e.preventDefault();
      menu.back();
    }
  }, true);
  for (const type of ['mousemove', 'mousedown', 'touchstart', 'keydown']) {
    ui.stage.addEventListener(type, wake, { passive: true });
  }
  // A browser that held the sound back lets it play at the next click or key press
  for (const type of ['pointerdown', 'keydown']) {
    window.addEventListener(type, () => sound.wake(), { passive: true });
  }
  ui.screen.addEventListener('dblclick', () => menu.h.onFullscreen());
  document.addEventListener('fullscreenchange', () => {
    if (menu.open) menu.setPicture(prefs.mode, prefs.zoom, !!document.fullscreenElement, prefs.stats, hasPicture());
  });
  window.addEventListener('resize', applyPicture);
  window.addEventListener('gamepadconnected', () => {
    if (touch.visible && touch.auto) setTouch(false, true);
  });
  // The controller went away (battery, out of reach): a phone or tablet gets its controls back,
  // unless the player has switched them by hand
  window.addEventListener('gamepaddisconnected', () => {
    setTimeout(() => {
      if (TouchControls.likely() && !pad.gamepads().length && !touch.visible && touch.auto !== false) setTouch(true, true);
    }, 300);
  });

  // Leaving the page says goodbye, so that the others do not wait for a timeout
  window.addEventListener('pagehide', () => {
    if (session) session.close();
    dualsense.close();
  });
  document.addEventListener('visibilitychange', () => {
    // Coming back to the tab: the picture starts over at once instead of catching up
    if (!document.hidden && session && session.connected) session.requestKeyframe();
    if (!document.hidden && session && navigator.wakeLock) {
      navigator.wakeLock.request('screen').then((lock) => {
        wakeLock = lock;
      }).catch(() => {});
    }
  });

  // For somebody trying things out on their own machine: a look inside from the console
  if (LOCAL) {
    window.psrpt = {
      get session() { return session; },
      get player() { return player; },
      get state() { return lastState; },
      sound,
      pad,
      touch,
      camera,
      dualsense,
      hud,
      menu,
      options,
      hudSettings,
      startCamera,
      sizes: CAMERA_SIZES,
    };
  }

  invite = readInvite();
  if (invite) showJoin();
  else show(ui.landing);
  // A phone or tablet without a controller gets one on the screen
  if (TouchControls.likely() && !pad.gamepads().length) setTouch(true, true);
}

start();
