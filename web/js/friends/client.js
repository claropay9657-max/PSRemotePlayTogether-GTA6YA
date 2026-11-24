// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Friends, chats, groups and status for the browser page (docs/friends.md; the protocol in
// docs/friends-protocol.md). The same client as the app's (gui/src/friends.cpp): the same state,
// the same requests to the friends server (server/friends/psrpt_friends.php), the same answers to
// every note, so that a browser is a device like the app on a computer. Everything sealed and
// opened goes through ./crypto.js.
//
// What a browser does its own way:
// - The state is one object in IndexedDB (./store.js), private keys as CryptoKeys in it. Only the
//   identity's two key pairs and a group's owner key can be exported, because a backup file and
//   a newly linked device carry them; the owner key is kept as the app keeps it (hex).
// - Its card says device kind 2 (a web browser), its status "web": true.
// - It asks the server every 30 seconds while the page is visible, every 2 minutes while not.
// - WebCrypto is asynchronous, so what the app does in one go is guarded here: a conversation is
//   used by one message at a time (its ratchet is stored before the message goes out), the
//   messages to one friend leave in the order they were written, a group's own sender key seals
//   one message at a time, and a group's versions are signed one after the other.
//
// Usage:
//   const friends = new FriendsClient();
//   await friends.setStorage(new IndexedDbStore());
//   friends.addEventListener('change', render);   // also 'request', 'message', 'hosting', 'looked'
//   friends.setProfile(name, jpegBytes);
//   if (!friends.enabled) await friends.enable();
//
// Only here, not in the app: pushKey() and setPush() for the browser's push messages while the
// page is closed (./push.js, docs/friends.md "Web only").
//
// Every method that changes something returns a promise; nothing throws. idle() resolves once
// nothing is under way any more (tests wait for it).

import { base64UrlDecode, base64UrlEncode, bytesEqual, concat, hex, truncateUtf8 } from '../bytes.js';
import { QUOTE_BYTES, mentions } from '../chatmarks.js';
import { MAX_REACTION_KINDS, MAX_REACTIONS_PER_PLAYER, validReaction } from '../proto.js';
import { chatHtml, withEmoji } from './chattext.js';
import * as fc from './crypto.js';
import { publicFromPrivate } from './p256.js';
import { WriteQueue } from './store.js';

const DEFAULT_SERVER = 'https://gamernova.net/api/psrpt_friends.php';
const DEFAULT_PAGE = 'https://crypto90.github.io/PSRemotePlayTogether/';
/** One-time prekeys made at a time, and how few may be left on the server before more go up. */
const PREKEY_BATCH = 50;
const PREKEY_LOW = 20;
/** Private one-time prekeys kept: the ones the server gave out but nobody used go in the end. */
const PREKEYS_KEPT = 300;
/** A status is fresh this long; the own one is put again every minute. */
const STATUS_FRESH_MS = 3 * 60 * 1000;
const STATUS_PUT_MS = 60 * 1000;
/** A friend's status note counts only if it was written this close to now (a server could hand out an old one again). */
const STATUS_MAX_AGE_MS = 10 * 60 * 1000;
const DAY_MS = 24 * 3600 * 1000;
/** Signed prekeys are renewed after a week; the one before stays usable. */
const SPK_RENEW_MS = 7 * DAY_MS;
const MESSAGES_KEPT = 500;
const CARD_CACHE_MS = 10 * 60 * 1000;
const POLL_VISIBLE_MS = 30000;
const POLL_HIDDEN_MS = 120000;
const CALL_TIMEOUT_MS = 15000;
const MAX_ANSWER = 4 * 1024 * 1024;
const DEVICE_KIND_WEB = 2;
/** Conversations kept per other device, and ephemeral keys remembered per other device. */
const SESSIONS_KEPT = 4;
const EKS_KEPT = 64;
/** A message's text, in UTF-8 bytes: with what sealing adds, a note stays under the server's 8 KB. */
const TEXT_MAX_BYTES = 3500;
/** A message's id as a reply names it, and a quote's writer: at most this many characters. */
const REPLY_ID_MAX = 64;
/** Muted until turned back on: a time that never comes. */
export const MUTE_FOREVER = 8640000000000000;
const MAX_GROUP_MEMBERS = 50;
/** A sender key is made anew at least this often (and whenever who is in the group changes). */
const SENDER_KEY_RENEW_MS = 7 * DAY_MS;
/** Group messages whose writer's sender key has not come yet wait this long, at most this many. */
const WAITING_MS = 7 * DAY_MS;
const WAITING_MAX = 200;
/** Linking another device: how often the link box is looked at, and the size of the parts. */
const LINK_POLL_MS = 1500;
const LINK_CHUNK = 192 * 1024;
const LINK_GRACE_MS = 10000;
/** Switching off waits this long at most for the card without this device. */
const DISABLE_WAIT_MS = 10000;

const encoder = new TextEncoder();
const decoder = new TextDecoder();

// ------------------------------------------------------------------ small helpers

const b64 = base64UrlEncode;

/** base64url to bytes; null if it is none, or not `size` bytes long. */
function unb64(text, size) {
  if (typeof text !== 'string') return null;
  const out = base64UrlDecode(text);
  return out && (size === undefined || out.length === size) ? out : null;
}

function unhex(text, size) {
  if (typeof text !== 'string' || text.length % 2 || !/^[0-9a-fA-F]*$/.test(text)) return null;
  const out = new Uint8Array(text.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(text.substr(2 * i, 2), 16);
  return size === undefined || out.length === size ? out : null;
}

function str(v) {
  return typeof v === 'string' ? v : '';
}

function num(v) {
  return typeof v === 'number' && Number.isFinite(v) ? v : 0;
}

function isObject(v) {
  return v !== null && typeof v === 'object' && !Array.isArray(v);
}

function parseObject(text) {
  try {
    const v = JSON.parse(text);
    return isObject(v) ? v : {};
  } catch (e) {
    return {};
  }
}

function randomId() {
  return b64(fc.randomBytes(16));
}

function u32b(v) {
  const out = new Uint8Array(4);
  new DataView(out.buffer).setUint32(0, v >>> 0);
  return out;
}

function u16b(v) {
  const out = new Uint8Array(2);
  new DataView(out.buffer).setUint16(0, v & 0xffff);
  return out;
}

/** Standard base64 (with + / and padding), as Qt's toBase64 writes a picture into the profile. */
function standardBase64(bytes) {
  let s = '';
  for (let i = 0; i < bytes.length; i++) s += String.fromCharCode(bytes[i]);
  return btoa(s);
}

/** Percent-encodes all but the unreserved characters, as QUrl::toPercentEncoding. */
function percentEncode(text) {
  return encodeURIComponent(text).replace(/[!'()*]/g, (c) => '%' + c.charCodeAt(0).toString(16).toUpperCase());
}

function sessionKey(fp, device) {
  return `${fp}:${hex(device)}`;
}

async function tokenHash(token) {
  return b64(await fc.sha256(unb64(token) || new Uint8Array(0)));
}

function sameBytes(a, b) {
  if (!a || !b) return !a && !b;
  return bytesEqual(a, b);
}

/** A promise that gives up after ms (and keeps no timer alive once it is settled). */
function withTimeout(p, ms) {
  let timer = null;
  const late = new Promise((resolve) => {
    timer = setTimeout(resolve, ms);
    if (timer && timer.unref) timer.unref();
  });
  return Promise.race([p, late]).finally(() => clearTimeout(timer));
}

/** The text cut to at most `max` bytes of UTF-8, without half a character at the end. */
function cutUtf8(text, max) {
  const bytes = encoder.encode(text);
  if (bytes.length <= max) return text;
  return decoder.decode(bytes.subarray(0, max)).replace(/�+$/, '');
}

/**
 * Reads "psrpt://friend/FP.LK", "https://.../#f=FP.LK" or "FP.LK", each maybe with "?at=<server>".
 * @returns {{fp: Uint8Array, lk: Uint8Array, at: string}|null}
 */
export function parseFriendLink(text) {
  let s = String(text ?? '').trim();
  const f = s.indexOf('#f=');
  if (f >= 0) s = s.slice(f + 3);
  else if (s.startsWith('psrpt://friend/')) s = s.slice(15);
  let at = '';
  const q = s.indexOf('?');
  if (q >= 0) {
    try {
      at = new URLSearchParams(s.slice(q + 1)).get('at') || '';
    } catch (e) {
      at = '';
    }
    s = s.slice(0, q);
  }
  const parts = s.split('.');
  if (parts.length !== 2) return null;
  const fp = unb64(parts[0], 16);
  const lk = unb64(parts[1], 16);
  return fp && lk ? { fp, lk, at } : null;
}

const COVER = /^https:\/\/image\.api\.playstation\.com\/[A-Za-z0-9._~/%-]{1,300}$/;

/** Trimmed, every run of white space one space, as QString::simplified. */
function simplified(text) {
  return str(text).replace(/\s+/g, ' ').trim();
}

/**
 * A session as a friend shows it (a join card in a chat, the room in a status): only what the
 * app knows to show, each checked; {} if it has no invite. The picture only from Sony's image
 * server, as in the public rooms: any other address would tell whoever runs it who looked.
 */
export function cleanRoom(r) {
  const out = {};
  if (!isObject(r)) return out;
  const invite = str(r.invite);
  if (invite.length > 2000 || !(invite.startsWith('psrpt://') || invite.startsWith('https://'))) return out;
  out.invite = invite;
  const web = str(r.web);
  if (web.length <= 2000 && web.startsWith('https://')) out.web = web;
  for (const name of ['title', 'host', 'game']) {
    const text = simplified(r[name]).slice(0, 80);
    if (text) out[name] = text;
  }
  if (COVER.test(str(r.cover))) out.cover = r.cover;
  if (['ps5', 'ps4', 'pc', 'xbox'].includes(r.console)) out.console = r.console;
  if (['ask', 'watch', 'open'].includes(r.policy)) out.policy = r.policy;
  if (['all', 'turns', 'host', 'each'].includes(r.control)) out.control = r.control;
  const max = Number.isInteger(r.max) ? r.max : 0;
  const players = Number.isInteger(r.players) ? r.players : 0;
  if (max >= 2 && max <= 16 && players >= 1 && players <= max) {
    out.players = players;
    out.max = max;
  }
  if (r.talk === true) out.talk = true;
  return out;
}

// A link preview (made by the writer's app, as in a session's chat; the app's CleanEmbed): the
// texts' limits in UTF-8 bytes (coplay's kMaxChatUrl, kMaxChatSite, …) and the picture's
const MAX_EMBED_URL = 256;
const EMBED_TEXTS = [['site', 48], ['title', 100], ['description', 160]];
const MAX_EMBED_IMAGE = 5600;
const MAX_EMBED_SIDE = 800;

/** The size a JPEG says it has (its frame header), without decoding it; null if it has none. */
export function jpegSize(b) {
  if (!(b instanceof Uint8Array) || b.length < 4 || b[0] !== 0xff || b[1] !== 0xd8) return null;
  let i = 2;
  while (i + 8 < b.length) {
    if (b[i] !== 0xff) return null;
    const marker = b[i + 1];
    if (marker === 0xff) {
      i++;
      continue;
    }
    if (marker === 0x01 || (marker >= 0xd0 && marker <= 0xd7)) {
      i += 2;
      continue;
    }
    // A scan or the end before any frame header: no size
    if (marker === 0xda || marker === 0xd9) return null;
    const length = (b[i + 2] << 8) | b[i + 3];
    if (length < 2) return null;
    if (marker >= 0xc0 && marker <= 0xcf && marker !== 0xc4 && marker !== 0xc8 && marker !== 0xcc) {
      return { height: (b[i + 5] << 8) | b[i + 6], width: (b[i + 7] << 8) | b[i + 8] };
    }
    i += 2 + length;
  }
  return null;
}

/** The first http(s) link in a text (the app's CoPlayLinkPreview::FirstLink), or ''. */
function firstLink(text) {
  const m = /https?:\/\/[^\s<>"']+/i.exec(text);
  if (!m) return '';
  let s = m[0];
  while (s && '.,;:!?)]}'.includes(s.at(-1))) s = s.slice(0, -1);
  return s;
}

function hrefOf(url) {
  try {
    return new URL(url).href;
  } catch (e) {
    return null;
  }
}

/**
 * A link preview as a friend sent it, as the app's CleanEmbed keeps it: only for a link that is
 * in the message itself (as typed, or as the writer's app wrote it, percent-encoded), short
 * texts, and a small JPEG whose size is read from its header. {} if there is nothing to show
 * but the link.
 */
export function cleanEmbed(e, text) {
  const out = {};
  if (!isObject(e)) return out;
  const url = str(e.url);
  if (!url || encoder.encode(url).length > MAX_EMBED_URL || !/^https?:\/\/[^\s<>"']+$/i.test(url)) return out;
  let parsed;
  try {
    parsed = new URL(url);
  } catch (err) {
    return out;
  }
  if ((parsed.protocol !== 'https:' && parsed.protocol !== 'http:') || !parsed.hostname) return out;
  const first = firstLink(str(text));
  if (!str(text).includes(url) && !(first && hrefOf(first) === parsed.href)) return out;
  out.url = url;
  for (const [name, max] of EMBED_TEXTS) {
    const value = decoder.decode(truncateUtf8(simplified(e[name]), max));
    if (value) out[name] = value;
  }
  const image = str(e.image);
  if (image && image.length <= Math.ceil(MAX_EMBED_IMAGE / 3) * 4 && /^[A-Za-z0-9+/]+={0,2}$/.test(image)) {
    let jpeg = null;
    try {
      jpeg = Uint8Array.from(atob(image), (c) => c.charCodeAt(0));
    } catch (err) {
      jpeg = null;
    }
    const size = jpeg && jpeg.length <= MAX_EMBED_IMAGE && jpeg[2] === 0xff ? jpegSize(jpeg) : null;
    if (size && size.width > 0 && size.height > 0 && size.width <= MAX_EMBED_SIDE && size.height <= MAX_EMBED_SIDE) out.image = image;
  }
  if (!out.title && !out.description && !out.image) return {};
  return out;
}

/** A plan as sent: a title and 1 to 8 times (ms); null if it has no time. */
export function cleanPlan(p) {
  if (!isObject(p)) return null;
  const times = [];
  for (const t of Array.isArray(p.times) ? p.times : []) {
    if (typeof t === 'number' && Number.isFinite(t) && t > 0 && times.length < 8) times.push(t);
  }
  if (!times.length) return null;
  return { title: simplified(p.title).slice(0, 80), times };
}

/**
 * What a text answers, as received (docs/friends-protocol.md, "Replies"): `reply` only as a
 * string of at most 64 characters, `quote` only with both fields, `from` at most 64 characters,
 * its text on one line and cut to 160 bytes of UTF-8. {} if neither.
 */
export function cleanReply(p) {
  const out = {};
  if (!isObject(p)) return out;
  if (typeof p.reply === 'string' && p.reply.length > 0 && p.reply.length <= REPLY_ID_MAX) out.reply = p.reply;
  const q = p.quote;
  if (isObject(q) && typeof q.from === 'string' && q.from.length > 0 && q.from.length <= REPLY_ID_MAX && typeof q.text === 'string') {
    out.quote = { from: q.from, text: cutUtf8(simplified(q.text), QUOTE_BYTES) };
  }
  return out;
}

/** The start of a message as a reply quotes it: on one line, at most 160 bytes (a card or plan: its text). */
function quoteOf(m) {
  return cutUtf8(simplified(str(m.text)), QUOTE_BYTES);
}

/** A plan's answers as sent: one per time, 0 no, 1 maybe, 2 yes (anything else counts as no). */
export function cleanAnswers(v, times) {
  const a = Array.isArray(v) ? v : [];
  const out = [];
  for (let i = 0; i < times; i++) {
    const x = i < a.length ? a[i] : 0;
    out.push(Number.isInteger(x) ? Math.min(2, Math.max(0, x)) : 0);
  }
  return out;
}

/** Fingerprints from a list: each once, each a real one. */
export function fpsOf(v) {
  const out = [];
  for (const x of Array.isArray(v) ? v : []) {
    if (typeof x === 'string' && unb64(x, 16) && !out.includes(x)) out.push(x);
  }
  return out;
}

function sameSet(a, b) {
  return a.length === b.length && [...a].sort().join(',') === [...b].sort().join(',');
}

/**
 * What an admin signs for a version of a group: "psrpt group" || id || u32 version || u16 name ||
 * u8 add (1: everybody may add people) || members, admins, banned (each u8 count and the
 * fingerprints in the order of their bytes; base64url text sorts differently).
 */
export function groupSigned(gid, version, name, add, members, admins, banned) {
  const n = encoder.encode(name).slice(0, 200);
  const parts = [encoder.encode('psrpt group'), gid, u32b(version), u16b(n.length), n, new Uint8Array([add === 'all' ? 1 : 0])];
  for (const list of [members, admins, banned]) {
    const ids = list.map((fp) => unb64(fp, 16) || new Uint8Array(16)).sort(fc.compareBytes);
    parts.push(new Uint8Array([ids.length]), ...ids);
  }
  return concat(...parts);
}

/** A key pair from its private scalar (hex), as the app keeps group owner keys; null if it is none. */
async function pairFromHex(hexD, usage) {
  const d = unhex(hexD, 32);
  const pub = d ? await publicFromPrivate(d) : null;
  return pub ? fc.importKeyPair(concat(d, pub), { usage }) : null;
}

/** An identity from a backup: the private scalars as hex (as the app writes them), or d || pub. */
async function importIdentity(stored) {
  const pair = async (text, usage) => {
    let data = unhex(text);
    if (data && data.length === 32) {
      const pub = await publicFromPrivate(data);
      if (!pub) return null;
      data = concat(data, pub);
    }
    return data && data.length === 97 ? fc.importKeyPair(data, { usage, extractable: true }) : null;
  };
  const iks = await pair(stored.iks, 'ecdsa');
  const ikd = await pair(stored.ikd, 'ecdh');
  return iks && ikd ? fc.completeIdentity({ iks, ikd }) : null;
}

/** Somebody else's sender key as kept: the chain key and its skipped message keys in hex. */
function saveTheirChain(c) {
  const skipped = {};
  for (const [i, mk] of c.skipped) skipped[String(i)] = hex(mk);
  return { chain: c.chain, iter: c.iter, ck: hex(c.ck), pub: b64(c.signPub), skipped };
}

function loadTheirChain(c) {
  if (!isObject(c)) return null;
  const ck = unhex(c.ck, 32);
  const signPub = unb64(c.pub, 65);
  if (!ck || !signPub) return null;
  const skipped = new Map();
  for (const [k, v] of Object.entries(isObject(c.skipped) ? c.skipped : {})) {
    const mk = unhex(v, 32);
    if (mk) skipped.set(Number(k) >>> 0, mk);
  }
  return { chain: num(c.chain) >>> 0, iter: num(c.iter) >>> 0, ck, signPub, skipped };
}

/** A friend's picture as a data: address; only base64, which nothing in a page can read as more than a picture. */
function pictureUrl(avatar) {
  const text = str(avatar);
  return text && /^[A-Za-z0-9+/]+={0,2}$/.test(text) ? `data:image/jpeg;base64,${text}` : '';
}

function avatarUrl(f) {
  return pictureUrl(f.avatar);
}

/** A friend record's name, as the app reads it: the record's own if it has one, else the fallback. */
function nameOr(record, fallback) {
  return isObject(record) && typeof record.name === 'string' ? record.name : fallback;
}

// ------------------------------------------------------------------ the client

export class FriendsClient extends EventTarget {
  #fetch;
  #timers;
  #store = null;
  #queue = null;
  #server = DEFAULT_SERVER;
  #enabled = false;
  /** The store is another tab's: nothing is switched on here. */
  #elsewhere = false;
  /** Switched off: what is left of the state is not written any more. */
  #discard = false;
  /** Counts up with every switch on or off: a late clean-up never touches a newer identity. */
  #generation = 0;
  #online = false;
  #problem = '';
  #background = false;
  #myState = 'online';
  #myFree = '';
  #activity = '';
  #room = null;
  #profileName = '';
  #profileAvatar = null;
  /** Everything kept: the app's state object, with CryptoKeys where the app has hex. */
  #state = {};
  /** Friends' statuses as last read (fp -> status), when each card was fetched (fp -> ms): not kept. */
  #statuses = new Map();
  #cardFetched = new Map();
  /** Cards read before (base64url -> promise of the card), and the safety codes (fp -> text). */
  #decoded = new Map();
  #safety = new Map();
  /** The own devices, from the newest own card known: each {id, kind, me}. */
  #devicesList = [];
  /** Seconds this clock is behind the server's, once the server said so. */
  #clockOffset = 0;
  #statusInFlight = false;
  #statusAgain = false;
  /** The status note was taken away (invisible, or shown to nobody). */
  #statusHidden = false;
  #publishing = null;
  #publishAgain = false;
  #toppingUp = false;
  #polling = null;
  #pollAgain = false;
  #busy = 0;
  #idleWaiters = [];
  #lockTail = Promise.resolve();
  #lines = new Map();
  #pollTimer = null;
  #statusTimer = null;
  #onVisibility = null;
  /** Linking another device, while it runs: {side, state, code, text, secret, ek, key, commit, box, busy, timer}. */
  #link = null;
  /** Counts the sender keys that came: a group message waiting for one is tried again. */
  #keysCame = 0;
  /** The push subscription last given to the server, and the group boxes it was given with. */
  #pushSub = null;
  #pushGroups = '';

  /**
   * @param {{fetch?: Function, timers?: boolean}} options timers: false asks the server only on
   *   refresh() and puts the status only on changes (tests)
   */
  constructor({ fetch: fetchFn = null, timers = true } = {}) {
    super();
    this.#fetch = fetchFn || ((...args) => globalThis.fetch(...args));
    this.#timers = timers;
  }

  // ---------------------------------------------------------------- setup

  /** Where the state is kept (./store.js); reads it, and starts asking the server if friends are on. */
  setStorage(store) {
    return this.#task(async () => {
      this.#stopTimers();
      this.#store = store;
      this.#queue = store ? new WriteQueue(store, () => this.#state) : null;
      this.#state = {};
      this.#enabled = false;
      this.#discard = false;
      this.#statusHidden = false;
      this.#devicesList = [];
      this.#statuses.clear();
      this.#safety.clear();
      this.#cardFetched.clear();
      this.#elsewhere = false;
      if (store && store.claim && !(await store.claim())) {
        // Another tab is this browser's device: this one neither reads nor writes the state
        this.#elsewhere = true;
        this.#queue = null;
        this.#problem = 'Friends are open in another tab of this browser.';
        this.#changed();
        return;
      }
      let loaded = null;
      try {
        loaded = store ? await store.load() : null;
      } catch (e) {
        this.#problem = 'The friends of this browser cannot be read.';
        this.#changed();
        return;
      }
      if (isObject(loaded)) {
        this.#state = loaded;
        this.#enabled = isObject(loaded.identity) && !!loaded.identity.iks;
        this.#server = str(loaded.server) || DEFAULT_SERVER;
        const settings = isObject(loaded.settings) ? loaded.settings : {};
        this.#myState = str(settings.state) || 'online';
        this.#myFree = str(settings.free);
      }
      if (this.#enabled) {
        this.#startTimers();
        if (this.#timers) {
          this.#poll();
          this.#spawn(this.#putStatus());
        }
        this.#spawn(this.#refreshDevices());
      }
      this.#changed();
    });
  }

  /** The friends server; empty or blank for the default one. */
  setServer(url) {
    return this.#task(async () => {
      const trimmed = String(url ?? '').trim();
      const next = trimmed || DEFAULT_SERVER;
      if (next === this.#server) return;
      this.#server = next;
      if (this.#enabled) {
        // Everything starts again on the other server: the card, the prekeys, the boxes
        this.#state.server = this.server;
        delete this.#state.device.next_opk;
        this.#cardFetched.clear();
        this.#statuses.clear();
        await this.#save();
        this.#publishCard();
      }
      this.#changed();
    });
  }

  /** The name and picture (JPEG bytes, at most 4 KB) friends see. */
  setProfile(name, avatarJpeg = null) {
    const n = String(name ?? '').slice(0, 32);
    // The card carries a small picture only (a whole request has to fit in 8 KB)
    const a = avatarJpeg instanceof Uint8Array && avatarJpeg.length && avatarJpeg.length <= 4096 ? avatarJpeg.slice() : null;
    if (n === this.#profileName && sameBytes(a, this.#profileAvatar)) return Promise.resolve();
    this.#profileName = n;
    this.#profileAvatar = a;
    return this.#enabled ? this.#publishCard() : Promise.resolve();
  }

  /** What this browser is doing, for the status: "In a session with friends", a room to join (or null). */
  setActivity(text, room = null) {
    const t = String(text ?? '');
    const clean = isObject(room) && Object.keys(room).length ? cleanRoom(room) : {};
    const r = Object.keys(clean).length ? clean : null;
    if (t === this.#activity && JSON.stringify(r) === JSON.stringify(this.#room)) return Promise.resolve();
    this.#activity = t;
    this.#room = r;
    return this.#enabled ? this.#task(() => this.#putStatus()) : Promise.resolve();
  }

  /** Asking the server less often, as for a hidden page. */
  setBackground(inBackground) {
    this.#background = !!inBackground;
    if (this.#pollTimer !== null) this.#schedulePoll();
  }

  /** Stops asking the server (the state stays as it is). */
  close() {
    this.#stopTimers();
    this.cancelLinking();
    if (this.#store && this.#store.close) this.#store.close();
  }

  // ---------------------------------------------------------------- properties

  static get defaultServer() {
    return DEFAULT_SERVER;
  }

  get defaultServer() {
    return DEFAULT_SERVER;
  }

  get enabled() {
    return this.#enabled;
  }

  get online() {
    return this.#online;
  }

  get problem() {
    return this.#problem;
  }

  /** The friends server, empty for the default one. */
  get server() {
    return this.#server === DEFAULT_SERVER ? '' : this.#server;
  }

  /** Work under way (requests, sealing, storing). */
  get pending() {
    return this.#busy;
  }

  /** The name this player goes by (the profile's), which "@" mentions them with. */
  get profileName() {
    return this.#profileName;
  }

  get fingerprint() {
    return this.#enabled ? str(this.#state.fp) : '';
  }

  #linkPart() {
    const lk = unhex(this.#state.lk, 16) || new Uint8Array(0);
    let s = `${this.#state.fp}.${b64(lk)}`;
    if (this.#server !== DEFAULT_SERVER) s += `?at=${percentEncode(this.#server)}`;
    return s;
  }

  /** The own friend link for the app. */
  get link() {
    return this.#enabled ? `psrpt://friend/${this.#linkPart()}` : '';
  }

  /** The own friend link for a web browser. */
  get webLink() {
    return this.#enabled ? `${DEFAULT_PAGE}#f=${this.#linkPart()}` : '';
  }

  /** Linking another device: "" (not), "waiting", "code" (both show linkCode), "sending", "receiving", "done", "failed". */
  get linkState() {
    return this.#link ? this.#link.state : '';
  }

  /** The six digits both devices show, to compare. */
  get linkCode() {
    return this.#link ? this.#link.code : '';
  }

  /** On the device that has the friends: what the new device needs (psrpt://link/...). */
  get linkText() {
    return this.#link ? this.#link.text : '';
  }

  /** The devices of this identity: id (hex), kind (1 the app, 2 a web browser), me (this one). */
  get devices() {
    return this.#enabled ? this.#devicesList.map((d) => ({ ...d })) : [];
  }

  /** Each: fp, name, avatar (data URL), state (online, away, busy, web, offline), activity, room, free, checked, safety. */
  get friends() {
    const out = [];
    if (!this.#enabled) return out;
    const now = Date.now();
    for (const [fp, f] of Object.entries(this.#friendsObj())) {
      if (f.state !== 'friend') continue;
      const st = this.#statuses.get(fp);
      const fresh = !!st && now - num(st.seen) < STATUS_FRESH_MS;
      let s = fresh ? str(st.state) || 'online' : 'offline';
      if (fresh && st.web === true && s === 'online') s = 'web';
      out.push({
        fp, name: str(f.name), avatar: avatarUrl(f), state: s,
        activity: fresh ? str(st.activity) : '',
        room: fresh && isObject(st.room) ? { ...st.room } : null,
        free: fresh ? str(st.free) : '',
        checked: f.checked === true,
        safety: this.#safety.get(fp) || '',
      });
    }
    // Who plays or is online first, then by name
    out.sort((x, y) => {
      const xo = x.state !== 'offline';
      const yo = y.state !== 'offline';
      if (xo !== yo) return xo ? -1 : 1;
      const a = x.name.toLowerCase();
      const b = y.name.toLowerCase();
      return a < b ? -1 : a > b ? 1 : 0;
    });
    return out;
  }

  /** Each: fp, name, avatar, text, incoming, unsent, at. */
  get requests() {
    const out = [];
    if (!this.#enabled) return out;
    for (const [fp, f] of Object.entries(this.#friendsObj())) {
      if (f.state !== 'incoming' && f.state !== 'requested') continue;
      out.push({ fp, name: str(f.name), avatar: avatarUrl(f), text: str(f.text), incoming: f.state === 'incoming', unsent: f.unsent === true,
        at: num(f.since) });
    }
    return out;
  }

  /** Each: fp, name. */
  get blocked() {
    if (!this.#enabled) return [];
    return Object.entries(this.#friendsObj()).filter(([, f]) => f.state === 'blocked').map(([fp, f]) => ({ fp, name: str(f.name) }));
  }

  /**
   * Each: id, kind ("friend" or "group"), title, avatar, last, at, unread, mentions (of the unread,
   * how many name this player), muted and mutedUntil, and for a group also members (how many) and
   * left; the newest first. Every group shows, also before its first message.
   */
  get chats() {
    const out = [];
    if (!this.#enabled) return out;
    const chats = isObject(this.#state.chats) ? this.#state.chats : {};
    const friends = this.#friendsObj();
    const groups = isObject(this.#state.groups) ? this.#state.groups : {};
    const ids = Object.keys(chats);
    for (const gid of Object.keys(groups)) if (!ids.includes(gid)) ids.push(gid);
    for (const id of ids) {
      const c = isObject(chats[id]) ? chats[id] : {};
      const msgs = Array.isArray(c.m) ? c.m : [];
      const g = isObject(groups[id]) ? groups[id] : null;
      if (!msgs.length && !g) continue;
      const last = msgs.length ? msgs[msgs.length - 1] : null;
      const f = isObject(friends[id]) ? friends[id] : {};
      const entry = { id, kind: g ? 'group' : 'friend', title: g ? str(g.name) : str(f.name), avatar: g ? '' : avatarUrl(f) };
      if (g) {
        entry.members = Array.isArray(g.members) ? g.members.length : 0;
        entry.left = g.left === true;
      }
      const card = last && isObject(last.join) ? last.join : null;
      const cardTitle = card && typeof card.title === 'string' ? card.title : 'a session';
      let line = !last ? 'No messages yet'
        : last.deleted === true ? 'Message deleted'
          : !card ? str(last.text)
            : last.mine === true ? `You invited them to ${cardTitle}` : `Invites you to ${cardTitle}`;
      // In a group: who wrote it
      if (g && last && last.system !== true) {
        const from = str(last.from);
        const names = isObject(g.names) ? g.names : {};
        const who = last.mine === true ? 'You' : nameOr(friends[from], str(names[from]));
        if (who) line = `${who}: ${line}`;
      }
      entry.last = line;
      entry.at = last ? num(last.at) : num(g.made);
      const seen = num(c.seen);
      // A stand-in for a deleted message is nothing to read
      const unread = msgs.filter((m) => m.mine !== true && m.system !== true && m.deleted !== true && num(m.at) > seen);
      entry.unread = unread.length;
      entry.mentions = unread.filter((m) => mentions(str(m.text), this.#profileName)).length;
      entry.muted = num(c.mute) > Date.now();
      entry.mutedUntil = entry.muted ? num(c.mute) : 0;
      out.push(entry);
    }
    out.sort((a, b) => b.at - a.at);
    return out;
  }

  /** What is new to read, in all chats; a muted chat counts only the messages that name this player. */
  get unread() {
    return this.chats.reduce((n, c) => n + (c.muted ? c.mentions : c.unread), 0);
  }

  get myState() {
    return this.#myState;
  }

  set myState(s) {
    this.setMyState(s);
  }

  get myFree() {
    return this.#myFree;
  }

  set myFree(text) {
    this.setMyFree(text);
  }

  get showStatus() {
    return str(this.#settings().show_status) || 'all';
  }

  set showStatus(who) {
    this.setShowStatus(who);
  }

  get showGame() {
    const v = this.#settings().show_game;
    return typeof v === 'boolean' ? v : true;
  }

  set showGame(on) {
    this.setShowGame(on);
  }

  /** On or off: "always" or "never"; "ask" from older states counts as on. */
  get friendsJoin() {
    return str(this.#settings().friends_join) === 'never' ? 'never' : 'always';
  }

  set friendsJoin(who) {
    this.setFriendsJoin(who);
  }

  /** Read receipts, both ways: off sends none and shows none. */
  get readReceipts() {
    const v = this.#settings().read_receipts;
    return typeof v === 'boolean' ? v : true;
  }

  set readReceipts(on) {
    this.setReadReceipts(on);
  }

  setMyState(s) {
    if (s === this.#myState || !['online', 'away', 'busy', 'invisible'].includes(s)) return Promise.resolve();
    this.#myState = s;
    return this.#settingChanged();
  }

  setMyFree(text) {
    const t = String(text ?? '');
    if (t === this.#myFree) return Promise.resolve();
    this.#myFree = t.slice(0, 80);
    return this.#settingChanged();
  }

  setShowStatus(who) {
    this.#settings(true).show_status = who === 'nobody' ? 'nobody' : 'all';
    return this.#settingChanged();
  }

  setShowGame(on) {
    this.#settings(true).show_game = !!on;
    return this.#settingChanged();
  }

  setFriendsJoin(who) {
    this.#settings(true).friends_join = who === 'never' ? 'never' : 'always';
    return this.#settingChanged();
  }

  setReadReceipts(on) {
    this.#settings(true).read_receipts = !!on;
    this.#changed();
    return this.#save();
  }

  #settings(make = false) {
    if (isObject(this.#state.settings)) return this.#state.settings;
    if (!make) return {};
    this.#state.settings = {};
    return this.#state.settings;
  }

  #settingChanged() {
    this.#changed();
    return this.#task(async () => {
      await this.#save();
      await this.#putStatus();
    });
  }

  // ---------------------------------------------------------------- on and off

  /** Switches friends on: makes the identity and this browser's keys, and puts the card on the server. */
  enable() {
    return this.#task(async () => {
      if (this.#enabled || this.#elsewhere) return;
      let id;
      try {
        id = await fc.makeIdentity();
      } catch (e) {
        this.#problem = 'No keys could be made in this browser.';
        this.#changed();
        return;
      }
      if (this.#enabled) return;
      this.#generation++;
      this.#discard = false;
      this.#statusHidden = false;
      this.#state = {
        v: 1,
        identity: id,
        fp: b64(id.fp),
        server: this.server,
        device: { id: hex(fc.randomBytes(8)) },
        lk: hex(fc.randomBytes(16)),
        // The friends key for statuses: a new epoch now
        fk: { epoch: 1, key: hex(fc.randomBytes(32)), start: Date.now() },
        // A token for the own devices' inboxes
        self_token: b64(fc.randomBytes(16)),
      };
      this.#statuses.clear();
      this.#safety.clear();
      this.#cardFetched.clear();
      this.#devicesList = [];
      this.#enabled = true;
      this.#problem = '';
      await this.#save();
      this.#publishCard();
      this.#startTimers();
      this.#changed();
    });
  }

  /**
   * Switches friends off in this browser: off at once, then this browser leaves the card (so
   * that friends stop writing to it, 10 seconds at most), then everything here is forgotten.
   */
  disable() {
    this.cancelLinking();
    const generation = ++this.#generation;
    const own = this.#enabled && isObject(this.#state.device) ? str(this.#state.device.id) : '';
    this.#stopTimers();
    this.#enabled = false;
    this.#discard = true;
    this.#pushSub = null;
    this.#changed();
    return this.#task(async () => {
      if (own) await withTimeout(this.#publishOnce(own), DISABLE_WAIT_MS);
      // Never over an identity made since
      if (generation !== this.#generation) return;
      this.#state = {};
      this.#statuses.clear();
      this.#safety.clear();
      this.#cardFetched.clear();
      this.#devicesList = [];
      if (this.#queue) {
        try {
          await this.#queue.clear();
        } catch (e) {
          this.#problem = 'The friends of this browser cannot be removed.';
        }
      }
      this.#changed();
    });
  }

  /** Asks the server now: requests, messages, groups, statuses. Resolves once all of it is read. */
  refresh() {
    return this.#enabled ? this.#poll() : Promise.resolve();
  }

  /** Resolves once nothing is under way any more. */
  idle() {
    if (this.#busy === 0) return Promise.resolve();
    return new Promise((resolve) => this.#idleWaiters.push(resolve));
  }

  // ---------------------------------------------------------------- the own devices

  /** Another device of this identity off the card: friends no longer write to it. */
  removeDevice(id) {
    if (!this.#enabled || !id || id === str(isObject(this.#state.device) ? this.#state.device.id : '')) return Promise.resolve();
    return this.#task(async () => {
      await this.#publishCard(id);
      this.#changed();
    });
  }

  /** The own devices from the newest own card known: the one this device put, or one fetched since. */
  async #refreshDevices() {
    const put = await this.#decode(str(this.#state.card));
    const fetched = await this.#decode(str(isObject(this.#state.cards) ? this.#state.cards[this.#state.fp] : ''));
    let card = put && b64(put.fp) === this.#state.fp ? put : null;
    if (fetched && b64(fetched.fp) === this.#state.fp && (!card || fetched.seq > card.seq)) card = fetched;
    const mine = str(isObject(this.#state.device) ? this.#state.device.id : '');
    const list = card ? card.devices.map((d) => ({ id: hex(d.id), kind: d.kind, me: hex(d.id) === mine })) : [];
    if (JSON.stringify(list) === JSON.stringify(this.#devicesList)) return;
    this.#devicesList = list;
    this.#changed();
  }

  /** Of this identity's devices, the one that acts where only one may (the lowest id). */
  #actingDevice() {
    const mine = str(isObject(this.#state.device) ? this.#state.device.id : '');
    return !this.#devicesList.some((d) => d.id < mine);
  }

  // ---------------------------------------------------------------- friend links and requests

  /**
   * Looks a friend link up: { ok, fp, name, avatar, error }, also as a 'looked' event.
   */
  lookUp(link) {
    return this.#task(async () => {
      const looked = (ok, fp, name, avatar, error) => {
        const detail = { ok, fp, name, avatar, error };
        this.#emit('looked', detail);
        return detail;
      };
      const parsed = parseFriendLink(link);
      if (!this.#enabled || !parsed) return looked(false, '', '', '', 'That is no friend link.');
      const fp = b64(parsed.fp);
      // Friends meet on one server: a link from another one leads nowhere here
      if ((parsed.at || DEFAULT_SERVER) !== this.#server) {
        return looked(false, fp, '', '',
          `This link is for another friends server (${parsed.at || 'the default one'}). Both of you need the same one: Settings → Friends.`);
      }
      if (fp === this.#state.fp) return looked(false, fp, '', '', 'That is your own friend link.');
      const { ok, answer } = await this.#call('card_get', { fp });
      const card = ok ? await this.#decode(answer.card) : null;
      const json = card && bytesEqual(card.fp, parsed.fp) ? await fc.openProfile(parsed.lk, parsed.fp, card.profile) : null;
      if (json === null) return looked(false, fp, '', '', 'Nobody uses this link any more.');
      const profile = parseObject(json);
      return looked(true, fp, str(profile.name).slice(0, 32), pictureUrl(profile.avatar), '');
    });
  }

  /** Asks the owner of a friend link to become friends. */
  sendRequest(link, text) {
    return this.#task(async () => {
      const parsed = parseFriendLink(link);
      if (!this.#enabled || !parsed || (parsed.at || DEFAULT_SERVER) !== this.#server) return;
      const key = b64(parsed.fp);
      if (key === this.#state.fp) return;
      const friends = this.#friendsObj();
      const before = friends[key] || {};
      if (before.state === 'friend') return;
      // Sent again: the token handed out with the request before goes
      if (str(before.my_token)) this.#removeToken(before.my_token);
      const words = String(text ?? '').slice(0, 200);
      const myToken = this.#newToken(key);
      const record = { ...before, state: 'requested', lk: hex(parsed.lk), my_token: myToken, text: words, since: Date.now() };
      delete record.unsent;
      friends[key] = record;
      this.#changed();
      await this.#save();
      // On the server before the request goes: the answer comes with it
      await this.#call('token_add', { hash: await tokenHash(myToken) }, true);
      this.#syncOwn(key);
      const payload = { t: 'request', lk: b64(unhex(this.#state.lk, 16)), text: words, token: myToken };
      const unsent = async () => {
        const f = this.#friendsObj()[key];
        if (!f || f.state !== 'requested') return;
        f.unsent = true;
        this.#changed();
        await this.#save();
      };
      // Their card first: their name for the list, and the devices the request goes to
      const card = await this.#withCard(key);
      if (!card) {
        await unsent();
        return;
      }
      await this.#refreshProfile(key, card);
      this.#changed();
      await this.#save();
      if (!(await this.#sendPayload(key, payload, true))) await unsent();
    });
  }

  /** A request that did not go out (or got no answer), once more. */
  resendRequest(fp) {
    const f = this.#friendsObj()[fp];
    const lk = f && unhex(f.lk, 16);
    if (!f || f.state !== 'requested' || !lk) return Promise.resolve();
    // The link as it was, on this client's server
    let link = `psrpt://friend/${fp}.${b64(lk)}`;
    if (this.#server !== DEFAULT_SERVER) link += `?at=${percentEncode(this.#server)}`;
    return this.sendRequest(link, str(f.text));
  }

  accept(fp) {
    return this.#task(async () => {
      const f = this.#friendsObj()[fp];
      if (!f || f.state !== 'incoming') return;
      const myToken = this.#newToken(fp);
      f.state = 'friend';
      f.my_token = myToken;
      f.since = Date.now();
      this.#changed();
      await this.#save();
      await this.#call('token_add', { hash: await tokenHash(myToken) }, true);
      const fk = isObject(this.#state.fk) ? this.#state.fk : {};
      const sending = this.#sendPayload(fp, { t: 'accept', lk: b64(unhex(this.#state.lk, 16)), token: myToken,
        fk: { key: fk.key, epoch: fk.epoch } }, true);
      this.#syncOwn(fp);
      this.#spawn(this.#putStatus());
      await sending;
    });
  }

  decline(fp) {
    return this.#task(async () => {
      const friends = this.#friendsObj();
      const f = friends[fp];
      const sending = f && f.state === 'incoming' ? this.#sendPayload(fp, { t: 'decline' }, true) : null;
      delete friends[fp];
      this.#syncOwn(fp);
      this.#changed();
      await this.#save();
      if (sending) await sending;
    });
  }

  unfriend(fp) {
    return this.#task(async () => {
      const friends = this.#friendsObj();
      const f = friends[fp];
      if (!f) return;
      const sending = this.#sendPayload(fp, { t: 'unfriend' }, true);
      if (str(f.my_token)) this.#removeToken(f.my_token);
      delete friends[fp];
      this.#newFriendsEpoch();
      this.#statuses.delete(fp);
      // Everybody else gets the new key, so the removed one reads no status any more
      for (const [other, g] of Object.entries(friends)) if (g.state === 'friend') this.#sendFriendsKey(other);
      this.#syncOwn(fp, true);
      this.#changed();
      await this.#save();
      this.#spawn(this.#putStatus());
      await sending;
    });
  }

  block(fp) {
    return this.#task(async () => {
      const friends = this.#friendsObj();
      const f = friends[fp] || {};
      if (str(f.my_token)) this.#removeToken(f.my_token);
      const wasFriend = f.state === 'friend';
      const blocked = { state: 'blocked' };
      if (f.name !== undefined) blocked.name = f.name;
      friends[fp] = blocked;
      // Their conversations go
      const sessions = isObject(this.#state.sessions) ? this.#state.sessions : {};
      for (const k of Object.keys(sessions)) if (k.startsWith(`${fp}:`)) delete sessions[k];
      if (wasFriend) this.#newFriendsEpoch();
      this.#statuses.delete(fp);
      if (wasFriend) {
        for (const [other, g] of Object.entries(friends)) if (g.state === 'friend') this.#sendFriendsKey(other);
        this.#spawn(this.#putStatus());
      }
      this.#syncOwn(fp, wasFriend);
      this.#changed();
      await this.#save();
    });
  }

  /** They may send requests again (they become no friend by it). */
  unblock(fp) {
    return this.#task(async () => {
      const friends = this.#friendsObj();
      if (!friends[fp] || friends[fp].state !== 'blocked') return;
      delete friends[fp];
      this.#syncOwn(fp);
      this.#changed();
      await this.#save();
    });
  }

  setChecked(fp, checked) {
    return this.#task(async () => {
      const f = this.#friendsObj()[fp];
      if (!f) return;
      f.checked = !!checked;
      this.#syncOwn(fp);
      this.#changed();
      await this.#save();
    });
  }

  /** A new link: the old one stops working, the friends stay. */
  newLink() {
    return this.#task(async () => {
      if (!this.#enabled) return;
      const lk = fc.randomBytes(16);
      this.#state.lk = hex(lk);
      await this.#save();
      // The card's profile is sealed with the new key; the friends get it to keep reading it
      this.#publishCard();
      for (const [fp, f] of Object.entries(this.#friendsObj())) {
        if (f.state === 'friend') this.#spawn(this.#sendPayload(fp, { t: 'lk', lk: b64(lk) }, false));
      }
      this.#syncOwn('', false, true);
      this.#changed();
    });
  }

  #newToken(fp) {
    const token = b64(fc.randomBytes(16));
    if (!isObject(this.#state.issued)) this.#state.issued = {};
    this.#state.issued[token] = fp;
    return token;
  }

  /** A write token for this identity's inboxes taken back. */
  #removeToken(token) {
    this.#spawn(tokenHash(token).then((hash) => this.#call('token_remove', { hash }, true)));
  }

  /** A new friends key epoch: the one before stays with whoever was removed. */
  #newFriendsEpoch() {
    const fk = isObject(this.#state.fk) ? this.#state.fk : {};
    this.#state.fk = { ...fk, epoch: num(fk.epoch) + 1, key: hex(fc.randomBytes(32)), start: Date.now() };
  }

  #sendFriendsKey(fp) {
    const fk = isObject(this.#state.fk) ? this.#state.fk : {};
    this.#spawn(this.#sendPayload(fp, { t: 'fk', key: fk.key, epoch: fk.epoch }, false));
  }

  /** Tells the own other devices about a changed friend, the friends key or the link key. */
  #syncOwn(friendFp = '', fk = false, lk = false) {
    if (!this.#enabled) return;
    const p = { t: 'own' };
    if (friendFp) {
      const friends = this.#friendsObj();
      const record = { ...(friends[friendFp] || {}) };
      // The picture comes from their card on the other device too
      delete record.avatar;
      record.fp = friendFp;
      if (!(friendFp in friends)) record.removed = true;
      p.friend = record;
    }
    if (fk) p.fk = this.#state.fk;
    if (lk) p.lk = this.#state.lk;
    this.#spawn(this.#sendPayload(this.#state.fp, p, false));
  }

  // ---------------------------------------------------------------- chats

  /**
   * The messages of a chat, oldest first: id, from, name, mine, text, html, at, state ("sending",
   * "sent", "failed"), join, plan and votes, system (a note of the group), and on the own
   * messages read (a friend's chat) or readBy (how many in a group) while read receipts are on.
   */
  messages(chat) {
    if (!this.#enabled) return [];
    const friends = this.#friendsObj();
    const g = this.#group(chat);
    const names = g && isObject(g.names) ? g.names : {};
    const c = isObject(this.#state.chats) ? this.#state.chats[chat] : null;
    const msgs = c && Array.isArray(c.m) ? c.m : [];
    const read = c && isObject(c.read) ? c.read : {};
    const receipts = this.readReceipts;
    const me = this.#state.fp;
    const byId = new Map(msgs.map((m) => [str(m.id), m]));
    const nameOf = (fp) => (fp === me ? this.#profileName : nameOr(friends[fp], str(names[fp])));
    return msgs.map((m) => {
      const out = { ...m };
      const from = str(m.from);
      out.name = m.mine === true ? this.#profileName : nameOr(friends[from], str(names[from]));
      out.html = chatHtml(str(m.text));
      // What it answers: the message itself while the chat has it, else the copy, else only that it was earlier
      if (m.deleted !== true && (m.reply || m.quote)) {
        const original = m.reply ? byId.get(str(m.reply)) : null;
        if (original) {
          const of = str(original.from);
          out.quoted = { id: str(m.reply), from: of, name: original.mine === true ? this.#profileName : nameOf(of),
            text: original.deleted === true ? 'Deleted message' : quoteOf(original), deleted: original.deleted === true, found: true };
        } else if (isObject(m.quote)) {
          out.quoted = { id: str(m.reply), from: str(m.quote.from), name: nameOf(str(m.quote.from)), text: str(m.quote.text),
            deleted: false, found: false };
        } else out.quoted = { id: str(m.reply), from: '', name: '', text: 'A message from earlier', deleted: false, found: false };
      }
      // Somebody else's message that names this player
      out.mention = m.mine !== true && m.system !== true && m.deleted !== true && mentions(str(m.text), this.#profileName);
      if (isObject(m.plan)) out.plan = { ...m.plan, times: [...(m.plan.times || [])] };
      if (isObject(m.votes)) out.votes = Object.fromEntries(Object.entries(m.votes).map(([k, v]) => [k, [...v]]));
      if (isObject(m.reactions)) out.reactions = Object.fromEntries(Object.entries(m.reactions).map(([k, v]) => [k, [...v]]));
      // The preview's picture as the chat shows it
      if (isObject(m.embed)) out.embed = { ...m.embed, image: m.embed.image ? `data:image/jpeg;base64,${m.embed.image}` : '' };
      // Read: by the friend, or by how many in a group (neither with read receipts off)
      if (m.mine === true && receipts) {
        const at = num(m.at);
        if (!g) out.read = num(read[chat]) >= at;
        else out.readBy = Object.entries(read).filter(([fp, v]) => fp !== me && num(v) >= at).length;
      }
      return out;
    });
  }

  /**
   * A text message: emoji for :shortcodes:, at most 3500 bytes of UTF-8. With `reply` (a
   * message's id in this chat) it answers that one, with a copy of its start (`quote`).
   */
  send(chat, text, reply = '') {
    return this.#task(async () => {
      const trimmed = cutUtf8(withEmoji(String(text ?? '').trim()), TEXT_MAX_BYTES);
      if (!this.#enabled || !trimmed) return;
      const payload = { t: 'text', id: randomId(), at: Date.now(), text: trimmed };
      const answered = reply ? this.#messageIn(chat, String(reply)) : null;
      if (answered && answered.system !== true && answered.deleted !== true && str(answered.id).length <= REPLY_ID_MAX && str(answered.from)) {
        payload.reply = str(answered.id);
        payload.quote = { from: str(answered.from), text: quoteOf(answered) };
      }
      await this.#sendMessage(chat, payload);
    });
  }

  /**
   * Tells whoever shows notifications that a message came ('message': chat, name, text, mention),
   * unless the chat is muted and the message does not name this player.
   */
  #notify(chat, name, text) {
    const mention = mentions(str(text), this.#profileName);
    if (this.mutedUntil(chat) && !mention) return;
    this.#emit('message', { chat, name, text, mention });
  }

  /** A message of a chat by its id, as kept; null if there is none. */
  #messageIn(chat, id) {
    const c = isObject(this.#state.chats) ? this.#state.chats[chat] : null;
    return (c && Array.isArray(c.m) ? c.m : []).find((m) => str(m.id) === id) || null;
  }

  /**
   * Deletes one of this player's own messages for everybody in the chat: here at once, then to
   * the friend (and the own other devices) or the group. Resolves to whether it went.
   */
  deleteMessage(chat, message) {
    return this.#task(async () => {
      if (!this.#enabled) return false;
      const id = String(message ?? '');
      if (!(await this.#applyDelete(chat, id, this.#state.fp))) return false;
      await this.#sendToChat(chat, { t: 'delete', msg: id });
      return true;
    });
  }

  /**
   * A message deleted by its writer: only the writer's own (`from`), never a note of the group,
   * and once. What stays is a stand-in ("Message deleted"): its id, time, writer and state.
   * Resolves to whether it changed something.
   */
  #applyDelete(chat, id, fp) {
    const m = this.#messageIn(chat, id);
    if (!m || m.system === true || m.deleted === true || str(m.from) !== fp) return Promise.resolve(false);
    for (const key of Object.keys(m)) {
      if (!['id', 'at', 'from', 'mine', 'state'].includes(key)) delete m[key];
    }
    m.deleted = true;
    this.#changed();
    return this.#save().then(() => true);
  }

  // ---------------------------------------------------------------- muting (this device only)

  /**
   * Mutes a chat until a time (ms since 1970; MUTE_FOREVER until turned back on), or 0 to turn
   * it back on. Muted, it gives no notifications and does not count in the unread total, unless
   * a message names this player. Nothing of it is sent.
   */
  setMute(chat, until) {
    return this.#task(async () => {
      if (!this.#enabled || !chat) return;
      if (!isObject(this.#state.chats)) this.#state.chats = {};
      const chats = this.#state.chats;
      if (!isObject(chats[chat])) chats[chat] = {};
      const v = Number(until);
      if (Number.isFinite(v) && v > Date.now()) chats[chat].mute = Math.min(v, MUTE_FOREVER);
      else delete chats[chat].mute;
      this.#changed();
      await this.#save();
    });
  }

  /** Until when a chat is muted (ms since 1970), 0 if it is not. */
  mutedUntil(chat) {
    const c = this.#enabled && isObject(this.#state.chats) ? this.#state.chats[chat] : null;
    const until = c ? num(c.mute) : 0;
    return until > Date.now() ? until : 0;
  }

  /** A join card for a room: invite, title, game, console, … (what cleanRoom keeps). */
  sendJoin(chat, room) {
    return this.#task(async () => {
      if (!this.#enabled) return;
      const card = cleanRoom(room);
      if (!Object.keys(card).length) return;
      // The text for whoever shows only text (a notification, an older app)
      await this.#sendMessage(chat, { t: 'join', id: randomId(), at: Date.now(), join: card,
        text: `Invites you to ${card.title ?? 'a session'}` });
    });
  }

  /** A plan: a title and times to choose from (ms since 1970), in any chat. */
  sendPlan(chat, title, times) {
    return this.#task(async () => {
      if (!this.#enabled) return;
      const plan = cleanPlan({ title: String(title ?? ''), times });
      if (!plan) return;
      await this.#sendMessage(chat, { t: 'plan', id: randomId(), at: Date.now(), plan,
        text: plan.title ? `When: ${plan.title}?` : 'When shall we play?' });
    });
  }

  /** This player's answers to a plan: one per time, 0 no, 1 maybe, 2 yes. */
  vote(chat, plan, answers) {
    return this.#task(async () => {
      if (!this.#enabled) return;
      const a = Array.isArray(answers) ? answers.slice(0, 8) : [];
      await this.#applyVote(chat, plan, this.#state.fp, a);
      await this.#sendToChat(chat, { t: 'vote', plan, answers: a });
    });
  }

  /** This player's reaction to a message: given, or taken back if it was given already. */
  react(chat, message, emoji) {
    return this.#task(async () => {
      if (!this.#enabled || !validReaction(String(emoji ?? ''))) return;
      const c = isObject(this.#state.chats) ? this.#state.chats[chat] : null;
      const m = c && Array.isArray(c.m) ? c.m.find((x) => str(x.id) === String(message)) : null;
      // Given already: taken back
      const given = !!m && isObject(m.reactions) && Array.isArray(m.reactions[emoji]) && m.reactions[emoji].includes(this.#state.fp);
      // Only what changed here goes out (not over the limits, not for a message that is not here)
      if (!(await this.#applyReaction(chat, String(message), this.#state.fp, String(emoji), !given))) return;
      await this.#sendToChat(chat, { t: 'react', msg: String(message), emoji: String(emoji), on: !given });
    });
  }

  /**
   * Who has read how far in a chat: {fp, name, avatar, at} for everybody but this player (the
   * friend, or the members of a group); nobody with read receipts off.
   */
  readers(chat) {
    if (!this.#enabled || !this.readReceipts) return [];
    const c = isObject(this.#state.chats) ? this.#state.chats[chat] : null;
    const read = c && isObject(c.read) ? c.read : {};
    const friends = this.#friendsObj();
    const g = this.#group(chat);
    const names = g && isObject(g.names) ? g.names : {};
    const out = [];
    // In the app's order (a QJsonObject's keys)
    for (const fp of Object.keys(read).sort()) {
      if (fp === this.#state.fp) continue;
      const f = friends[fp];
      out.push({ fp, name: nameOr(f, str(names[fp])), avatar: isObject(f) ? avatarUrl(f) : '', at: num(read[fp]) });
    }
    return out;
  }

  async #sendMessage(chat, payload) {
    const saved = this.#addMessage(chat, { ...payload, from: this.#state.fp, mine: true, state: 'sending' });
    // In line now, before anything is awaited: the messages to one friend leave in this order
    const sending = this.#sendToChat(chat, payload);
    await saved;
    const ok = await sending;
    await this.#addMessage(chat, { id: payload.id, state: ok ? 'sent' : 'failed' });
  }

  /** Sends to a chat, whichever it is: a friend's devices (and the own ones), or a group. */
  #sendToChat(chat, payload) {
    return this.#group(chat) ? this.#track(this.#sendGroup(chat, payload)) : this.#sendPayload(chat, payload, true);
  }

  /**
   * Read up to the newest message: unread no more here; "read up to here" to the others while
   * read receipts are on, and to the own other devices always.
   */
  markRead(chat) {
    if (!this.#enabled) return Promise.resolve();
    const c = isObject(this.#state.chats) ? this.#state.chats[chat] : null;
    if (!isObject(c)) return Promise.resolve();
    const msgs = Array.isArray(c.m) ? c.m : [];
    // Only when something was unread: a change says so to everybody who shows the chat
    const seen = num(c.seen);
    if (!msgs.some((m) => m.mine !== true && num(m.at) > seen)) return Promise.resolve();
    // The newest message's time, not this clock's: a friend's clock ahead of ours would stay unread
    let newest = Date.now();
    let upto = '';
    let uptoAt = 0;
    for (const m of msgs) {
      newest = Math.max(newest, num(m.at));
      if (m.mine !== true && m.system !== true && num(m.at) >= uptoAt) {
        upto = str(m.id);
        uptoAt = num(m.at);
      }
    }
    c.seen = newest;
    this.#changed();
    return this.#task(async () => {
      await this.#save();
      if (!upto) return;
      const seenPayload = { t: 'seen', upto, upto_at: uptoAt };
      if (this.readReceipts) await this.#sendToChat(chat, seenPayload);
      else if (!this.#group(chat)) await this.#sendPayload(this.#state.fp, { t: 'sync', to: chat, of: seenPayload }, false);
    });
  }

  /** Somebody read a chat up to the message written at `at` (the writer's clock). */
  #applyRead(chat, fp, at) {
    if (!isObject(this.#state.chats)) this.#state.chats = {};
    const chats = this.#state.chats;
    if (!isObject(chats[chat])) chats[chat] = {};
    const c = chats[chat];
    if (!isObject(c.read)) c.read = {};
    if (num(c.read[fp]) >= at) return Promise.resolve();
    c.read[fp] = at;
    this.#changed();
    return this.#save();
  }

  /** Another own device read it: here too. */
  #markSeenUpTo(chat, at) {
    const c = isObject(this.#state.chats) ? this.#state.chats[chat] : null;
    if (!isObject(c) || num(c.seen) >= at) return Promise.resolve();
    c.seen = at;
    this.#changed();
    return this.#save();
  }

  /**
   * A reaction from one player to a message, given or taken back. The same rules as in a
   * session's chat: one emoji, at most 3 different ones of one player and 12 on a message.
   * Resolves false if it changed nothing.
   */
  #applyReaction(chat, message, fp, emoji, on) {
    if (!validReaction(emoji)) return Promise.resolve(false);
    const c = isObject(this.#state.chats) ? this.#state.chats[chat] : null;
    const msgs = c && Array.isArray(c.m) ? c.m : [];
    const m = msgs.find((x) => str(x.id) === message && x.system !== true && x.deleted !== true);
    if (!m) return Promise.resolve(false);
    const reactions = isObject(m.reactions) ? m.reactions : {};
    const who = Array.isArray(reactions[emoji]) ? reactions[emoji].filter((x) => typeof x === 'string') : [];
    const has = who.includes(fp);
    if (on === has) return Promise.resolve(false);
    if (on) {
      const own = Object.values(reactions).filter((list) => Array.isArray(list) && list.includes(fp)).length;
      if ((!(emoji in reactions) && Object.keys(reactions).length >= MAX_REACTION_KINDS) || own >= MAX_REACTIONS_PER_PLAYER) {
        return Promise.resolve(false);
      }
      who.push(fp);
    } else who.splice(who.indexOf(fp), 1);
    if (who.length) reactions[emoji] = who;
    else delete reactions[emoji];
    m.reactions = reactions;
    this.#changed();
    return this.#save().then(() => true);
  }

  /** A plan's answers from one player, kept with the plan. */
  #applyVote(chat, plan, fp, answers) {
    const c = isObject(this.#state.chats) ? this.#state.chats[chat] : null;
    const msgs = c && Array.isArray(c.m) ? c.m : [];
    const m = msgs.find((x) => str(x.id) === plan && isObject(x.plan));
    if (!m) return Promise.resolve();
    if (!isObject(m.votes)) m.votes = {};
    m.votes[fp] = cleanAnswers(answers, Array.isArray(m.plan.times) ? m.plan.times.length : 0);
    this.#changed();
    return this.#save();
  }

  /** Adds a message where its writer's time puts it, or changes the state of one there is. */
  #addMessage(chat, message) {
    if (!isObject(this.#state.chats)) this.#state.chats = {};
    const chats = this.#state.chats;
    if (!isObject(chats[chat])) chats[chat] = {};
    const c = chats[chat];
    if (!Array.isArray(c.m)) c.m = [];
    const msgs = c.m;
    const id = str(message.id);
    const there = msgs.find((m) => str(m.id) === id);
    if (there) {
      // The same message again (a resend, or the own copy): only its state changes
      if ('state' in message) there.state = message.state;
    } else {
      // In the order they were written: two sent at once may reach the server the other way round
      const at = num(message.at);
      let place = msgs.length;
      while (place > 0 && num(msgs[place - 1].at) > at) place--;
      msgs.splice(place, 0, message);
      while (msgs.length > MESSAGES_KEPT) msgs.shift();
    }
    this.#changed();
    return this.#save();
  }

  /** Sends these messages of a chat, opened, to the server's operator, with a line of why. */
  report(chat, ids, why = '') {
    return this.#task(async () => {
      const list = Array.isArray(ids) ? ids : [];
      if (!this.#enabled || !list.length) return false;
      // Only what the player chose, opened: the server cannot read anything else
      const c = isObject(this.#state.chats) ? this.#state.chats[chat] : null;
      const picked = (c && Array.isArray(c.m) ? c.m : [])
        .filter((m) => list.includes(str(m.id)) && m.mine !== true && m.deleted !== true)
        .map((m) => ({ from: m.from, at: m.at, text: m.text }));
      if (!picked.length) return false;
      const text = { why: String(why ?? '').slice(0, 500), by: this.#state.fp, group: !!this.#group(chat), messages: picked };
      const { ok } = await this.#call('report', { text: JSON.stringify(text) });
      return ok;
    });
  }

  // ---------------------------------------------------------------- groups

  #group(gid) {
    const groups = this.#state.groups;
    return isObject(groups) && isObject(groups[gid]) ? groups[gid] : null;
  }

  #putGroup(gid, g) {
    if (!isObject(this.#state.groups)) this.#state.groups = {};
    this.#state.groups[gid] = g;
  }

  isGroup(chat) {
    return this.#enabled && !!this.#group(chat);
  }

  /**
   * A group as the panel shows it: id, name, admin (this player is one), add ("admins" or "all"),
   * left, members (each: fp, name, admin, friend, me), banned (each: fp, name); null if there is none.
   */
  group(chat) {
    const g = this.#group(chat);
    if (!g || !this.#enabled) return null;
    const me = this.#state.fp;
    const admins = fpsOf(g.admins);
    const friends = this.#friendsObj();
    const names = isObject(g.names) ? g.names : {};
    const nameOf = (fp) => {
      if (fp === me) return this.#profileName;
      const name = str((friends[fp] || {}).name);
      return name || str(names[fp]) || 'A member';
    };
    return {
      id: chat,
      name: str(g.name),
      admin: admins.includes(me),
      add: str(g.add) || 'admins',
      left: g.left === true,
      members: fpsOf(g.members).map((fp) => ({ fp, name: nameOf(fp), admin: admins.includes(fp), me: fp === me,
        friend: (friends[fp] || {}).state === 'friend' })),
      banned: fpsOf(g.banned).map((fp) => ({ fp, name: nameOf(fp) })),
    };
  }

  /** A new group with these friends in it, this player its admin. Resolves to its id ("" if none was made). */
  createGroup(name, list) {
    return this.#task(async () => {
      const id = this.#identity();
      if (!this.#enabled || !id) return '';
      const gid = fc.randomBytes(16);
      // Its first byte tells a group message from an envelope in the group's box (kind 1 or 2 there)
      gid[0] |= 0x80;
      const owner = await fc.generateKeyPair({ usage: 'ecdsa', extractable: true });
      const ownerD = (await fc.exportPrivate(owner)).subarray(0, 32);
      const token = fc.randomBytes(16);
      const me = this.#state.fp;
      const friends = this.#friendsObj();
      const members = [me];
      for (const fp of Array.isArray(list) ? list : []) {
        if (!members.includes(fp) && members.length < MAX_GROUP_MEMBERS && (friends[fp] || {}).state === 'friend') members.push(fp);
      }
      const key = b64(gid);
      const box = b64(await fc.groupBox(gid));
      const clean = simplified(name).slice(0, 48);
      this.#putGroup(key, {
        name: clean || 'Group', version: 0, members, admins: [me], banned: [], add: 'admins',
        token: b64(token), owner: hex(ownerD), box, after: 0, made: Date.now(),
      });
      await this.#save();
      this.#changed();
      // The box on the server first; then the group's first version to everybody, then the sender key
      this.#spawn((async () => {
        const { ok } = await this.#call('group_new', { box, owner: b64(owner.pub), hash: b64(await fc.sha256(token)) });
        if (!ok) return;
        this.#spawn(this.#publishGroup(key));
        this.#spawn(this.#newSenderKey(key));
      })());
      return key;
    });
  }

  addToGroup(chat, list) {
    return this.#task(async () => {
      const g = this.#group(chat);
      const me = this.#state.fp;
      if (!this.#enabled || !g || g.left === true) return;
      const admin = fpsOf(g.admins).includes(me);
      if (!admin && g.add !== 'all') return;
      const members = fpsOf(g.members);
      const banned = fpsOf(g.banned);
      const friends = this.#friendsObj();
      let more = false;
      for (const fp of Array.isArray(list) ? list : []) {
        if (members.includes(fp) || members.length >= MAX_GROUP_MEMBERS || (banned.includes(fp) && !admin)
          || (friends[fp] || {}).state !== 'friend') continue;
        members.push(fp);
        more = true;
      }
      if (!more) return;
      // An admin lifts a ban by adding them again
      g.members = members;
      g.banned = banned.filter((fp) => !members.includes(fp));
      await this.#save();
      this.#changed();
      // The newcomers get the own sender key with everybody's new ones
      this.#spawn(this.#publishGroup(chat));
      this.#spawn(this.#newSenderKey(chat));
    });
  }

  /** Admins only; a ban keeps them from being added again by anybody but an admin. */
  removeFromGroup(chat, fp, ban) {
    return this.#task(async () => {
      const g = this.#group(chat);
      const me = this.#state.fp;
      if (!this.#enabled || !g || fp === me || !fpsOf(g.admins).includes(me) || !str(g.owner)) return;
      const members = fpsOf(g.members).filter((x) => x !== fp);
      const admins = fpsOf(g.admins).filter((x) => x !== fp);
      const banned = fpsOf(g.banned);
      if (ban && !banned.includes(fp)) banned.push(fp);
      const wasAdmin = fpsOf(g.admins).includes(fp);
      // An admin taken out also had the owner key: a new one first, which they do not get
      if (wasAdmin && !(await this.#rotateOwner(chat))) return;
      // A new write token: they write nothing more into the box
      const now = this.#group(chat);
      const owner = now ? await pairFromHex(now.owner, 'ecdsa') : null;
      if (!owner) return;
      const token = fc.randomBytes(16);
      const { ok } = await this.#callOwner('group_token', { box: str(now.box), hash: b64(await fc.sha256(token)) }, owner);
      const after = this.#group(chat);
      if (!ok || !after) return;
      after.members = members;
      after.admins = admins;
      after.banned = banned;
      after.token = b64(token);
      await this.#save();
      this.#changed();
      await this.#publishGroup(chat, [fp]);
      this.#spawn(this.#newSenderKey(chat));
    });
  }

  setGroupAdmin(chat, fp, admin) {
    return this.#task(async () => {
      const g = this.#group(chat);
      const me = this.#state.fp;
      if (!this.#enabled || !g) return;
      let admins = fpsOf(g.admins);
      if (!admins.includes(me) || !fpsOf(g.members).includes(fp) || admins.includes(fp) === !!admin) return;
      admins = admin ? [...admins, fp] : admins.filter((x) => x !== fp);
      if (!admins.length) return;
      g.admins = admins;
      await this.#save();
      this.#changed();
      // No longer an admin: a new owner key they do not get
      if (!admin) await this.#rotateOwner(chat);
      await this.#publishGroup(chat);
    });
  }

  renameGroup(chat, name) {
    return this.#task(async () => {
      const g = this.#group(chat);
      const clean = simplified(name).slice(0, 48);
      if (!this.#enabled || !g || !clean || !fpsOf(g.admins).includes(this.#state.fp)) return;
      g.name = clean;
      await this.#save();
      this.#changed();
      await this.#publishGroup(chat);
    });
  }

  /** "admins" or "all": who may add people. */
  setGroupAdd(chat, who) {
    return this.#task(async () => {
      const g = this.#group(chat);
      if (!this.#enabled || !g || !fpsOf(g.admins).includes(this.#state.fp)) return;
      g.add = who === 'all' ? 'all' : 'admins';
      await this.#save();
      this.#changed();
      await this.#publishGroup(chat);
    });
  }

  /**
   * Leaves a group: an admin with a version of their own (the last admin hands it on to whoever
   * has been in it longest), a member with "leave" to everybody, which an admin carries out.
   */
  leaveGroup(chat) {
    return this.#task(async () => {
      let g = this.#group(chat);
      const me = this.#state.fp;
      if (!this.#enabled || !g || g.left === true) return;
      let admins = fpsOf(g.admins);
      let members = fpsOf(g.members);
      let handOver = null;
      if (admins.includes(me)) {
        members = members.filter((x) => x !== me);
        admins = admins.filter((x) => x !== me);
        if (!admins.length && members.length) admins = [members[0]];
        g.members = members;
        g.admins = admins;
        // A new owner key for those who stay, then the version without this player
        if (members.length) {
          const rotating = this.#rotateOwner(chat);
          handOver = (async () => {
            await rotating;
            await this.#publishGroup(chat, [me]);
            const after = this.#group(chat);
            if (after) delete after.owner;
            await this.#save();
          })();
        }
      } else {
        // The admins take them out
        for (const fp of members) if (fp !== me) this.#spawn(this.#sendInGroup(chat, fp, { t: 'leave', group: chat }));
      }
      g = this.#group(chat);
      g.left = true;
      delete g.mine;
      delete g.chains;
      delete g.owner;
      await this.#addMessage(chat, { id: randomId(), at: Date.now(), text: 'You left the group', system: true });
      if (handOver) await handOver;
    });
  }

  /** A new owner key for a group (when an admin goes): signed with the one before. Resolves to whether it worked. */
  async #rotateOwner(gid) {
    const g = this.#group(gid);
    // The key before, read at once: the caller may take it away right after
    const oldHex = g ? str(g.owner) : '';
    const box = g ? str(g.box) : '';
    const old = oldHex ? await pairFromHex(oldHex, 'ecdsa') : null;
    if (!old) return false;
    const fresh = await fc.generateKeyPair({ usage: 'ecdsa', extractable: true });
    const { ok } = await this.#callOwner('group_owner', { box, owner: b64(fresh.pub) }, old);
    const now = this.#group(gid);
    if (ok && now) {
      now.owner = hex((await fc.exportPrivate(fresh)).subarray(0, 32));
      await this.#save();
    }
    return ok;
  }

  /** A request signed with a group's owner key (group_token, group_owner): as every signed request, without fp. */
  async #callOwner(op, fieldsIn, owner) {
    const fields = { ...fieldsIn, n: b64(fc.randomBytes(8)) };
    const t = String(Math.floor(Date.now() / 1000) + this.#clockOffset);
    const sig = await fc.sign(owner, fc.requestMessage(op, t, fields));
    return this.#call(op, { ...fields, t, sig: b64(sig) });
  }

  /** The group's newest version, signed by this player, to every member (and to whoever was taken out). */
  #publishGroup(gid, alsoTo = []) {
    return this.#track(this.#inLine(`gp:${gid}`, () => this.#publishGroupNow(gid, alsoTo)));
  }

  async #publishGroupNow(gid, alsoTo) {
    const id = this.#identity();
    const gidb = unb64(gid, 16);
    let g = this.#group(gid);
    if (!id || !gidb || !g) return;
    const me = this.#state.fp;
    const version = num(g.version) + 1;
    const members = fpsOf(g.members);
    const admins = fpsOf(g.admins);
    const banned = fpsOf(g.banned);
    const name = str(g.name);
    const add = str(g.add) || 'admins';
    const sig = await fc.sign(id.iks, groupSigned(gidb, version, name, add, members, admins, banned));
    g = this.#group(gid);
    if (!g) return;
    // The names the members go by, for those who are no friends of each other
    const friends = this.#friendsObj();
    const names = { ...(isObject(g.names) ? g.names : {}) };
    names[me] = this.#profileName;
    for (const fp of members) {
      const n = str((friends[fp] || {}).name);
      if (n) names[fp] = n;
    }
    g.names = names;
    g.version = version;
    await this.#save();
    const p = { t: 'group', group: gid, version, name, add, members, admins, banned, by: me, sig: b64(sig), names };
    const friend = (fp) => (friends[fp] || {}).state === 'friend';
    // Whoever was taken out learns it, without the new token
    for (const fp of alsoTo) {
      if (fp === me || members.includes(fp)) continue;
      this.#spawn(friend(fp) ? this.#sendPayload(fp, p, false) : this.#sendInGroup(gid, fp, p));
    }
    const withToken = { ...p, token: str(g.token) };
    const forAdmins = { ...withToken, owner: str(g.owner) };
    for (const fp of members) {
      const payload = admins.includes(fp) ? forAdmins : withToken;
      if (fp === me) this.#spawn(this.#sendPayload(me, payload, false)); // the own other devices
      else if (friend(fp)) this.#spawn(this.#sendPayload(fp, payload, false));
      else this.#spawn(this.#sendInGroup(gid, fp, payload));
    }
  }

  /**
   * A version of a group from `from`: taken if the protocol's rules allow it ("Groups" in
   * docs/friends-protocol.md) and its signature is `by`'s. Resolves to whether it was taken.
   */
  async #applyGroup(from, p) {
    const gid = str(p.group);
    const by = str(p.by);
    const gidb = unb64(gid, 16);
    // Only who signed it hands a version on
    if (!gidb || !(gidb[0] & 0x80) || by !== from) return false;
    const box = b64(await fc.groupBox(gidb));
    const sentOwner = str(p.owner);
    const ownerOk = sentOwner ? !!(await pairFromHex(sentOwner, 'ecdsa')) : false;
    for (let attempt = 0; attempt < 3; attempt++) {
      const before = this.#group(gid);
      const knownVersion = before ? num(before.version) : 0;
      // Versions count up a little at a time: a jump to the end of the numbers would freeze the group
      const sentVersion = p.version;
      if (typeof sentVersion !== 'number' || !Number.isInteger(sentVersion) || sentVersion < 1 || sentVersion > 2147483647
        || (before && (sentVersion <= knownVersion || sentVersion > knownVersion + 16))) return false;
      const members = fpsOf(p.members);
      const admins = fpsOf(p.admins);
      const banned = fpsOf(p.banned);
      const name = simplified(p.name).slice(0, 48);
      const add = p.add === 'all' ? 'all' : 'admins';
      if (!members.length || members.length > MAX_GROUP_MEMBERS || !admins.length) return false;
      if (admins.some((a) => !members.includes(a))) return false;
      // A group new here comes only from a friend, and only with this player in it: nobody else
      // puts anybody into a group (the first version reaches a new member through their inbox)
      const me = this.#state.fp;
      if (!before && from !== me && ((this.#friendsObj()[from] || {}).state !== 'friend' || !members.includes(me))) return false;
      // Who may: an admin of the version known so far (of this one, for a group new here); where
      // everybody may add people, a member who adds somebody who is not banned, and changes nothing else
      const oldMembers = before ? fpsOf(before.members) : [];
      const byAdmin = before ? fpsOf(before.admins).includes(by) : admins.includes(by);
      let allowed = byAdmin;
      if (!allowed && before && before.add === 'all' && oldMembers.includes(by)) {
        allowed = members.length > oldMembers.length && name === str(before.name) && add === 'all'
          && sameSet(admins, fpsOf(before.admins)) && sameSet(banned, fpsOf(before.banned))
          && oldMembers.every((m) => members.includes(m))
          && members.every((m) => oldMembers.includes(m) || !banned.includes(m));
      }
      if (!allowed) return false;
      // Signed with their identity key, from their card
      const card = await this.#decode(str(isObject(this.#state.cards) ? this.#state.cards[by] : ''));
      const sig = unb64(p.sig, 64);
      if (!card || !sig || !(await fc.verify(card.iks, groupSigned(gidb, sentVersion, name, add, members, admins, banned), sig))) return false;
      // Changed meanwhile: looked at again
      const g = this.#group(gid);
      if (g !== before || (g && num(g.version) !== knownVersion)) continue;
      this.#takeGroupVersion(gid, gidb, box, before, { version: sentVersion, members, admins, banned, name, add, by, byAdmin,
        token: typeof p.token === 'string' ? p.token : null, owner: ownerOk ? sentOwner : '', names: isObject(p.names) ? p.names : {} });
      return true;
    }
    return false;
  }

  /** A version that passed every check: kept, with a note in the chat where this player came or went. */
  #takeGroupVersion(gid, gidb, box, before, v) {
    const me = this.#state.fp;
    const fresh = !before;
    const wasMember = !fresh && before.left !== true;
    const member = v.members.includes(me);
    const oldMembers = before ? fpsOf(before.members) : [];
    const g = before || {};
    g.name = v.name || 'Group';
    g.version = v.version;
    g.members = v.members;
    g.admins = v.admins;
    g.banned = v.banned;
    g.add = v.add;
    g.box = box;
    const names = isObject(g.names) ? g.names : {};
    for (const fp of v.members) {
      const n = simplified(v.names[fp]).slice(0, 32);
      if (n) names[fp] = n;
    }
    g.names = names;
    // The write token and the owner key only from an admin: a member who only adds somebody hands on neither
    if (v.byAdmin && v.token !== null) g.token = v.token;
    if (v.byAdmin && v.admins.includes(me) && v.owner) g.owner = v.owner;
    else if (!v.admins.includes(me)) delete g.owner;
    if (fresh) g.after = 0;
    const who = nameOr(this.#friendsObj()[v.by], str(names[v.by]));
    let note = '';
    if (!member) {
      g.left = true;
      delete g.mine;
      delete g.chains;
      delete g.owner;
      if (wasMember) note = `${who} took you out of the group`;
    } else {
      g.left = false;
      if (fresh || !wasMember) note = `${who} added you to the group`;
    }
    this.#putGroup(gid, g);
    this.#spawn(this.#save());
    if (note) this.#spawn(this.#addMessage(gid, { id: `v${v.version}`, at: Date.now(), text: note, system: true, from: v.by }));
    // A new sender key whenever who is in it changed: whoever left reads nothing new
    if (member && (!sameSet(oldMembers, v.members) || !isObject(g.mine))) this.#spawn(this.#newSenderKey(gid));
    if (fresh && member) this.#notify(gid, who, note);
    this.#changed();
  }

  /** This device's sender key, new, to every member's devices (and the own other ones). */
  #newSenderKey(gid) {
    return this.#track(this.#inLine(`gs:${gid}`, () => this.#newSenderKeyNow(gid)));
  }

  async #newSenderKeyNow(gid) {
    let g = this.#group(gid);
    if (!g || g.left === true) return;
    const c = await fc.newSenderChain(num(isObject(g.mine) ? g.mine.chain : 0) + 1);
    g = this.#group(gid);
    if (!g || g.left === true) return;
    g.mine = { chain: c.chain, iter: c.iter, ck: hex(c.ck), sign: c.sign, made: Date.now() };
    await this.#save();
    const p = { t: 'senderkey', group: gid, chain: c.chain, iter: c.iter, ck: hex(c.ck), pub: b64(c.signPub) };
    // Through the group's box: members need not be friends
    for (const fp of fpsOf(g.members)) this.#spawn(this.#sendInGroup(gid, fp, p));
  }

  /** A payload sealed for every device of one member, through the group's box. */
  #sendInGroup(gid, fp, payloadIn) {
    const g = this.#group(gid);
    if (!g || !str(g.token) || !str(g.box)) return Promise.resolve(false);
    const payload = { ...payloadIn };
    if (!('id' in payload)) payload.id = randomId();
    if (!('at' in payload)) payload.at = Date.now();
    const plain = JSON.stringify(payload);
    const box = str(g.box);
    const token = str(g.token);
    return this.#track(this.#inLine(fp, () => this.#sendSealedNow(fp, plain, 'group', box, token)));
  }

  /** A group message: sealed once with this device's sender key, into the group's box. */
  async #sendGroup(gid, payloadIn) {
    const id = this.#identity();
    const ownDevice = this.#ownDevice();
    const gidb = unb64(gid, 16);
    if (!this.#enabled || !id || !ownDevice || !gidb) return false;
    const payload = { ...payloadIn };
    if (!('id' in payload)) payload.id = randomId();
    if (!('at' in payload)) payload.at = Date.now();
    const sealed = await this.#inLine(`gs:${gid}`, async () => {
      let g = this.#group(gid);
      if (!g || g.left === true) return null;
      if (!isObject(g.mine) || Date.now() - num(g.mine.made) > SENDER_KEY_RENEW_MS) {
        await this.#newSenderKeyNow(gid);
        g = this.#group(gid);
        if (!g || !isObject(g.mine)) return null;
      }
      const mine = g.mine;
      const ck = unhex(mine.ck, 32);
      if (!ck || !mine.sign) return null;
      const own = { chain: num(mine.chain), iter: num(mine.iter), ck, sign: mine.sign };
      const note = await fc.groupSeal(own, gidb, id.fp, ownDevice, JSON.stringify(payload));
      if (!note) return null;
      // Kept before it goes out: one message key is never used twice
      const now = this.#group(gid);
      if (now && now.mine === mine) now.mine = { ...mine, iter: own.iter, ck: hex(own.ck) };
      await this.#save();
      return { note, box: str(g.box), token: str(g.token) };
    });
    if (!sealed) return false;
    const { ok } = await this.#call('group_put', { box: sealed.box, token: sealed.token, note: b64(sealed.note) });
    return ok;
  }

  async #pollGroups() {
    const groups = isObject(this.#state.groups) ? this.#state.groups : {};
    await Promise.all(Object.entries(groups).map(async ([gid, g]) => {
      if (!isObject(g) || g.left === true || !str(g.token)) return;
      const { ok, answer } = await this.#call('group_get', { box: str(g.box), token: str(g.token), after: String(Math.trunc(num(g.after))) });
      if (!ok) {
        // Not even an old token opens it: taken out (or gone a month)
        if (answer.error === 'bad_token' || answer.error === 'unknown') {
          const now = this.#group(gid);
          if (now && now.left !== true) {
            now.left = true;
            delete now.mine;
            await this.#addMessage(gid, { id: randomId(), at: Date.now(), text: 'You are no longer in this group', system: true });
          }
        }
        return;
      }
      await this.#takeGroupNotes(gid, answer.notes);
      // More waiting than one answer holds: again once these are read
      if (answer.more === true) this.#pollAgain = true;
    }));
  }

  /** What came through a group's box: envelopes for this device, and group messages. */
  async #takeGroupNotes(gid, notesIn) {
    const notes = Array.isArray(notesIn) ? notesIn.filter(isObject) : [];
    if (!notes.length) return;
    const ownDevice = this.#ownDevice();
    if (!ownDevice) return;
    // Who starts a conversation through the box is looked up first: X3DH needs their card
    const cards = isObject(this.#state.cards) ? this.#state.cards : {};
    const unknown = new Set();
    for (const n of notes) {
      const bytes = unb64(n.note);
      if (!bytes || bytes.length < 2 || (bytes[1] !== 1 && bytes[1] !== 2)) continue;
      const e = fc.decodeEnvelope(bytes);
      if (e && bytesEqual(e.toDevice, ownDevice) && e.kind === 1 && !cards[b64(e.fromFp)]) unknown.add(b64(e.fromFp));
    }
    await Promise.all([...unknown].map(async (fp) => {
      const { ok, answer } = await this.#call('card_get', { fp });
      if (ok && (await this.#keepCard(fp, answer.card))) this.#cardFetched.set(fp, Date.now());
    }));
    let after = num((this.#group(gid) || {}).after);
    const take = async (n) => {
      const bytes = unb64(n.note);
      if (!bytes || bytes.length < 2) return true;
      if (bytes[1] === 1 || bytes[1] === 2) {
        const e = fc.decodeEnvelope(bytes);
        return e && bytesEqual(e.toDevice, ownDevice) ? this.#handleEnvelope(bytes, false, gid) : true;
      }
      return this.#handleGroupMessage(gid, bytes);
    };
    const keys = this.#keysCame;
    let wait = [];
    for (const n of notes) {
      after = Math.max(after, num(n.id));
      if (!(await take(n))) wait.push('since' in n ? { ...n } : { ...n, since: Date.now() });
    }
    // A message can reach the box before its writer's sender key for this device (that one takes
    // a card and a bundle first): the key came later in this same lot, so its messages once more
    if (wait.length && this.#keysCame !== keys) {
      const again = [];
      for (const n of wait) if (!(await take(n))) again.push(n);
      wait = again;
    }
    const g = this.#group(gid);
    if (!g) return;
    g.after = after;
    // What still waits for a sender key: a while, not for ever
    const waiting = [];
    const ids = new Set();
    for (const w of [...(Array.isArray(g.waiting) ? g.waiting : []), ...wait]) {
      const id = num(w.id);
      if (ids.has(id) || Date.now() - num(w.since) > WAITING_MS) continue;
      ids.add(id);
      waiting.push(w);
    }
    while (waiting.length > WAITING_MAX) waiting.shift();
    g.waiting = waiting;
    await this.#save();
  }

  /** False if the writer's sender key has not come yet. */
  #handleGroupMessage(gid, bytes) {
    return this.#inLine(`gm:${gid}`, async () => {
      const m = fc.decodeGroupMessage(bytes);
      const ownDevice = this.#ownDevice();
      if (!m || b64(m.group) !== gid || !ownDevice) return true;
      const from = b64(m.fp);
      const me = this.#state.fp;
      const g = this.#group(gid);
      if (!g || g.left === true || !fpsOf(g.members).includes(from)) return true;
      // This device's own, shown when it was written; a blocked player's, not at all
      if ((from === me && bytesEqual(m.device, ownDevice)) || (this.#friendsObj()[from] || {}).state === 'blocked') return true;
      const key = `${from}:${hex(m.device)}`;
      const stored = isObject(g.chains) ? g.chains[key] : null;
      const chain = loadTheirChain(stored);
      if (!chain) return false;
      // A newer chain whose key has not come yet waits; an older one is over
      if (m.chain !== chain.chain) return m.chain < chain.chain;
      const plain = await fc.groupOpen(chain, m);
      if (!plain) return true;
      const now = this.#group(gid);
      if (now && isObject(now.chains) && now.chains[key] === stored) {
        now.chains[key] = saveTheirChain(chain);
        await this.#save();
      }
      const p = parseObject(decoder.decode(plain));
      const t = str(p.t);
      const names = isObject(g.names) ? g.names : {};
      const name = from === me ? this.#profileName : nameOr(this.#friendsObj()[from], str(names[from]) || 'A member');
      if (t === 'vote') {
        await this.#applyVote(gid, str(p.plan), from, p.answers);
        return true;
      }
      if (t === 'react') {
        await this.#applyReaction(gid, str(p.msg), from, str(p.emoji), p.on === true);
        return true;
      }
      if (t === 'delete') {
        await this.#applyDelete(gid, str(p.msg), from);
        return true;
      }
      if (t === 'seen') {
        if (from === me) await this.#markSeenUpTo(gid, num(p.upto_at));
        else await this.#applyRead(gid, from, num(p.upto_at));
        return true;
      }
      if (t !== 'text' && t !== 'join' && t !== 'plan') return true;
      const msg = { id: p.id, at: p.at, text: str(p.text).slice(0, 4000), from, mine: from === me };
      if (from === me) msg.state = 'sent';
      if (t === 'text') {
        const embed = cleanEmbed(p.embed, msg.text);
        if (Object.keys(embed).length) msg.embed = embed;
        Object.assign(msg, cleanReply(p));
      }
      if (t === 'join') {
        const card = cleanRoom(p.join);
        if (!Object.keys(card).length) return true;
        msg.join = card;
      }
      if (t === 'plan') {
        const plan = cleanPlan(p.plan);
        if (!plan) return true;
        msg.plan = plan;
      }
      const isNew = !this.#messageIn(gid, str(msg.id));
      await this.#addMessage(gid, msg);
      if (from !== me && isNew) this.#notify(gid, name, msg.text);
      return true;
    });
  }

  // ---------------------------------------------------------------- backup, linking another device

  /** What a backup or a linked device gets: everything but this device's own keys and conversations. */
  async #bundle() {
    const bundle = {};
    for (const [k, v] of Object.entries(this.#state)) {
      if (k !== 'device' && k !== 'sessions' && k !== 'bundle_opks' && k !== 'identity' && k !== 'groups') bundle[k] = v;
    }
    const id = this.#state.identity;
    // As the app writes it: the private scalars as hex
    bundle.identity = {
      iks: hex((await fc.exportPrivate(id.iks)).subarray(0, 32)),
      ikd: hex((await fc.exportPrivate(id.ikd)).subarray(0, 32)),
    };
    bundle.settings = { ...this.#settings(), state: this.#myState, free: this.#myFree };
    // A group's sender keys are each device's own
    const groups = {};
    for (const [gid, g] of Object.entries(isObject(this.#state.groups) ? this.#state.groups : {})) {
      if (!isObject(g)) continue;
      const { mine, chains, waiting, ...rest } = g;
      groups[gid] = rest;
    }
    bundle.groups = groups;
    return bundle;
  }

  /**
   * The backup file's text: everything of the identity, sealed with the passphrase (at least
   * 8 characters); null if it cannot be made. A restored copy is a new device.
   */
  saveBackup(passphrase) {
    return this.#task(async () => {
      if (!this.#enabled || typeof passphrase !== 'string' || passphrase.length < 8) return null;
      return fc.sealBackup(passphrase, JSON.stringify(await this.#bundle()));
    });
  }

  /** Makes this browser a new device of the identity in a backup file's text. */
  restoreBackup(text, passphrase) {
    return this.#task(async () => {
      if (this.#elsewhere) return false;
      const json = await fc.openBackup(String(passphrase ?? ''), String(text ?? ''));
      return json !== null && (await this.#restoreFrom(parseObject(json)));
    });
  }

  /** The identity from a backup or another device: this browser becomes one more device of it. */
  async #restoreFrom(restored) {
    if (this.#elsewhere || !isObject(restored) || !isObject(restored.identity)) return false;
    const identity = await importIdentity(restored.identity);
    // The keys have to be the identity the bundle names
    if (!identity || restored.fp !== b64(identity.fp)) return false;
    restored.identity = identity;
    // This browser becomes a new device of the identity, on the server it is set to
    restored.device = { id: hex(fc.randomBytes(8)) };
    delete restored.sessions;
    delete restored.bundle_opks;
    const groups = isObject(restored.groups) ? restored.groups : {};
    for (const g of Object.values(groups)) {
      if (!isObject(g)) continue;
      delete g.mine;
      delete g.chains;
      delete g.waiting;
    }
    restored.server = this.server;
    this.#stopTimers();
    this.#generation++;
    this.#discard = false;
    this.#statusHidden = false;
    this.#state = restored;
    const settings = this.#settings();
    this.#myState = ['online', 'away', 'busy', 'invisible'].includes(settings.state) ? settings.state : 'online';
    this.#myFree = str(settings.free);
    this.#statuses.clear();
    this.#safety.clear();
    this.#cardFetched.clear();
    this.#devicesList = [];
    this.#enabled = true;
    this.#problem = '';
    await this.#save();
    this.#publishCard();
    // The own sender keys in every group, for the other members
    for (const gid of Object.keys(groups)) this.#spawn(this.#newSenderKey(gid));
    this.#startTimers();
    this.#changed();
    return true;
  }

  /** On the device that has the friends: a link for the new one, then waits for it. */
  startLinking() {
    return this.#task(async () => {
      if (!this.#enabled) return;
      this.cancelLinking();
      const link = { side: 1, state: 'waiting', code: '', text: '', secret: fc.randomBytes(16), ek: null, key: null, commit: null,
        box: '', busy: false, timer: null };
      this.#link = link;
      this.#problem = '';
      link.ek = await fc.generateKeyPair({ usage: 'ecdh' });
      link.box = b64(await fc.linkBox(link.secret));
      if (this.#link !== link) return;
      link.text = `psrpt://link/${b64(link.secret)}`;
      if (this.#server !== DEFAULT_SERVER) link.text += `?at=${percentEncode(this.#server)}`;
      this.#changed();
      // Part 0: only the hash of this side's key. The key itself comes once the other side has put
      // its own, so that nobody can try keys until the six digits happen to match
      const { ok } = await this.#call('link_put', { box: link.box, side: '1', part: '0', data: b64(await fc.sha256(link.ek.pub)) });
      if (this.#link !== link) return;
      if (!ok) {
        this.#linkFailed(link, 'The friends server did not take the link.');
        return;
      }
      this.#startLinkTimer(link);
    });
  }

  /** On the new device (friends off here): the link the other device shows. */
  joinLink(text) {
    return this.#task(async () => {
      if (this.#enabled || this.#elsewhere) return;
      this.cancelLinking();
      let s = String(text ?? '').trim();
      const at = s.indexOf('psrpt://link/');
      if (at >= 0) s = s.slice(at + 13);
      const q = s.indexOf('?');
      if (q >= 0) {
        let server = '';
        try {
          server = new URLSearchParams(s.slice(q + 1)).get('at') || '';
        } catch (e) {
          server = '';
        }
        s = s.slice(0, q);
        if (server) await this.setServer(server);
      } else {
        await this.setServer('');
      }
      const link = { side: 2, state: 'waiting', code: '', text: '', secret: unb64(s, 16), ek: null, key: null, commit: null,
        box: '', busy: false, timer: null, joined: Date.now() };
      this.#link = link;
      if (!link.secret) {
        this.#linkFailed(link, 'That is no link from another device.');
        return;
      }
      link.ek = await fc.generateKeyPair({ usage: 'ecdh' });
      link.box = b64(await fc.linkBox(link.secret));
      if (this.#link !== link) return;
      // The other side's commitment comes first; this side's key is put once it is there
      this.#problem = '';
      this.#changed();
      this.#startLinkTimer(link);
    });
  }

  cancelLinking() {
    const link = this.#link;
    if (!link) return;
    this.#link = null;
    clearInterval(link.timer);
    if (link.box && link.state !== 'done' && link.state !== 'failed') {
      this.#spawn(this.#call('link_put', { box: link.box, side: String(link.side), part: '0', data: '' }));
    }
    this.#changed();
  }

  /** The codes are the same on both: the keys and friends go to the new device. */
  confirmLinking() {
    return this.#task(async () => {
      const link = this.#link;
      if (!link || link.side !== 1 || link.state !== 'code' || !link.key) return;
      link.state = 'sending';
      this.#changed();
      // Everything of the identity, sealed with the key only the two devices have
      const nonce = fc.randomBytes(12);
      const sealed = await fc.seal(link.key, nonce, await fc.linkBox(link.secret), JSON.stringify(await this.#bundle()));
      const all = concat(nonce, sealed);
      // In parts of at most 192 KB (parts 2 to n + 1); the first says how many there are
      const parts = Math.ceil((all.length + 2) / LINK_CHUNK);
      const datas = [];
      let at = 0;
      for (let part = 1; part <= parts; part++) {
        const head = part === 1 ? new Uint8Array([parts >> 8, parts & 0xff]) : new Uint8Array(0);
        const take = Math.min(LINK_CHUNK - head.length, all.length - at);
        datas.push(concat(head, all.subarray(at, at + take)));
        at += take;
      }
      const put = async (part, data) => (await this.#call('link_put', { box: link.box, side: '1', part: String(part), data: b64(data) })).ok;
      // The rest first, the first part (which says how many) last: the new device starts once it sees it
      const rest = await Promise.all(datas.slice(1).map((data, i) => put(i + 3, data)));
      if (this.#link !== link) return;
      if (!rest.every(Boolean) || !(await put(2, datas[0]))) {
        this.#linkFailed(link, 'The friends server did not take everything.');
        return;
      }
      if (this.#link !== link) return;
      link.state = 'done';
      link.key = null;
      this.#changed();
    });
  }

  #startLinkTimer(link) {
    clearInterval(link.timer);
    link.timer = setInterval(() => this.#spawn(this.#pollLink(link)), LINK_POLL_MS);
    if (link.timer.unref) link.timer.unref();
    this.#spawn(this.#pollLink(link));
  }

  #linkFailed(link, why) {
    clearInterval(link.timer);
    link.state = 'failed';
    link.code = '';
    link.key = null;
    link.busy = false;
    this.#problem = why;
    this.#changed();
  }

  /** The digits from the other side's key; false if it is no key. */
  async #linkKeyFrom(link, data) {
    if (!data || data.length !== 65 || !(await fc.validPublic(data))) return false;
    const dhOut = await fc.dh(link.ek, data);
    if (!dhOut) return false;
    link.key = await fc.linkKey(link.secret, dhOut);
    link.code = await fc.linkCode(link.key);
    link.state = 'code';
    return true;
  }

  async #pollLink(link) {
    if (this.#link !== link || link.busy || link.state === 'failed' || link.state === 'done') return;
    link.busy = true;
    try {
      await (link.side === 1 ? this.#pollLinkGiving(link) : this.#pollLinkTaking(link));
    } finally {
      link.busy = false;
    }
  }

  async #linkGet(link, side, part) {
    const { ok, answer } = await this.#call('link_get', { box: link.box, side, part: String(part) });
    return { ok, answer, data: unb64(str(answer.data)) || new Uint8Array(0) };
  }

  /** Taken away by the other device, or run out. */
  #linkGone(link, answer) {
    if (answer.error === 'unknown' && link.state !== 'waiting') this.#linkFailed(link, 'The link has run out. Start it again on the other device.');
  }

  /**
   * The new device: the server answers a box it no longer has like an empty one, so a box
   * without the other side's commitment (part 0, put as the link is shown and kept until
   * the box goes) was taken away by the other device or has run out. True if so.
   */
  #linkVanished(link, answer) {
    if (!Array.isArray(answer.parts) || answer.parts.includes(0)) return false;
    // The other device shows its link while it puts the commitment: a moment's grace for that
    if (!link.commit && Date.now() - link.joined < LINK_GRACE_MS) return false;
    this.#linkFailed(link, 'The link has run out. Start it again on the other device.');
    return true;
  }

  /** The device that has the friends: the new device's key, then this side's own, out of hiding, and the digits. */
  async #pollLinkGiving(link) {
    if (link.state !== 'waiting') return;
    const { ok, answer, data } = await this.#linkGet(link, '2', 0);
    if (this.#link !== link) return;
    if (!ok) {
      this.#linkGone(link, answer);
      return;
    }
    if (!data.length) return;
    if (!(await this.#linkKeyFrom(link, data))) {
      this.#linkFailed(link, "The other device's answer is broken.");
      return;
    }
    clearInterval(link.timer);
    this.#changed();
    const put = await this.#call('link_put', { box: link.box, side: '1', part: '1', data: b64(link.ek.pub) });
    if (!put.ok && this.#link === link) this.#linkFailed(link, 'The friends server did not take the link.');
  }

  /** The new device: the commitment, then its own key, then the other key, then the parts. */
  async #pollLinkTaking(link) {
    if (!link.commit) {
      const { ok, answer, data } = await this.#linkGet(link, '1', 0);
      if (this.#link !== link || !ok || this.#linkVanished(link, answer) || data.length !== 32) return;
      link.commit = data;
      const put = await this.#call('link_put', { box: link.box, side: '2', part: '0', data: b64(link.ek.pub) });
      if (!put.ok && this.#link === link) this.#linkFailed(link, 'The link has run out. Start it again on the other device.');
      return;
    }
    if (link.state === 'waiting') {
      const { ok, answer, data } = await this.#linkGet(link, '1', 1);
      if (this.#link !== link) return;
      if (!ok) {
        this.#linkGone(link, answer);
        return;
      }
      if (!data.length) {
        this.#linkVanished(link, answer);
        return;
      }
      // The key has to be the one committed to
      if (!bytesEqual(await fc.sha256(data), link.commit) || !(await this.#linkKeyFrom(link, data))) {
        this.#linkFailed(link, "The other device's answer is broken.");
        return;
      }
      this.#changed();
      return;
    }
    if (link.state !== 'code') return;
    // Part 2 says how many parts there are (2 to n + 1), then all of them
    const { ok, answer, data } = await this.#linkGet(link, '1', 2);
    if (this.#link !== link) return;
    if (!ok) {
      this.#linkGone(link, answer);
      return;
    }
    if (data.length < 2) {
      this.#linkVanished(link, answer);
      return;
    }
    const parts = (data[0] << 8) | data[1];
    link.state = 'receiving';
    clearInterval(link.timer);
    this.#changed();
    const chunks = [data.subarray(2)];
    for (let part = 3; part <= parts + 1; part++) {
      const r = await this.#call('link_get', { box: link.box, side: '1', part: String(part) });
      const chunk = r.ok ? unb64(str(r.answer.data)) : null;
      if (this.#link !== link) return;
      if (!chunk) {
        this.#linkFailed(link, 'Not everything came from the other device.');
        return;
      }
      chunks.push(chunk);
    }
    // Opened with the key both sides worked out
    const all = concat(...chunks);
    const plain = all.length >= 12 + 16 ? await fc.open(link.key, all.subarray(0, 12), await fc.linkBox(link.secret), all.subarray(12)) : null;
    if (this.#link !== link) return;
    if (!plain || !(await this.#restoreFrom(parseObject(decoder.decode(plain))))) {
      this.#linkFailed(link, 'What came from the other device does not open.');
      return;
    }
    this.#spawn(this.#call('link_put', { box: link.box, side: '2', part: '0', data: '' }));
    link.state = 'done';
    link.key = null;
    this.#changed();
  }

  // ---------------------------------------------------------------- push messages (web only)

  /**
   * The friends server's public key for push subscriptions (VAPID, 65 bytes), or null. Only a
   * browser needs it: the app is woken by nothing but its own timers.
   */
  pushKey() {
    return this.#task(async () => {
      const { ok, answer } = await this.#call('push_key', {});
      return ok ? unb64(answer.key, 65) : null;
    });
  }

  /** The group boxes whose messages wake this browser too, each "box.token" (a token that reads it). */
  #pushGroupList() {
    const out = [];
    for (const g of Object.values(isObject(this.#state.groups) ? this.#state.groups : {})) {
      if (isObject(g) && g.left !== true && str(g.box) && str(g.token) && out.length < 200) out.push(`${g.box}.${g.token}`);
    }
    return out.join(',');
  }

  /**
   * Where this browser takes push messages: {endpoint, p256dh, auth} with the keys as base64url,
   * as PushSubscription.toJSON() has them; or null for nowhere any more. The server then sends
   * an empty push whenever something arrives for this device, also in its groups' boxes.
   * Resolves to whether it took it.
   */
  setPush(subscription) {
    return this.#task(async () => {
      const device = this.#ownDevice();
      if (!this.#enabled || !device) return false;
      const fields = { device: b64(device), endpoint: '' };
      let sub = null;
      let groups = '';
      if (subscription && subscription.endpoint) {
        const p256dh = unb64(subscription.p256dh, 65);
        const auth = unb64(subscription.auth, 16);
        if (!p256dh || !auth) return false;
        sub = { endpoint: String(subscription.endpoint), p256dh: b64(p256dh), auth: b64(auth) };
        Object.assign(fields, sub);
        groups = this.#pushGroupList();
        if (groups) fields.groups = groups;
      }
      const { ok } = await this.#call('push_set', fields, true);
      if (ok) {
        this.#pushSub = sub;
        this.#pushGroups = groups;
      }
      return ok;
    });
  }

  // ---------------------------------------------------------------- the state

  #friendsObj() {
    if (!isObject(this.#state.friends)) this.#state.friends = {};
    return this.#state.friends;
  }

  #identity() {
    const id = this.#state.identity;
    return isObject(id) && id.iks && id.ikd && id.fp ? id : null;
  }

  #ownDevice() {
    return isObject(this.#state.device) ? unhex(this.#state.device.id, 8) : null;
  }

  /** Stores the state; false if the browser would not. Resolves once a write after this call is done. */
  #save() {
    if (!this.#queue || this.#discard) return Promise.resolve(true);
    if (this.#enabled) {
      const settings = this.#settings(true);
      settings.state = this.#myState;
      settings.free = this.#myFree;
    }
    return this.#track(this.#queue.save().then(() => true, () => {
      this.#problem = 'The friends of this browser cannot be stored.';
      this.#changed();
      return false;
    }));
  }

  #changed() {
    // The safety codes take a moment (SHA-256 is asynchronous here): made once per friend
    const own = unb64(this.#state.fp, 16);
    if (own) {
      for (const [fp, f] of Object.entries(this.#friendsObj())) {
        if (f.state !== 'friend' || this.#safety.has(fp)) continue;
        const theirs = unb64(fp, 16);
        if (!theirs) continue;
        this.#safety.set(fp, '');
        this.#spawn(fc.safetyCode(own, theirs).then((code) => {
          this.#safety.set(fp, code);
          this.dispatchEvent(new Event('change'));
        }));
      }
    }
    this.dispatchEvent(new Event('change'));
  }

  #emit(type, detail) {
    this.dispatchEvent(new CustomEvent(type, { detail }));
  }

  // ---------------------------------------------------------------- what is under way

  #track(p) {
    this.#busy++;
    const end = () => {
      if (--this.#busy === 0) {
        const waiters = this.#idleWaiters;
        this.#idleWaiters = [];
        for (const w of waiters) w();
      }
    };
    p.then(end, end);
    return p;
  }

  /** Runs fn, tracked; never rejects (a bug is logged, the answer is undefined). */
  #task(fn) {
    return this.#track((async () => {
      try {
        return await fn();
      } catch (e) {
        console.warn('friends:', e);
        return undefined;
      }
    })());
  }

  /** Something that goes on by itself, tracked. */
  #spawn(p) {
    this.#track(Promise.resolve(p).catch((e) => console.warn('friends:', e)));
  }

  /** One at a time: what uses a conversation (sealing, opening) and stores it. */
  #exclusive(fn) {
    const result = this.#lockTail.then(fn);
    this.#lockTail = result.then(() => {}, () => {});
    return result;
  }

  /**
   * In the order asked, one after the other, per key: what goes to one identity's devices (its
   * fingerprint), a group's own sender key ("gs:"), its versions ("gp:"), its messages ("gm:").
   */
  #inLine(key, fn) {
    const before = this.#lines.get(key) || Promise.resolve();
    const result = before.then(fn);
    const tail = result.then(() => {}, () => {});
    this.#lines.set(key, tail);
    tail.then(() => {
      if (this.#lines.get(key) === tail) this.#lines.delete(key);
    });
    return result;
  }

  // ---------------------------------------------------------------- timers

  #pollInterval() {
    const hidden = typeof document !== 'undefined' && document.visibilityState === 'hidden';
    return this.#background || hidden ? POLL_HIDDEN_MS : POLL_VISIBLE_MS;
  }

  #schedulePoll() {
    clearTimeout(this.#pollTimer);
    this.#pollTimer = setTimeout(() => {
      this.#pollTimer = null;
      if (!this.#enabled) return;
      this.#poll();
      this.#schedulePoll();
    }, this.#pollInterval());
    if (this.#pollTimer && this.#pollTimer.unref) this.#pollTimer.unref();
  }

  #startTimers() {
    if (!this.#timers || this.#statusTimer !== null) return;
    this.#schedulePoll();
    this.#statusTimer = setInterval(() => this.#spawn(this.#putStatus()), STATUS_PUT_MS);
    if (this.#statusTimer.unref) this.#statusTimer.unref();
    if (typeof document !== 'undefined' && document.addEventListener) {
      this.#onVisibility = () => {
        // Back on the page: what came meanwhile at once
        if (document.visibilityState === 'visible' && this.#enabled) this.#poll();
        this.#schedulePoll();
      };
      document.addEventListener('visibilitychange', this.#onVisibility);
    }
  }

  #stopTimers() {
    clearTimeout(this.#pollTimer);
    clearInterval(this.#statusTimer);
    this.#pollTimer = null;
    this.#statusTimer = null;
    if (this.#onVisibility) document.removeEventListener('visibilitychange', this.#onVisibility);
    this.#onVisibility = null;
  }

  // ---------------------------------------------------------------- talking to the server

  /**
   * One request. Signed ones carry fp, a nonce n, the time t and sig over all fields but v, op,
   * t and sig. @returns {Promise<{ok: boolean, answer: object}>}
   */
  async #call(op, fields, signed = false, again = true) {
    const all = { ...fields };
    const form = new URLSearchParams();
    form.set('v', '1');
    form.set('op', op);
    if (signed) {
      const id = this.#identity();
      if (!id) return { ok: false, answer: {} };
      all.fp = b64(id.fp);
      // A few random bytes: two requests in the same second are not the same request
      all.n = b64(fc.randomBytes(8));
      // The server's time where this clock is off
      const t = String(Math.floor(Date.now() / 1000) + this.#clockOffset);
      const sig = await fc.sign(id.iks, fc.requestMessage(op, t, all));
      form.set('t', t);
      form.set('sig', b64(sig));
    }
    for (const [k, v] of Object.entries(all)) form.set(k, String(v));
    let reached = false;
    let answer = {};
    try {
      const reply = await this.#fetch(this.#server, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: form.toString(),
        credentials: 'omit',
        referrerPolicy: 'no-referrer',
        signal: AbortSignal.timeout(CALL_TIMEOUT_MS),
      });
      reached = reply.ok;
      const text = await reply.text();
      if (text.length <= MAX_ANSWER) answer = parseObject(text);
    } catch (e) {
      reached = false;
    }
    const ok = reached && answer.ok === true;
    if (answer.error === 'stale_time' && typeof answer.now === 'number') {
      this.#clockOffset = Math.trunc(answer.now) - Math.floor(Date.now() / 1000);
      // Once more with the server's time: a clock that is off loses no request
      if (signed && again) return this.#call(op, fields, signed, false);
    }
    const online = reached && Object.keys(answer).length > 0;
    if (online !== this.#online) {
      this.#online = online;
      this.#changed();
    }
    return { ok, answer };
  }

  #poll() {
    if (this.#polling) {
      this.#pollAgain = true;
      return this.#polling;
    }
    this.#polling = this.#task(async () => {
      try {
        do {
          this.#pollAgain = false;
          await this.#pollOnce();
        } while (this.#pollAgain && this.#enabled);
      } finally {
        this.#polling = null;
      }
    });
    return this.#polling;
  }

  async #pollOnce() {
    if (!this.#enabled) return;
    const device = b64(this.#ownDevice() || new Uint8Array(0));
    const lk = unhex(this.#state.lk, 16);
    await Promise.all([
      // The inbox of this device
      (async () => {
        const { ok, answer } = await this.#call('inbox_get', { device }, true);
        if (!ok) return;
        await this.#takeNotes(answer.notes, false, 'device', device);
        // More waiting than one answer holds: again once these are taken away
        if (answer.more === true) this.#pollAgain = true;
        if (typeof answer.prekeys === 'number' && answer.prekeys < PREKEY_LOW) this.#spawn(this.#topUpPrekeys());
      })(),
      // The groups' boxes
      this.#pollGroups(),
      // The request box of the current link
      (async () => {
        if (!lk) return;
        const box = b64(await fc.requestBox(lk));
        const { ok, answer } = await this.#call('inbox_get', { box }, true);
        if (ok) await this.#takeNotes(answer.notes, true, 'box', box);
      })(),
      // The friends' statuses
      this.#pollStatuses(),
    ]);
    // In a group since the push subscription went to the server, or out of one: the server hears it
    if (this.#pushSub && this.#pushGroupList() !== this.#pushGroups) this.#spawn(this.setPush(this.#pushSub));
  }

  /**
   * The friends' statuses: each under a name worked out from their friends key and the day
   * (today's, and yesterday's around midnight, when clocks differ).
   */
  async #pollStatuses() {
    const today = Math.floor(Date.now() / DAY_MS);
    const boxes = [];
    const boxOf = new Map();
    for (const [fp, f] of Object.entries(this.#friendsObj())) {
      if (boxes.length >= 400) break;
      const key = f.state === 'friend' && isObject(f.fk) ? unhex(f.fk.key, 32) : null;
      if (!key) continue;
      const list = [];
      for (const day of [today, today - 1]) list.push(b64(await fc.statusBox(key, day)));
      boxes.push(...list);
      boxOf.set(fp, list);
    }
    if (!boxes.length) return;
    const { ok, answer } = await this.#call('status_get', { boxes: boxes.join(',') });
    if (ok) await this.#readStatuses(isObject(answer.notes) ? answer.notes : {}, boxOf);
  }

  async #readStatuses(notes, boxOf) {
    for (const fp of Object.keys(this.#friendsObj())) {
      const f = this.#friendsObj()[fp];
      if (!f) continue;
      // Today's note if there is one, else yesterday's
      const found = (boxOf.get(fp) || []).find((box) => typeof notes[box] === 'string');
      const note = found ? unb64(notes[found]) : null;
      const fk = isObject(f.fk) ? f.fk : {};
      const key = unhex(fk.key, 32);
      const fpBytes = unb64(fp, 16);
      if (!note || !key || !fpBytes) {
        this.#statuses.delete(fp);
        continue;
      }
      const opened = await fc.openStatus(key, num(fk.epoch), fpBytes, note);
      if (!opened) continue;
      const st = parseObject(opened.json);
      // Only a fresh one: a server could hand out an old note of the same key again
      if (Math.abs(Date.now() - num(st.at)) > STATUS_MAX_AGE_MS) continue;
      if ('room' in st) {
        const clean = cleanRoom(st.room);
        if (Object.keys(clean).length) st.room = clean;
        else delete st.room;
      }
      const before = this.#statuses.get(fp) || {};
      st.seen = Date.now();
      this.#statuses.set(fp, st);
      // Somebody opened a room since the last look
      const roomNow = isObject(st.room) ? st.room : {};
      if (Object.keys(roomNow).length && !(isObject(before.room) && Object.keys(before.room).length)) {
        this.#emit('hosting', { fp, name: str(f.name), title: str(roomNow.title) });
      }
    }
    this.#changed();
  }

  /** This device's signed prekey, made or renewed; the one before stays for a while. */
  async #ensureSignedPrekey(dev) {
    if (dev.spk && Date.now() - num(dev.spk_made) < SPK_RENEW_MS) return;
    const spk = await fc.generateKeyPair({ usage: 'ecdh' });
    if (dev.spk) {
      dev.spk_prev = dev.spk;
      dev.spk_prev_id = dev.spk_id;
    }
    dev.spk = spk;
    dev.spk_id = num(dev.spk_id) + 1;
    dev.spk_made = Date.now();
  }

  /**
   * Puts the card with this device on the server; one at a time, once more if asked meanwhile.
   * With `without` (a device id, hex): the card without that device, after any put under way.
   */
  #publishCard(without = '') {
    if (without) {
      return this.#task(async () => {
        if (this.#publishing) await this.#publishing;
        await this.#publishOnce(without);
      });
    }
    if (this.#publishing) {
      this.#publishAgain = true;
      return this.#publishing;
    }
    this.#publishing = this.#task(async () => {
      try {
        do {
          this.#publishAgain = false;
          await this.#publishOnce();
        } while (this.#publishAgain && this.#enabled);
      } finally {
        this.#publishing = null;
      }
    });
    return this.#publishing;
  }

  async #publishOnce(without = '') {
    const id = this.#identity();
    const dev = this.#state.device;
    if (!id || !isObject(dev)) return;
    await this.#ensureSignedPrekey(dev);
    await this.#save();
    // The card on the server lists the other own devices too: they stay
    const { ok, answer } = await this.#call('card_get', { fp: b64(id.fp) });
    if (this.#identity() !== id) return;
    let seq = num(this.#state.seq);
    let devices = [];
    const current = ok ? await this.#decode(answer.card) : null;
    if (current && bytesEqual(current.fp, id.fp)) {
      seq = Math.max(seq, current.seq);
      devices = current.devices;
    }
    const mineId = this.#ownDevice();
    if (!mineId || !dev.spk) return;
    // A device the card did not list yet: the friends hear of it, so they write to it too
    const newDevice = !devices.some((d) => bytesEqual(d.id, mineId));
    // This device leaves the card when it is the one taken off (friends switched off here)
    const leaving = hex(mineId) === without;
    const spkId = num(dev.spk_id);
    const mine = { id: mineId, kind: DEVICE_KIND_WEB, spkId, spk: dev.spk.pub, spkSig: await fc.signPrekey(id, mineId, spkId, dev.spk.pub) };
    devices = devices.filter((d) => !bytesEqual(d.id, mineId) && hex(d.id) !== without);
    if (!leaving) devices.unshift(mine);
    if (!devices.length) return;
    devices = devices.slice(0, fc.MAX_DEVICES);
    const profile = { name: this.#profileName };
    if (this.#profileAvatar) profile.avatar = standardBase64(this.#profileAvatar);
    const lk = unhex(this.#state.lk, 16) || new Uint8Array(16);
    const card = { seq: seq + 1, devices, profile: await fc.sealProfile(lk, id.fp, JSON.stringify(profile)) };
    const out = await fc.encodeCard(id, card);
    if (!out || this.#identity() !== id) return;
    this.#state.seq = card.seq;
    this.#state.card = b64(out);
    await this.#save();
    const put = await this.#call('card_put', { card: b64(out) }, true);
    if (put.ok) this.#spawn(this.#refreshDevices());
    if (!put.ok || leaving || this.#identity() !== id) return;
    this.#cardFetched.delete(this.#state.fp);
    if (newDevice) {
      for (const [fp, f] of Object.entries(this.#friendsObj())) {
        if (f.state === 'friend') this.#spawn(this.#sendPayload(fp, { t: 'card' }, false));
      }
      this.#spawn(this.#sendPayload(this.#state.fp, { t: 'card' }, false));
    }
    // The request box of the current link, and the own devices' token
    const box = b64(await fc.requestBox(unhex(this.#state.lk, 16) || new Uint8Array(16)));
    this.#spawn(this.#call('requests_set', { box }, true));
    this.#spawn(tokenHash(this.#state.self_token).then((hash) => this.#call('token_add', { hash }, true)));
    // The first one-time prekeys; later ones when the server says it has few left
    if (this.#state.device.next_opk === undefined) this.#spawn(this.#topUpPrekeys());
  }

  async #topUpPrekeys() {
    if (this.#toppingUp || !this.#enabled) return;
    this.#toppingUp = true;
    try {
      const dev = this.#state.device;
      if (!isObject(dev.opks)) dev.opks = {};
      let next = num(dev.next_opk) || 1;
      const keys = new Uint8Array(PREKEY_BATCH * 69);
      const view = new DataView(keys.buffer);
      for (let i = 0; i < PREKEY_BATCH; i++) {
        const pair = await fc.generateKeyPair({ usage: 'ecdh' });
        const id = next++;
        dev.opks[id] = pair;
        view.setUint32(i * 69, id);
        keys.set(pair.pub, i * 69 + 4);
      }
      // Prekeys the server gave out but nobody used are not kept forever: the oldest go
      const ids = Object.keys(dev.opks).map(Number).sort((a, b) => a - b);
      for (let i = 0; i < ids.length - PREKEYS_KEPT; i++) delete dev.opks[ids[i]];
      dev.next_opk = next;
      await this.#save();
      await this.#call('prekeys_put', { device: b64(unhex(dev.id, 8)), keys: b64(keys) }, true);
    } finally {
      this.#toppingUp = false;
    }
  }

  /** Puts the own status note; invisible or shown to nobody, the note goes (one empty put). */
  async #putStatus() {
    if (!this.#enabled) return;
    if (this.#statusInFlight) {
      this.#statusAgain = true;
      return;
    }
    this.#statusInFlight = true;
    try {
      await this.#putStatusOnce();
    } finally {
      this.#statusInFlight = false;
    }
    // Whatever changed while this one was on its way
    if (this.#statusAgain) {
      this.#statusAgain = false;
      await this.#putStatus();
    }
  }

  async #putStatusOnce() {
    const id = this.#identity();
    if (!id) return;
    if (this.#myState === 'invisible' || this.showStatus === 'nobody') {
      if (this.#statusHidden) return;
      this.#statusHidden = true;
      await this.#call('status_put', { note: '' }, true);
      return;
    }
    this.#statusHidden = false;
    let fk = isObject(this.#state.fk) ? this.#state.fk : {};
    let day = Math.floor((Date.now() - num(fk.start)) / DAY_MS);
    if (day > 365) {
      // A year on one epoch: a new one, sent to every friend
      this.#newFriendsEpoch();
      fk = this.#state.fk;
      day = 0;
      await this.#save();
      for (const [fp, f] of Object.entries(this.#friendsObj())) if (f.state === 'friend') this.#sendFriendsKey(fp);
      this.#syncOwn('', true);
    }
    const st = { state: this.#myState, web: true };
    if (this.showGame && this.#activity) st.activity = this.#activity;
    if (this.friendsJoin !== 'never' && this.#room) st.room = this.#room;
    if (this.#myFree) st.free = this.#myFree;
    st.at = Date.now();
    const key = unhex(fk.key, 32);
    if (!key) return;
    const note = await fc.sealStatus(key, num(fk.epoch), Math.max(0, day), id.fp, JSON.stringify(st));
    const box = b64(await fc.statusBox(key, Math.floor(Date.now() / DAY_MS)));
    await this.#call('status_put', { note: b64(note), box }, true);
  }

  // ---------------------------------------------------------------- cards

  /** A card from its base64url text, its signatures checked; the same text is read once. */
  #decode(text) {
    if (typeof text !== 'string' || !text) return Promise.resolve(null);
    let p = this.#decoded.get(text);
    if (!p) {
      const bytes = unb64(text);
      p = bytes ? fc.decodeCard(bytes).catch(() => null) : Promise.resolve(null);
      if (this.#decoded.size > 200) this.#decoded.clear();
      this.#decoded.set(text, p);
    }
    return p;
  }

  /** Keeps a card fetched for fp if it is valid; an older one than the card kept does not win. */
  async #keepCard(fp, text) {
    const card = await this.#decode(text);
    if (!card || b64(card.fp) !== fp) return null;
    for (;;) {
      if (!isObject(this.#state.cards)) this.#state.cards = {};
      const cards = this.#state.cards;
      const oldText = cards[fp];
      if (oldText && oldText !== text) {
        const old = await this.#decode(oldText);
        if (this.#state.cards !== cards || cards[fp] !== oldText) continue;
        if (old && old.seq > card.seq) return old;
      }
      cards[fp] = text;
      if (fp === this.#state.fp) this.#spawn(this.#refreshDevices());
      return card;
    }
  }

  /** A friend's name and picture from their card, opened with their link key. */
  async #refreshProfile(fp, card) {
    const f = this.#friendsObj()[fp];
    const lk = f && unhex(f.lk, 16);
    if (!lk) return;
    const json = await fc.openProfile(lk, card.fp, card.profile);
    if (json === null || this.#friendsObj()[fp] !== f) return;
    const profile = parseObject(json);
    f.name = str(profile.name).slice(0, 32);
    const avatar = str(profile.avatar);
    f.avatar = avatar.length <= 16000 ? avatar : '';
  }

  /** A card, from the cache if it was fetched a short while ago, else from the server; null if none. */
  async #withCard(fp) {
    const cached = isObject(this.#state.cards) ? this.#state.cards[fp] : null;
    if (cached && Date.now() - (this.#cardFetched.get(fp) || 0) < CARD_CACHE_MS) {
      const card = await this.#decode(cached);
      if (card) return card;
    }
    const { ok, answer } = await this.#call('card_get', { fp });
    const card = ok ? await this.#keepCard(fp, answer.card) : null;
    if (!card) return null;
    this.#cardFetched.set(fp, Date.now());
    await this.#refreshProfile(fp, card);
    await this.#save();
    return card;
  }

  // ---------------------------------------------------------------- sending sealed payloads

  #sessionsOf(key) {
    const entry = isObject(this.#state.sessions) ? this.#state.sessions[key] : null;
    return entry && Array.isArray(entry.s) ? entry.s : [];
  }

  #storeSessions(key, list) {
    if (!isObject(this.#state.sessions)) this.#state.sessions = {};
    const sessions = this.#state.sessions;
    // Older ones stay a while: both sides may have started one at the same moment
    const kept = list.slice(0, SESSIONS_KEPT);
    if (!kept.length) {
      delete sessions[key];
      return;
    }
    sessions[key] = { ...(isObject(sessions[key]) ? sessions[key] : {}), s: kept };
  }

  /** The ephemeral keys a device started conversations with: a first message comes only once. */
  #seenEk(key, ek) {
    const entry = isObject(this.#state.sessions) ? this.#state.sessions[key] : null;
    return !!entry && Array.isArray(entry.ek) && entry.ek.includes(ek);
  }

  #rememberEk(key, ek) {
    if (!isObject(this.#state.sessions)) this.#state.sessions = {};
    const sessions = this.#state.sessions;
    const entry = isObject(sessions[key]) ? sessions[key] : (sessions[key] = {});
    const eks = Array.isArray(entry.ek) ? entry.ek : (entry.ek = []);
    if (eks.includes(ek)) return;
    eks.push(ek);
    while (eks.length > EKS_KEPT) eks.shift();
  }

  /**
   * Seals a payload for every device of an identity (a friend's, or the own other devices) and
   * sends it: a request into their request box, everything else into their devices' inboxes.
   * Called in the order the payloads should arrive in; resolves to whether every copy went out.
   */
  #sendPayload(fp, payloadIn, toOwnDevices = true) {
    if (!this.#enabled) return Promise.resolve(false);
    const payload = { ...payloadIn };
    if (!('id' in payload)) payload.id = randomId();
    if (!('at' in payload)) payload.at = Date.now();
    const plain = JSON.stringify(payload);
    // Taken now: the record may go right after (a "no", unfriending)
    const f = this.#friendsObj()[fp] || {};
    const own = fp === this.#state.fp;
    const requestBox = payload.t === 'request';
    const lkHex = f.lk;
    const token = own ? str(this.#state.self_token) : str(f.token);
    return this.#track(this.#inLine(fp, async () => {
      let target = '';
      if (requestBox) {
        const lk = unhex(lkHex, 16);
        if (lk) target = b64(await fc.requestBox(lk));
      } else {
        target = token;
      }
      if (!target || !this.#enabled) return false;
      // The own other devices get a copy, so each of them shows the whole chat
      if (toOwnDevices && !own) this.#spawn(this.#sendPayload(this.#state.fp, { t: 'sync', to: fp, of: payload }, false));
      return this.#sendSealedNow(fp, plain, requestBox ? 'request' : 'inbox', target, '');
    }));
  }

  /** A sealed payload for every device of one identity (but this one), the way the route says. */
  async #sendSealedNow(fp, plain, route, target, token) {
    const own = fp === this.#state.fp;
    const card = await this.#withCard(fp);
    if (!card) return false;
    const ownDevice = this.#ownDevice();
    const others = (c) => c.devices.filter((d) => !(own && ownDevice && bytesEqual(d.id, ownDevice)));
    // Devices without a conversation yet get a one-time prekey each; the others need none
    const missing = others(card).filter((d) => !this.#sessionsOf(sessionKey(fp, d.id)).length).map((d) => b64(d.id));
    let use = card;
    let prekeys = {};
    if (missing.length) {
      const { ok, answer } = await this.#call('bundle_get', { fp, devices: missing.join(',') });
      if (ok) {
        use = (await this.#keepCard(fp, answer.card)) || card;
        prekeys = isObject(answer.prekeys) ? answer.prekeys : {};
      }
    }
    const sent = await Promise.all(others(use).map((d) =>
      this.#sendToDevice(fp, use, d, prekeys[b64(d.id)], plain, route, target, token)));
    return sent.every(Boolean);
  }

  async #sendToDevice(fp, card, device, opk, plain, route, target, token) {
    const id = this.#identity();
    const ownDevice = this.#ownDevice();
    if (!id || !ownDevice) return false;
    let bundleOpk = null;
    if (isObject(opk)) {
      const pub = unb64(opk.pub, 65);
      if (pub && (await fc.validPublic(pub)) && num(opk.id) > 0) bundleOpk = { id: num(opk.id) >>> 0, pub };
    }
    const key = sessionKey(fp, device.id);
    const note = await this.#exclusive(async () => {
      const list = this.#sessionsOf(key).slice();
      let session = list[0];
      if (!isObject(session) || !session.r) {
        // A new conversation with this device: X3DH with its signed prekey, and a one-time prekey if the server had one
        const bundle = { fp: card.fp, ikd: card.ikd, device: device.id, spkId: device.spkId, spk: device.spk,
          opkId: bundleOpk ? bundleOpk.id : 0, opk: bundleOpk ? bundleOpk.pub : null };
        const start = await fc.x3dhInitiate(id, ownDevice, bundle);
        const r = start && (await fc.ratchetInitInitiator(start.sk, device.spk));
        if (!r) return null;
        session = { ad: start.ad, x3dh: { ek: b64(start.ek.pub), spk_id: bundle.spkId, opk_id: bundle.opkId }, r };
        list.unshift(session);
      }
      const e = { kind: 2, fromFp: id.fp, fromDevice: ownDevice, toDevice: device.id };
      // Until the other device has answered, every message carries what X3DH needs
      if (isObject(session.x3dh)) {
        e.kind = 1;
        e.ek = unb64(session.x3dh.ek, 65);
        e.spkId = num(session.x3dh.spk_id);
        e.opkId = num(session.x3dh.opk_id);
        if (!e.ek) return null;
      }
      const sealed = await fc.ratchetEncrypt(session.r, session.ad, plain);
      if (!sealed) return null;
      e.header = sealed.header;
      e.sealed = sealed.sealed;
      const bytes = fc.encodeEnvelope(e);
      // Kept before it goes out: one message key is never used twice, even after a crash
      this.#storeSessions(key, list);
      if (!(await this.#save()) || !bytes) return null;
      return b64(bytes);
    });
    if (!note) return false;
    let r;
    if (route === 'request') r = await this.#call('request_put', { box: target, note });
    else if (route === 'group') r = await this.#call('group_put', { box: target, token, note });
    else r = await this.#call('inbox_put', { fp, device: b64(device.id), token: target, note });
    return r.ok;
  }

  // ---------------------------------------------------------------- receiving

  /** Notes from an inbox or the request box: unknown senders' cards first, then each one, then the ack. */
  async #takeNotes(notesIn, fromRequestBox, ackField, ackValue) {
    const notes = Array.isArray(notesIn) ? notesIn.filter(isObject) : [];
    if (!notes.length) return;
    const ownDevice = this.#ownDevice();
    if (!ownDevice) return;
    const read = notes.map((n) => {
      const bytes = unb64(n.note);
      return { id: str(n.id), bytes, e: bytes ? fc.decodeEnvelope(bytes) : null };
    });
    // Who starts a conversation is looked up first: X3DH needs their card
    const cards = isObject(this.#state.cards) ? this.#state.cards : {};
    const unknown = new Set();
    for (const { e } of read) {
      if (e && bytesEqual(e.toDevice, ownDevice) && e.kind === 1 && !cards[b64(e.fromFp)]) unknown.add(b64(e.fromFp));
    }
    // Senders the server has no card of: their notes can never be opened and go (a box full of
    // them would keep real requests out)
    const gone = new Set();
    await Promise.all([...unknown].map(async (fp) => {
      const { ok, answer } = await this.#call('card_get', { fp });
      if (ok && (await this.#keepCard(fp, answer.card))) this.#cardFetched.set(fp, Date.now());
      else if (answer.error === 'unknown') gone.add(fp);
    }));
    const ids = [];
    for (const { id, bytes, e } of read) {
      // The request box is shared by the own devices: each takes away only its own notes
      if (e && fromRequestBox && !bytesEqual(e.toDevice, ownDevice)) continue;
      if (!e || (await this.#handleEnvelope(bytes, fromRequestBox)) || gone.has(b64(e.fromFp))) ids.push(id);
    }
    if (ids.length) await this.#call('inbox_ack', { [ackField]: ackValue, ids: ids.join(',') }, true);
  }

  /** False if it has to wait (the sender's card is missing); true once it is done with, opened or not. */
  async #handleEnvelope(bytes, fromRequestBox, group = '') {
    const opened = await this.#exclusive(() => this.#openEnvelope(bytes, fromRequestBox, group));
    if (opened.payload) await this.#handlePayload(opened.from, opened.payload, opened.own, opened.device, group);
    return opened.done;
  }

  async #openEnvelope(bytes, fromRequestBox, group) {
    const e = fc.decodeEnvelope(bytes);
    const id = this.#identity();
    const ownDevice = this.#ownDevice();
    if (!e || !id || !ownDevice || !bytesEqual(e.toDevice, ownDevice)) return { done: true };
    const from = b64(e.fromFp);
    const own = from === this.#state.fp;
    const f = this.#friendsObj()[from];
    if ((f && f.state === 'blocked') || (own && bytesEqual(e.fromDevice, ownDevice))) return { done: true };
    // Strangers reach only the request box, and members of a shared group its box
    const g = group ? this.#group(group) : null;
    const member = !!g && fpsOf(g.members).includes(from);
    if (!own && !f && !fromRequestBox && !member) return { done: true };
    const key = sessionKey(from, e.fromDevice);
    const list = this.#sessionsOf(key).slice();
    const ek = b64(e.ek);
    const now = Date.now();
    let plain = null;
    let session = null;
    let used = -1;
    // A conversation already there: the newest first, then the older ones
    for (let i = 0; i < list.length && used < 0; i++) {
      const s = list[i];
      if (!isObject(s) || !s.r || !s.ad) continue;
      if (e.kind === 1 && s.their_ek !== ek) continue;
      const p = await fc.ratchetDecrypt(s.r, s.ad, e.header, e.sealed, now);
      if (!p) continue;
      // They answered: our messages need no X3DH part any more
      delete s.x3dh;
      session = s;
      used = i;
      plain = p;
    }
    if (used < 0) {
      // A first message that started a conversation before is a copy: starting it again would
      // put a second conversation next to the one the other side goes on with
      if (e.kind !== 1 || this.#seenEk(key, ek)) return { done: true };
      // A new conversation: X3DH with this device's prekeys and their identity from their card
      const cardText = isObject(this.#state.cards) ? this.#state.cards[from] : null;
      const card = cardText ? await this.#decode(cardText) : null;
      if (!card) return { done: false };
      const dev = this.#state.device;
      let spk = null;
      if (dev.spk && num(dev.spk_id) === e.spkId) spk = dev.spk;
      else if (dev.spk_prev && num(dev.spk_prev_id) === e.spkId) spk = dev.spk_prev;
      // A first message for a signed prekey gone, or a one-time prekey used already
      if (!spk) return { done: true };
      let opk = null;
      if (e.opkId) {
        opk = isObject(dev.opks) ? dev.opks[e.opkId] : null;
        if (!opk) return { done: true };
      }
      const x = await fc.x3dhRespond(id, ownDevice, spk, opk, e.fromFp, card.ikd, e.fromDevice, e.ek);
      if (!x) return { done: true };
      const r = fc.ratchetInitResponder(x.sk, spk);
      plain = await fc.ratchetDecrypt(r, x.ad, e.header, e.sealed, now);
      if (!plain) return { done: true };
      session = { their_ek: ek, ad: x.ad, r };
      if (e.opkId) delete dev.opks[e.opkId];
      list.unshift(session);
      used = 0;
    }
    // The one that worked is the one to answer with
    list.splice(used, 1);
    list.unshift(session);
    this.#storeSessions(key, list);
    if (e.kind === 1) this.#rememberEk(key, ek);
    await this.#save();
    return { done: true, payload: parseObject(decoder.decode(plain)), from, own, device: hex(e.fromDevice) };
  }

  /** Reads the link key a request or a yes carries and the profile it opens on their card. */
  async #profileFrom(from, lkText) {
    const lk = unb64(lkText, 16);
    if (!lk) return null;
    const out = { lk: hex(lk) };
    const cardText = isObject(this.#state.cards) ? this.#state.cards[from] : null;
    const card = cardText ? await this.#decode(cardText) : null;
    const json = card ? await fc.openProfile(lk, card.fp, card.profile) : null;
    if (json !== null) {
      const profile = parseObject(json);
      out.name = str(profile.name).slice(0, 32);
      const avatar = str(profile.avatar);
      out.avatar = avatar.length <= 16000 ? avatar : '';
    }
    return out;
  }

  async #handlePayload(from, p, ownDevice, fromDevice = '', group = '') {
    if (!this.#enabled) return;
    const t = str(p.t);
    const friends = this.#friendsObj();
    const f = friends[from];
    const fState = f ? str(f.state) : '';

    // Somebody who is no friend: from the request box only what a request needs, from a group's
    // box only what the group needs
    if (!ownDevice && fState !== 'friend') {
      const allowed = group ? ['group', 'senderkey', 'leave'] : ['request', 'accept', 'decline', 'card'];
      if (!allowed.includes(t)) return;
    }
    // What concerns a group: from any member (friends or not), through any way it came
    if (t === 'group') {
      await this.#applyGroup(from, p);
      return;
    }
    if (t === 'senderkey' || t === 'leave') {
      const gid = str(p.group);
      const g = this.#group(gid);
      if (!g || g.left === true || !fpsOf(g.members).includes(from)) return;
      if (t === 'leave') {
        // One admin takes them out (the one with the lowest fingerprint), on one of its devices:
        // two would each make a token of their own
        const admins = fpsOf(g.admins).map((a) => unb64(a, 16)).sort(fc.compareBytes);
        if (!ownDevice && admins.length && b64(admins[0]) === this.#state.fp && this.#actingDevice()) {
          await this.removeFromGroup(gid, from, false);
        }
        return;
      }
      const ck = unhex(p.ck, 32);
      const pub = unb64(p.pub, 65);
      if (!fromDevice || !ck || !pub || !(await fc.validPublic(pub))) return;
      const now = this.#group(gid);
      if (!now || now.left === true) return;
      if (!isObject(now.chains)) now.chains = {};
      now.chains[`${from}:${fromDevice}`] = { chain: num(p.chain) >>> 0, iter: num(p.iter) >>> 0, ck: hex(ck), pub: b64(pub), skipped: {} };
      this.#keysCame++;
      const waiting = Array.isArray(now.waiting) ? now.waiting : [];
      now.waiting = [];
      await this.#save();
      // Messages that waited for this key
      if (waiting.length) this.#spawn(this.#takeGroupNotes(gid, waiting));
      return;
    }
    // Their card changed (a new device): fetched again before the next message
    if (t === 'card') {
      this.#cardFetched.delete(from);
      return;
    }
    if (ownDevice) {
      // What another device of the own identity did
      if (t === 'sync') {
        const of = isObject(p.of) ? p.of : {};
        const to = str(p.to);
        if (of.t === 'seen') await this.#markSeenUpTo(to, num(of.upto_at));
        else if (of.t === 'vote') await this.#applyVote(to, str(of.plan), this.#state.fp, of.answers);
        else if (of.t === 'react') await this.#applyReaction(to, str(of.msg), this.#state.fp, str(of.emoji), of.on === true);
        else if (of.t === 'delete') await this.#applyDelete(to, str(of.msg), this.#state.fp);
        else if (of.t === 'text' || of.t === 'join' || of.t === 'plan') {
          const mine = { ...of, from: this.#state.fp, mine: true, state: 'sent' };
          for (const key of ['embed', 'reply', 'quote', 'deleted', 'system', 'reactions', 'votes']) delete mine[key];
          if (of.t === 'text') {
            const embed = cleanEmbed(of.embed, str(of.text));
            if (Object.keys(embed).length) mine.embed = embed;
            Object.assign(mine, cleanReply(of));
          }
          await this.#addMessage(to, mine);
        }
      } else if (t === 'own') {
        // A friend's record, the friends key or the link key changed on another own device
        const record = isObject(p.friend) ? p.friend : {};
        const who = str(record.fp);
        if (who && who !== this.#state.fp) {
          if (record.removed === true) {
            delete friends[who];
            this.#statuses.delete(who);
          } else {
            const merged = isObject(friends[who]) ? friends[who] : {};
            for (const [k, v] of Object.entries(record)) if (k !== 'fp') merged[k] = v;
            friends[who] = merged;
          }
          // Their name and picture come with their card, fetched next time
          this.#cardFetched.delete(who);
        }
        if (isObject(p.fk)) this.#state.fk = p.fk;
        if (typeof p.lk === 'string') this.#state.lk = p.lk;
        this.#changed();
        await this.#save();
      }
      return;
    }

    if (t === 'request') {
      if (fState === 'friend' || fState === 'blocked') return;
      const profile = await this.#profileFrom(from, p.lk);
      const now = this.#friendsObj()[from];
      const nowState = now ? str(now.state) : '';
      if (!profile || nowState === 'friend' || nowState === 'blocked') return;
      // Both asked each other at once: that is a yes from both sides
      const both = nowState === 'requested';
      const record = { ...(now || {}), ...profile, state: both ? 'friend' : 'incoming', token: str(p.token),
        text: str(p.text).slice(0, 200), since: Date.now() };
      this.#friendsObj()[from] = record;
      if (both) this.#sendFriendsKey(from);
      else this.#emit('request', { fp: from, name: str(record.name) });
      this.#changed();
      await this.#save();
    } else if (t === 'accept') {
      if (fState !== 'requested') return;
      const profile = await this.#profileFrom(from, p.lk);
      const now = this.#friendsObj()[from];
      if (!profile || !now || now.state !== 'requested') return;
      this.#friendsObj()[from] = { ...now, ...profile, state: 'friend', token: str(p.token), fk: isObject(p.fk) ? p.fk : {},
        since: Date.now() };
      // And our friends key for them
      this.#sendFriendsKey(from);
      this.#spawn(this.#putStatus());
      this.#changed();
      await this.#save();
    } else if (fState !== 'friend') {
      // From here on: friends only, but a "no" to our request
      if (t === 'decline' && fState === 'requested') {
        if (str(f.my_token)) this.#removeToken(f.my_token);
        delete friends[from];
        this.#changed();
        await this.#save();
      }
    } else if (t === 'fk') {
      const fk = {};
      if (p.key !== undefined) fk.key = p.key;
      if (p.epoch !== undefined) fk.epoch = p.epoch;
      f.fk = fk;
      await this.#save();
    } else if (t === 'lk') {
      const lk = unb64(p.lk, 16);
      if (lk) {
        f.lk = hex(lk);
        await this.#save();
      }
    } else if (t === 'vote') {
      await this.#applyVote(from, str(p.plan), from, p.answers);
    } else if (t === 'react') {
      await this.#applyReaction(from, str(p.msg), from, str(p.emoji), p.on === true);
    } else if (t === 'delete') {
      await this.#applyDelete(from, str(p.msg), from);
    } else if (t === 'seen') {
      await this.#applyRead(from, from, num(p.upto_at));
    } else if (t === 'text' || t === 'join' || t === 'plan') {
      const m = {};
      if (p.id !== undefined) m.id = p.id;
      if (p.at !== undefined) m.at = p.at;
      m.text = str(p.text).slice(0, 4000);
      m.from = from;
      m.mine = false;
      if (t === 'text') {
        const embed = cleanEmbed(p.embed, m.text);
        if (Object.keys(embed).length) m.embed = embed;
        Object.assign(m, cleanReply(p));
      }
      if (t === 'join') {
        const card = cleanRoom(p.join);
        if (!Object.keys(card).length) return;
        m.join = card;
      }
      if (t === 'plan') {
        const plan = cleanPlan(p.plan);
        if (!plan) return;
        m.plan = plan;
      }
      const isNew = !this.#messageIn(from, str(m.id));
      await this.#addMessage(from, m);
      if (isNew) this.#notify(from, str(f.name), m.text);
    } else if (t === 'unfriend') {
      // Their write token for this identity's inboxes goes too
      if (str(f.my_token)) this.#removeToken(f.my_token);
      delete friends[from];
      this.#statuses.delete(from);
      this.#changed();
      await this.#save();
    }
  }
}
