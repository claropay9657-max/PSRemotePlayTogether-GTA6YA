// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The page's friends client (web/js/friends/client.js) end to end, as test/friends-client.cpp
// does it for the app's: clients with stores of their own against a real friends server
// (server/friends/psrpt_friends.php, started here with php -S on a free port and an empty data
// folder). Then the page and the app as friends: the app's client as a scripted peer
// (build/gui/friends-client-test --peer) answers a request and echoes messages.
//
// Without php, or without the built peer, those tests are skipped.
// Run: node --experimental-default-type=module --test test/web/*.test.mjs

import { after, test } from 'node:test';
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import readline from 'node:readline';
import { fileURLToPath } from 'node:url';

import { base64UrlDecode, base64UrlEncode, hex } from '../../web/js/bytes.js';
import { chatHtml, withEmoji } from '../../web/js/friends/chattext.js';
import { FriendsClient, MUTE_FOREVER, cleanAnswers, cleanEmbed, cleanPlan, cleanReply, cleanRoom, fpsOf, groupSigned, jpegSize,
  parseFriendLink } from '../../web/js/friends/client.js';
import * as fc from '../../web/js/friends/crypto.js';
import { p256Public, publicFromPrivate } from '../../web/js/friends/p256.js';
import { MemoryStore, WriteQueue } from '../../web/js/friends/store.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '..', '..');
const PHP = spawnSync('php', ['-v'], { stdio: 'ignore' }).status === 0;
const PEER = path.join(root, 'build', 'gui', 'friends-client-test');
const HAVE_PEER = (() => {
  try {
    fs.accessSync(PEER, fs.constants.X_OK);
    return true;
  } catch (e) {
    return false;
  }
})();

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const b64 = base64UrlEncode;
const h = (s) => new Uint8Array((s.match(/../g) || []).map((x) => parseInt(x, 16)));

/** The card the server keeps for a fingerprint, read. */
async function serverCard(url, fp) {
  const answer = await (await fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams({ v: '1', op: 'card_get', fp }).toString() })).json();
  return answer.ok ? fc.decodeCard(base64UrlDecode(answer.card)) : null;
}

// ------------------------------------------------------------------ the friends server

const cleanup = [];
let serverStarted = null;

function freePort() {
  return new Promise((resolve, reject) => {
    const s = net.createServer();
    s.on('error', reject);
    s.listen(0, '127.0.0.1', () => {
      const { port } = s.address();
      s.close(() => resolve(port));
    });
  });
}

/** php -S with the server script and an empty data folder, once for the whole file. */
function friendsServer() {
  serverStarted ||= (async () => {
    const work = fs.mkdtempSync(path.join(os.tmpdir(), 'psrpt-friends-web-'));
    fs.mkdirSync(path.join(work, 'www'));
    fs.mkdirSync(path.join(work, 'data'));
    fs.copyFileSync(path.join(root, 'server', 'friends', 'psrpt_friends.php'), path.join(work, 'www', 'psrpt_friends.php'));
    const log = fs.openSync(path.join(work, 'php.log'), 'w');
    const port = await freePort();
    const proc = spawn('php', ['-S', `127.0.0.1:${port}`, '-t', path.join(work, 'www')], {
      // Every player here comes from one address: the limits per address an hour are raised
      env: { ...process.env, PSRPT_FRIENDS_DIR: path.join(work, 'data'), PSRPT_FRIENDS_IDENTITIES_PER_HOUR: '1000',
        PSRPT_FRIENDS_REQUESTS_PER_HOUR: '1000', PSRPT_FRIENDS_BUNDLES_PER_HOUR: '10000', PSRPT_FRIENDS_GROUP_CHANGES_PER_HOUR: '1000',
        PSRPT_FRIENDS_GROUPS_PER_HOUR: '1000', PSRPT_FRIENDS_LINKS_PER_HOUR: '1000', PSRPT_FRIENDS_REPORTS_PER_HOUR: '100' },
      stdio: ['ignore', log, log],
    });
    cleanup.push(() => {
      proc.kill('SIGKILL');
      fs.closeSync(log);
      fs.rmSync(work, { recursive: true, force: true });
    });
    const url = `http://127.0.0.1:${port}/psrpt_friends.php`;
    for (let i = 0; i < 200; i++) {
      try {
        if ((await fetch(url)).ok) return { url, log: path.join(work, 'php.log'), www: path.join(work, 'www') };
      } catch (e) {
        // not listening yet
      }
      await sleep(25);
    }
    throw new Error('the friends server did not start');
  })();
  return serverStarted;
}

after(() => {
  for (const f of cleanup.reverse()) {
    try {
      f();
    } catch (e) {
      // gone already
    }
  }
});

// ------------------------------------------------------------------ helpers

/**
 * A memory store that remembers, of every state it was given, how far each sending chain had
 * got (the ratchet's own public key and its message number): a message may go out only once
 * its ratchet is stored past it.
 */
class RecordingStore extends MemoryStore {
  chains = new Map();

  async save(state) {
    await super.save(state);
    for (const entry of Object.values(state.sessions || {})) {
      for (const s of entry.s || []) {
        if (!s.r || !s.r.dhs) continue;
        const key = hex(s.r.dhs.pub);
        this.chains.set(key, Math.max(this.chains.get(key) ?? 0, s.r.ns));
      }
    }
  }
}

/** A fetch that checks every envelope against what the sender had stored before it went. */
function checkedFetch(store, early) {
  return async (url, init) => {
    const form = new URLSearchParams(init.body);
    const op = form.get('op');
    if (op === 'inbox_put' || op === 'request_put') {
      const e = fc.decodeEnvelope(base64UrlDecode(form.get('note')));
      if (!e || !((store.chains.get(hex(e.header.dh)) ?? 0) > e.header.n)) early.push(op);
    }
    return fetch(url, init);
  };
}

const early = [];

async function makeClient(url, name, store = new RecordingStore()) {
  const c = new FriendsClient({ timers: false, fetch: checkedFetch(store, early) });
  c.store = store;
  await c.setStorage(store);
  await c.setServer(url);
  await c.setProfile(name);
  return c;
}

/** Until every client has nothing under way. */
function settle(...clients) {
  return Promise.all(clients.map((c) => c.idle()));
}

/** Asks the server again (each client in turn) and lets everything that follows finish. */
async function refresh(...clients) {
  for (const c of clients) {
    await c.refresh();
    await settle(...clients);
  }
}

/** x asks y, y says yes: friends on both sides. */
async function befriend(x, y) {
  await x.sendRequest(y.link, '');
  await settle(x, y);
  await refresh(y);
  await y.accept(x.fingerprint);
  await settle(x, y);
  await refresh(x, y, x);
  assert.equal(entry(x.friends, 'fp', y.fingerprint).fp, y.fingerprint);
  assert.equal(entry(y.friends, 'fp', x.fingerprint).fp, x.fingerprint);
}

function entry(list, key, value) {
  return list.find((x) => x[key] === value) || {};
}

function texts(c, chat, mine) {
  return c.messages(chat).filter((m) => (m.mine === true) === mine).map((m) => m.text);
}

async function until(what, check, ms = 30000) {
  const end = Date.now() + ms;
  for (;;) {
    if (await check()) return;
    if (Date.now() > end) throw new Error(`waited in vain for ${what}`);
    await sleep(200);
  }
}

// ------------------------------------------------------------------ the pieces

test('friend links as the app reads them', () => {
  const fp = 'AAECAwQFBgcICQoLDA0ODw';
  const lk = '_-7t7Ovq6ejn5uXk4-Lh4A';
  for (const link of [`psrpt://friend/${fp}.${lk}`, `https://crypto90.github.io/PSRemotePlayTogether/#f=${fp}.${lk}`, ` ${fp}.${lk} `]) {
    const p = parseFriendLink(link);
    assert.ok(p, link);
    assert.equal(hex(p.fp), '000102030405060708090a0b0c0d0e0f');
    assert.equal(p.lk.length, 16);
    assert.equal(p.at, '');
  }
  const other = parseFriendLink(`psrpt://friend/${fp}.${lk}?at=https%3A%2F%2Fexample.org%2Ff.php`);
  assert.equal(other.at, 'https://example.org/f.php');
  assert.equal(parseFriendLink('not a link'), null);
  assert.equal(parseFriendLink(`psrpt://friend/${fp}`), null);
  assert.equal(parseFriendLink(`psrpt://friend/${fp}.AAAA`), null);
});

test('chat text as the app shows it', () => {
  assert.equal(withEmoji('gg :FIRE: :) a:)b <3'), 'gg 🔥 🙂 a:)b ❤️');
  assert.equal(chatHtml('<b>&"</b>\nsee https://example.org/a?b=1&c=2.'),
    '&lt;b&gt;&amp;&quot;&lt;/b&gt;<br>see <a href="https://example.org/a?b=1&amp;c=2" target="_blank" rel="noopener noreferrer">'
    + 'https://example.org/a?b=1&amp;c=2</a>.');
});

test('a room as friends see it: only what the app shows, each checked', () => {
  assert.deepEqual(cleanRoom({ title: 'no invite' }), {});
  assert.deepEqual(cleanRoom({ invite: 'javascript:alert(1)' }), {});
  assert.deepEqual(cleanRoom({
    invite: 'psrpt://join/abc', web: 'http://not-https.example', title: ' Friday\n racing ', host: 'x'.repeat(100), game: '',
    cover: 'https://image.api.playstation.com/vulcan/img/a.png', console: 'ps5', policy: 'open', control: 'turns',
    players: 3, max: 4, talk: true, secret: 1,
  }), {
    invite: 'psrpt://join/abc', title: 'Friday racing', host: 'x'.repeat(80), cover: 'https://image.api.playstation.com/vulcan/img/a.png',
    console: 'ps5', policy: 'open', control: 'turns', players: 3, max: 4, talk: true,
  });
  // Pictures from anywhere else, odd counts and unknown words go
  assert.deepEqual(cleanRoom({ invite: 'https://x.example/#r', cover: 'https://image.api.playstation.com.evil/x', console: 'switch',
    players: 5, max: 4 }), { invite: 'https://x.example/#r' });
  assert.deepEqual(cleanRoom({ invite: 'https://x.example/#r', players: 1.5, max: 4 }), { invite: 'https://x.example/#r' });
});

/** A real JPEG of 32 by 18 pixels (made with macOS sips), for link previews. */
const TINY_JPEG = '/9j/4AAQSkZJRgABAQAASABIAAD/4QCARXhpZgAATU0AKgAAAAgABAEaAAUAAAABAAAAPgEbAAUAAAABAAAARgEoAAMAAAABAAIAAIdpAAQAAAABAAAATgAAAAAAAABIAAAAAQAAAEgAAAABAAOgAQADAAAAAQABAACgAgAEAAAAAQAAACCgAwAEAAAAAQAAABIAAAAA/+0AOFBob3Rvc2hvcCAzLjAAOEJJTQQEAAAAAAAAOEJJTQQlAAAAAAAQ1B2M2Y8AsgTpgAmY7PhCfv/AABEIABIAIAMBIgACEQEDEQH/xAAfAAABBQEBAQEBAQAAAAAAAAAAAQIDBAUGBwgJCgv/xAC1EAACAQMDAgQDBQUEBAAAAX0BAgMABBEFEiExQQYTUWEHInEUMoGRoQgjQrHBFVLR8CQzYnKCCQoWFxgZGiUmJygpKjQ1Njc4OTpDREVGR0hJSlNUVVZXWFlaY2RlZmdoaWpzdHV2d3h5eoOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4eLj5OXm5+jp6vHy8/T19vf4+fr/xAAfAQADAQEBAQEBAQEBAAAAAAAAAQIDBAUGBwgJCgv/xAC1EQACAQIEBAMEBwUEBAABAncAAQIDEQQFITEGEkFRB2FxEyIygQgUQpGhscEJIzNS8BVictEKFiQ04SXxFxgZGiYnKCkqNTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqCg4SFhoeIiYqSk5SVlpeYmZqio6Slpqeoqaqys7S1tre4ubrCw8TFxsfIycrS09TV1tfY2dri4+Tl5ufo6ery8/T19vf4+fr/2wBDAAICAgICAgQCAgQGBAQEBggGBgYGCAoICAgICAoMCgoKCgoKDAwMDAwMDAwODg4ODg4QEBAQEBISEhISEhISEhL/2wBDAQMDAwUEBQgEBAgTDQsNExMTExMTExMTExMTExMTExMTExMTExMTExMTExMTExMTExMTExMTExMTExMTExMTExP/3QAEAAL/2gAMAwEAAhEDEQA/AKVj2rtLHtXF2Pau0se1f13i+p8jxp9o7Sx7V2lj2ri7HtXaWPavmMX1P5J40+0f/9ClY9q7Sx7Vxdj2rtLHtX9d4vqfI8afaO0se1dpY9q4ux7V2lj2r5jF9T+SeNPtH//Z';

/** The JPEG with another size in its frame header (it still says JPEG, but would decode huge). */
function withJpegSize(base64, width, height) {
  const b = Uint8Array.from(atob(base64), (c) => c.charCodeAt(0));
  for (let i = 2; i + 8 < b.length;) {
    const marker = b[i + 1];
    if (marker === 0xc0 || marker === 0xc2) {
      b[i + 5] = height >> 8;
      b[i + 6] = height & 255;
      b[i + 7] = width >> 8;
      b[i + 8] = width & 255;
      break;
    }
    i += 2 + ((b[i + 2] << 8) | b[i + 3]);
  }
  return btoa(String.fromCharCode(...b));
}

test('a link preview as friends see it: the app\'s CleanEmbed', () => {
  assert.deepEqual(jpegSize(Uint8Array.from(atob(TINY_JPEG), (c) => c.charCodeAt(0))), { width: 32, height: 18 });
  const text = 'Look https://example.org/track?id=1 !';
  const good = cleanEmbed({ url: 'https://example.org/track?id=1', site: ' Racing   club ', title: 'Friday\'s track',
    description: 'Twelve corners', image: TINY_JPEG, extra: 'dropped' }, text);
  assert.deepEqual(good, { url: 'https://example.org/track?id=1', site: 'Racing club', title: 'Friday\'s track',
    description: 'Twelve corners', image: TINY_JPEG });
  // Only for the link in the message: a preview never leads anywhere else
  assert.deepEqual(cleanEmbed({ url: 'https://evil.example/', title: 'Bank' }, text), {});
  assert.deepEqual(cleanEmbed({ url: 'javascript:alert(1)', title: 'x' }, 'javascript:alert(1)'), {});
  assert.deepEqual(cleanEmbed({ url: 'ftp://example.org/', title: 'x' }, 'ftp://example.org/'), {});
  assert.deepEqual(cleanEmbed('https://example.org/', text), {});
  // As the writer's app wrote it, percent-encoded
  assert.equal(cleanEmbed({ url: 'https://de.wikipedia.org/wiki/%C3%84', title: 'Ä' }, 'see https://de.wikipedia.org/wiki/Ä.').title, 'Ä');
  // Nothing to show but the link: no preview
  assert.deepEqual(cleanEmbed({ url: 'https://example.org/track?id=1' }, text), {});
  // Texts cut to their bytes, never inside a character
  const long = cleanEmbed({ url: 'https://example.org/track?id=1', title: '🔥'.repeat(40), site: 'x'.repeat(80) }, text);
  assert.equal(long.title, '🔥'.repeat(25));
  assert.equal(long.site, 'x'.repeat(48));
  // The picture: a small JPEG, its size from its header
  const noPicture = (image) => !('image' in cleanEmbed({ url: 'https://example.org/track?id=1', title: 't', image }, text));
  assert.ok(noPicture(withJpegSize(TINY_JPEG, 20000, 20000)));
  assert.ok(noPicture(withJpegSize(TINY_JPEG, 801, 10)));
  assert.ok(!noPicture(withJpegSize(TINY_JPEG, 800, 450)));
  assert.ok(noPicture(btoa('\x89PNG\r\n\x1a\n' + 'x'.repeat(100))));
  assert.ok(noPicture('not base64!'));
  assert.ok(noPicture(TINY_JPEG + 'A'.repeat(7000)));
  assert.ok(noPicture(btoa(String.fromCharCode(0xff, 0xd8, 0xff, 0xd9))));
});

test('plans, answers and fingerprint lists as the app takes them', () => {
  assert.equal(cleanPlan({ title: 'x' }), null);
  assert.equal(cleanPlan({ times: [] }), null);
  assert.deepEqual(cleanPlan({ title: '  Racing\n night ', times: [1, -5, 'x', 2.5, 3, 4, 5, 6, 7, 8, 9] }),
    { title: 'Racing night', times: [1, 2.5, 3, 4, 5, 6, 7, 8] });
  assert.equal(cleanPlan({ title: 'y'.repeat(90), times: [1] }).title.length, 80);
  assert.deepEqual(cleanAnswers([2, 1, 0, 5, -1, 1.5, 'x'], 8), [2, 1, 0, 2, 0, 0, 0, 0]);
  assert.deepEqual(cleanAnswers(null, 2), [0, 0]);
  const fp = 'AAECAwQFBgcICQoLDA0ODw';
  assert.deepEqual(fpsOf([fp, fp, 'short', 7, null, '_-7t7Ovq6ejn5uXk4-Lh4A']), [fp, '_-7t7Ovq6ejn5uXk4-Lh4A']);
});

test('what an admin signs for a group: lists in the order of their bytes', () => {
  const gid = new Uint8Array(16).fill(0x80);
  // In base64url text "_" sorts after "A"; as bytes 0xff... comes after 0x00...
  const low = base64UrlEncode(new Uint8Array(16));
  const high = base64UrlEncode(new Uint8Array(16).fill(0xff));
  const mid = base64UrlEncode(new Uint8Array(16).fill(0x7c));
  const a = groupSigned(gid, 3, 'Friday', 'all', [high, low, mid], [mid], []);
  const b = groupSigned(gid, 3, 'Friday', 'all', [mid, low, high], [mid], []);
  assert.deepEqual(a, b);
  const head = new TextEncoder().encode('psrpt group');
  assert.deepEqual(a.subarray(0, head.length), head);
  let at = head.length + 16;
  assert.deepEqual([...a.subarray(at, at + 4)], [0, 0, 0, 3]);
  at += 4;
  assert.deepEqual([...a.subarray(at, at + 2)], [0, 6]);
  at += 2 + 6;
  assert.equal(a[at], 1);
  assert.equal(a[at + 1], 3);
  assert.deepEqual(a.subarray(at + 2, at + 18), new Uint8Array(16));
  assert.deepEqual(a.subarray(at + 34, at + 50), new Uint8Array(16).fill(0xff));
  assert.notDeepEqual(groupSigned(gid, 3, 'Friday', 'admins', [low], [low], []), groupSigned(gid, 3, 'Friday', 'all', [low], [low], []));
});

test('the public key of a private key, computed and from WebCrypto alike', async () => {
  for (let i = 0; i < 3; i++) {
    const pair = await crypto.subtle.generateKey({ name: 'ECDH', namedCurve: 'P-256' }, true, ['deriveBits']);
    const jwk = await crypto.subtle.exportKey('jwk', pair.privateKey);
    const d = base64UrlDecode(jwk.d);
    const pub = new Uint8Array(await crypto.subtle.exportKey('raw', pair.publicKey));
    assert.deepEqual(p256Public(d), pub);
    assert.deepEqual(await publicFromPrivate(d), pub);
  }
  assert.equal(p256Public(new Uint8Array(32)), null);
  assert.equal(p256Public(new Uint8Array(32).fill(0xff)), null);
});

test('the store: copies, one write at a time, a save waits for a write after it', async () => {
  const memory = new MemoryStore();
  assert.equal(await memory.load(), null);
  const state = { a: 1, keys: new Map([['k', new Uint8Array([1])]]) };
  await memory.save(state);
  state.a = 2;
  assert.equal((await memory.load()).a, 1);
  await memory.clear();
  assert.equal(await memory.load(), null);

  // A slow store that notes when writes start and end
  const log = [];
  let running = 0;
  const slow = {
    async save(s) {
      running++;
      assert.equal(running, 1, 'two writes at once');
      log.push(`start ${s.n}`);
      await sleep(20);
      log.push(`end ${s.n}`);
      running--;
    },
    async clear() {
      log.push('clear');
    },
  };
  const live = { n: 0 };
  const queue = new WriteQueue(slow, () => ({ ...live }));
  live.n = 1;
  const first = queue.save();
  await sleep(5);
  // The first write is under way: these wait for one after it, together
  live.n = 2;
  const second = queue.save();
  live.n = 3;
  const third = queue.save();
  assert.notEqual(first, second);
  assert.equal(second, third, 'saves asked for while one waits share it');
  await first;
  assert.deepEqual(log, ['start 1', 'end 1']);
  await third;
  // The second write took the state as it was when it started
  assert.deepEqual(log, ['start 1', 'end 1', 'start 3', 'end 3']);
  // A clear comes after the save asked for before it, and a save after the clear after that
  live.n = 4;
  const saved = queue.save();
  const cleared = queue.clear();
  live.n = 5;
  await Promise.all([saved, cleared, queue.save()]);
  assert.deepEqual(log.slice(4), ['start 5', 'end 5', 'clear', 'start 5', 'end 5']);
});

test('a store another tab has is neither read nor written', async () => {
  const writes = [];
  const taken = {
    claim: async () => false,
    load: async () => {
      writes.push('load');
      return null;
    },
    save: async () => writes.push('save'),
    clear: async () => writes.push('clear'),
  };
  const c = new FriendsClient({ timers: false, fetch: () => Promise.reject(new Error('no network in this test')) });
  await c.setStorage(taken);
  assert.equal(c.problem, 'Friends are open in another tab of this browser.');
  await c.enable();
  await c.setMyState('away');
  assert.equal(await c.restoreBackup('PSRPT-BACKUP 1\nAAAA', 'correct horse battery'), false);
  await c.idle();
  assert.ok(!c.enabled);
  assert.deepEqual(writes, []);
});

// ------------------------------------------------------------------ two browsers

test('two browsers: a request, statuses, chats, a second device, a new link, unfriending, blocking, a restart',
  { timeout: 240000 }, async (t) => {
    if (!PHP) {
      t.skip('php is not installed');
      return;
    }
    const { url } = await friendsServer();
    const a = await makeClient(url, 'Alice');
    const b = await makeClient(url, 'Bob');
    assert.ok(!a.enabled && a.link === '');

    // ---- on: an identity each, cards on the server
    await a.enable();
    await b.enable();
    await settle(a, b);
    assert.ok(a.enabled && b.enabled);
    assert.ok(a.online && b.online);
    assert.ok(a.link.startsWith('psrpt://friend/'));
    assert.ok(b.webLink.startsWith('https://crypto90.github.io/PSRemotePlayTogether/#f='));
    const fa = a.fingerprint;
    const fb = b.fingerprint;
    assert.ok(fa && fa !== fb);
    // A browser's card says so
    const card = await fc.decodeCard(base64UrlDecode((await a.store.load()).card));
    assert.deepEqual(card.devices.map((d) => d.kind), [2]);

    // ---- a link looked up shows the name behind it
    let looked = null;
    a.addEventListener('looked', (ev) => {
      looked = ev.detail;
    });
    await a.lookUp(b.link);
    assert.ok(looked.ok && looked.name === 'Bob' && looked.fp === fb);
    await a.lookUp('not a link');
    assert.ok(!looked.ok);
    assert.equal((await a.lookUp(a.link)).error, 'That is your own friend link.');
    // A link of the default server while this one is another
    const elsewhere = await a.lookUp(b.link.replace(/\?at=.*$/, ''));
    assert.ok(!elsewhere.ok && elsewhere.error.includes('another friends server'));

    // ---- a request, and a yes
    let requestsSeen = 0;
    b.addEventListener('request', (ev) => {
      if (ev.detail.fp === fa && ev.detail.name === 'Alice') requestsSeen++;
    });
    await a.sendRequest(b.link, 'Hi Bob, from the forum');
    await settle(a, b);
    const out = entry(a.requests, 'fp', fb);
    assert.ok(out.fp && !out.incoming && !out.unsent);
    assert.equal(out.name, 'Bob');
    await refresh(b);
    const incoming = entry(b.requests, 'fp', fa);
    assert.ok(incoming.incoming);
    assert.equal(incoming.name, 'Alice');
    assert.equal(incoming.text, 'Hi Bob, from the forum');
    assert.equal(requestsSeen, 1);
    await b.accept(fa);
    await settle(a, b);
    await refresh(a, b, a);
    assert.equal(b.requests.length, 0);
    assert.equal(a.requests.length, 0);
    assert.equal(entry(a.friends, 'fp', fb).name, 'Bob');
    assert.equal(entry(b.friends, 'fp', fa).name, 'Alice');
    // Both see the same safety code
    const safety = entry(a.friends, 'fp', fb).safety;
    assert.equal(safety.length, 29);
    assert.equal(safety, entry(b.friends, 'fp', fa).safety);

    // ---- statuses: what one does, the other sees
    let hosting = null;
    a.addEventListener('hosting', (ev) => {
      hosting = ev.detail;
    });
    await b.setActivity('Playing Astro Bot on PS5', { invite: 'https://example.invalid/#r=room', title: 'Bob\'s room' });
    await a.setMyState('busy');
    await a.setMyFree('free after 8');
    await settle(a, b);
    await refresh(a, b);
    let bob = entry(a.friends, 'fp', fb);
    // Online in a web browser
    assert.equal(bob.state, 'web');
    assert.equal(bob.activity, 'Playing Astro Bot on PS5');
    // Friends may join by default, so the room shows
    assert.equal(bob.room.title, 'Bob\'s room');
    assert.deepEqual(hosting, { fp: fb, name: 'Bob', title: 'Bob\'s room' });
    const alice = entry(b.friends, 'fp', fa);
    assert.equal(alice.state, 'busy');
    assert.equal(alice.free, 'free after 8');
    // The game hidden: the status stays, without it
    await b.setShowGame(false);
    await b.setFriendsJoin('never');
    await settle(b);
    await refresh(a);
    bob = entry(a.friends, 'fp', fb);
    assert.equal(bob.state, 'web');
    assert.equal(bob.activity, '');
    assert.equal(bob.room, null);

    // ---- chats, both ways, several in a row
    let messagesSeen = 0;
    b.addEventListener('message', (ev) => {
      if (ev.detail.chat === fa && ev.detail.name === 'Alice') messagesSeen++;
    });
    a.send(fb, 'Hello Bob');
    a.send(fb, 'Tonight at 8?');
    await settle(a);
    const sent = a.messages(fb);
    assert.equal(sent.length, 2);
    assert.equal(sent[1].state, 'sent');
    await refresh(b);
    assert.deepEqual(texts(b, fa, false), ['Hello Bob', 'Tonight at 8?']);
    assert.equal(messagesSeen, 2);
    assert.equal(b.unread, 2);
    assert.equal(entry(b.chats, 'id', fa).last, 'Tonight at 8?');
    await b.markRead(fa);
    assert.equal(b.unread, 0);
    b.send(fa, 'Yes!');
    b.send(fa, 'I\'ll host.');
    a.send(fb, 'Great');
    await settle(a, b);
    await refresh(a, b);
    assert.deepEqual(texts(a, fb, false), ['Yes!', 'I\'ll host.']);
    assert.equal(texts(b, fa, false).length, 3);
    assert.equal(texts(b, fa, false).at(-1), 'Great');
    // Emoji typed as text, and a link shown as one
    await a.send(fb, '  gg :fire: see https://example.org/x.  ');
    await settle(a);
    await refresh(b);
    const last = b.messages(fa).at(-1);
    assert.equal(last.text, 'gg 🔥 see https://example.org/x.');
    assert.ok(last.html.includes('<a href="https://example.org/x"'));
    assert.equal(last.name, 'Alice');
    // A join card, only with what the app shows; none without an invite
    await b.sendJoin(fa, { title: 'No invite' });
    await b.sendJoin(fa, { invite: 'https://example.invalid/#r=room', title: '  Bob\'s   room ', cover: 'https://tracker.example/x.png',
      players: 2, max: 4, extra: 'dropped' });
    await settle(b);
    await refresh(a);
    assert.deepEqual(a.messages(fb).at(-1).join, { invite: 'https://example.invalid/#r=room', title: 'Bob\'s room', players: 2, max: 4 });
    assert.equal(a.messages(fb).at(-1).text, 'Invites you to Bob\'s room');
    assert.equal(entry(a.chats, 'id', fb).last, 'Invites you to Bob\'s room');
    assert.equal(entry(b.chats, 'id', fa).last, 'You invited them to Bob\'s room');
    assert.ok(!texts(a, fb, false).includes('Invites you to No invite'));

    // ---- a second device of Alice, from a backup
    assert.equal(await a.saveBackup('short'), null);
    const backup = await a.saveBackup('correct horse battery');
    assert.ok(backup.startsWith('PSRPT-BACKUP 1\n'));
    const a2 = await makeClient(url, 'Alice');
    assert.equal(await a2.restoreBackup(backup, 'wrong horse battery'), false);
    assert.equal(await a2.restoreBackup(backup, 'correct horse battery'), true);
    assert.equal(a2.fingerprint, fa);
    assert.equal(a2.myState, 'busy');
    await settle(a, a2, b);
    await refresh(b, a);
    assert.equal(entry(a2.friends, 'fp', fb).name, 'Bob');
    // Bob writes once: both of Alice's devices have it
    await b.send(fa, 'To both of you');
    await settle(b);
    await refresh(a, a2);
    assert.ok(texts(a, fb, false).includes('To both of you'));
    assert.ok(texts(a2, fb, false).includes('To both of you'));
    // The new device writes: Bob has it, the first device has it as its own
    await a2.send(fb, 'Now from my laptop');
    await settle(a2);
    await refresh(b, a);
    assert.ok(texts(b, fa, false).includes('Now from my laptop'));
    assert.ok(texts(a, fb, true).includes('Now from my laptop'));
    // And the first device's own messages reach the second one
    await a.send(fb, 'Back on the desktop');
    await settle(a);
    await refresh(a2, b);
    assert.ok(texts(a2, fb, true).includes('Back on the desktop'));
    assert.ok(texts(b, fa, false).includes('Back on the desktop'));
    // Settings go to the other device too: a friend marked as checked
    await a.setChecked(fb, true);
    await settle(a);
    await refresh(a2);
    assert.equal(entry(a2.friends, 'fp', fb).checked, true);

    // ---- read again after a restart
    {
      const again = new FriendsClient({ timers: false });
      await again.setServer(url);
      await again.setStorage(a.store);
      assert.ok(again.enabled);
      assert.equal(again.fingerprint, fa);
      assert.equal(entry(again.friends, 'fp', fb).name, 'Bob');
      assert.equal(again.messages(fb).length, a.messages(fb).length);
      assert.equal(again.myState, 'busy');
      // Gone before it asks the server: the store is the first device's
      again.close();
    }
    // What a browser keeps: the identity can be exported (backups), this device's keys cannot
    const kept = await a.store.load();
    assert.ok(kept.identity.iks.ecdsa instanceof CryptoKey && kept.identity.iks.ecdsa.extractable);
    assert.equal(kept.device.spk.ecdh.extractable, false);
    assert.equal(Object.values(kept.device.opks)[0].ecdh.extractable, false);

    // ---- a new link: the old one leads nowhere, the friends stay
    const oldLink = b.link;
    await b.newLink();
    await settle(b);
    assert.notEqual(b.link, oldLink);
    await a.lookUp(oldLink);
    assert.ok(!looked.ok);
    await a.lookUp(b.link);
    assert.ok(looked.ok && looked.name === 'Bob');
    await refresh(a);
    assert.ok(entry(a.friends, 'fp', fb).fp);

    // ---- unfriending: gone on both sides, and on Alice's second device
    await b.unfriend(fa);
    await settle(b);
    await refresh(a, a2);
    assert.equal(b.friends.length, 0);
    assert.equal(a.friends.length, 0);
    assert.equal(a2.friends.length, 0);

    // ---- blocking: a new request comes, Bob blocks, the next one never shows
    await a.sendRequest(b.link, 'Sorry, again?');
    await settle(a);
    await refresh(b);
    assert.ok(entry(b.requests, 'fp', fa).incoming);
    await b.block(fa);
    await settle(b);
    assert.equal(b.requests.length, 0);
    assert.deepEqual(b.blocked.map((x) => x.fp), [fa]);
    await a.decline(fb);
    await a.sendRequest(b.link, 'Please?');
    await settle(a);
    await refresh(b);
    assert.equal(b.requests.length, 0);
    assert.equal(b.friends.length, 0);
    // Sent once more (on this server): it goes out, and still never shows
    await a.resendRequest(fb);
    await settle(a);
    assert.ok(entry(a.requests, 'fp', fb).fp && !entry(a.requests, 'fp', fb).unsent);
    await refresh(b);
    assert.equal(b.requests.length, 0);
    await b.unblock(fa);
    assert.equal(b.blocked.length, 0);

    // ---- off: nothing left in the store
    await a2.disable();
    assert.ok(!a2.enabled);
    assert.equal(a2.friends.length, 0);
    assert.equal(await a2.store.load(), null);

    // ---- every envelope went out after its ratchet was stored
    assert.deepEqual(early, []);
  });

// ------------------------------------------------------------------ more of what the app does

test('read receipts, statuses kept in boxes, invisible, long texts, junk in the request box', { timeout: 180000 }, async (t) => {
  if (!PHP) {
    t.skip('php is not installed');
    return;
  }
  const { url } = await friendsServer();
  const a = await makeClient(url, 'Alice');
  // Bob's requests to the server are watched: what his request box answers
  const boxAnswers = [];
  const bobStore = new RecordingStore();
  const b = new FriendsClient({ timers: false, fetch: async (address, init) => {
    const reply = await checkedFetch(bobStore, early)(address, init);
    const form = new URLSearchParams(init.body);
    if (form.get('op') === 'inbox_get' && form.has('box')) boxAnswers.push(JSON.parse(await reply.clone().text()));
    if (form.get('op') === 'status_put' || form.get('op') === 'status_get') boxAnswers.push({ op: form.get('op'), form: Object.fromEntries(form) });
    return reply;
  } });
  b.store = bobStore;
  await b.setStorage(bobStore);
  await b.setServer(url);
  await b.setProfile('Bob');
  await a.enable();
  await b.enable();
  await settle(a, b);
  await befriend(a, b);
  const fa = a.fingerprint;
  const fb = b.fingerprint;

  // ---- statuses under boxes only friends can name, today's and yesterday's asked for
  await refresh(b);
  const put = boxAnswers.filter((x) => x.op === 'status_put').at(-1);
  assert.ok(put.form.box && put.form.box.length === 22 && put.form.note);
  const asked = boxAnswers.filter((x) => x.op === 'status_get').at(-1);
  assert.equal(asked.form.boxes.split(',').length, 2);
  assert.ok(!asked.form.fps, 'no fingerprints asked for');
  assert.equal(entry(b.friends, 'fp', fa).state, 'web');
  // Invisible: the note goes at once, Alice looks offline
  await a.setMyState('invisible');
  await settle(a);
  await refresh(b);
  assert.equal(entry(b.friends, 'fp', fa).state, 'offline');
  await a.setMyState('online');
  await settle(a);
  await refresh(b);
  assert.equal(entry(b.friends, 'fp', fa).state, 'web');

  // ---- read receipts: "Read" under one's own messages once the friend has read them
  const a2 = await makeClient(url, 'Alice');
  assert.equal(await a2.restoreBackup(await a.saveBackup('correct horse battery'), 'correct horse battery'), true);
  await settle(a, a2, b);
  await refresh(b, a, a2);
  await a.send(fb, 'Read me');
  await settle(a);
  await refresh(b, a2);
  assert.equal(a.messages(fb).at(-1).read, false);
  assert.equal(b.unread, 1);
  await b.markRead(fa);
  await settle(b);
  await refresh(a, a2);
  assert.equal(a.messages(fb).at(-1).read, true);
  assert.equal(a2.messages(fb).at(-1).read, true);
  // Read on one device: read on the other too
  await b.send(fa, 'Got it');
  await settle(b);
  await refresh(a, a2);
  assert.equal(entry(a2.chats, 'id', fb).unread, 1);
  await a.markRead(fb);
  await settle(a);
  await refresh(a2, b);
  assert.equal(entry(a2.chats, 'id', fb).unread, 0);
  assert.equal(b.messages(fa).at(-1).read, true);
  // Off: none sent and none shown, but the own devices still hear what was read
  await a.setReadReceipts(false);
  assert.equal(a.readReceipts, false);
  assert.ok(!('read' in a.messages(fb).at(-1)));
  await b.send(fa, 'And this?');
  await settle(b);
  await refresh(a, a2);
  await a.markRead(fb);
  await settle(a);
  await refresh(a2, b);
  assert.equal(entry(a2.chats, 'id', fb).unread, 0);
  assert.equal(b.messages(fa).at(-1).read, false);
  await a.setReadReceipts(true);

  // ---- a text is at most 3500 bytes of UTF-8, cut between characters
  await a.send(fb, 'é'.repeat(3000));
  await settle(a);
  await refresh(b);
  const long = b.messages(fa).at(-1).text;
  assert.equal(long, 'é'.repeat(1750));
  assert.equal(a.messages(fb).at(-1).state, 'sent');

  // ---- a request box full of notes nobody can open is emptied (their senders have no card)
  const bobDevice = b.devices.find((d) => d.me);
  assert.ok(bobDevice && bobDevice.kind === 2);
  const box = b64(await fc.requestBox(parseFriendLink(b.link).lk));
  const stranger = await fc.generateKeyPair({ usage: 'ecdh' });
  const junk = fc.encodeEnvelope({ kind: 1, fromFp: crypto.getRandomValues(new Uint8Array(16)), fromDevice: new Uint8Array(8),
    toDevice: h(bobDevice.id), ek: stranger.pub, spkId: 1, opkId: 0, header: { dh: stranger.pub, pn: 0, n: 0 }, sealed: new Uint8Array(40) });
  const putJunk = await (await fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams({ v: '1', op: 'request_put', box, note: b64(junk) }).toString() })).json();
  assert.equal(putJunk.ok, true);
  boxAnswers.length = 0;
  await refresh(b);
  await refresh(b);
  const seen = boxAnswers.filter((x) => Array.isArray(x.notes));
  assert.equal(seen[0].notes.length, 1);
  assert.equal(seen.at(-1).notes.length, 0);
  assert.equal(b.requests.length, 0);

  // ---- the own devices, from the card; one taken off
  await refresh(a);
  await settle(a, a2);
  const devices = a.devices;
  assert.equal(devices.length, 2);
  assert.equal(devices.filter((d) => d.me).length, 1);
  assert.ok(devices.every((d) => d.kind === 2 && /^[0-9a-f]{16}$/.test(d.id)));
  const other = devices.find((d) => !d.me);
  await a.removeDevice(other.id);
  await settle(a);
  assert.deepEqual(a.devices.map((d) => d.id), devices.filter((d) => d.me).map((d) => d.id));
  // Bob's next message goes to that one device only
  await b.send(fa, 'Only one of you now');
  await settle(b);
  await refresh(a, a2);
  assert.ok(texts(a, fb, false).includes('Only one of you now'));
  assert.ok(!texts(a2, fb, false).includes('Only one of you now'));

  assert.equal((await serverCard(url, fa)).devices.length, 1);

  // ---- off: at once for the player, then nothing left (the last device stays on the card: a card has one at least)
  const off = a.disable();
  assert.ok(!a.enabled && a.friends.length === 0 && a.chats.length === 0 && a.devices.length === 0 && a.fingerprint === '');
  await off;
  assert.equal(await a.store.load(), null);
  a2.close();
  b.close();
  assert.deepEqual(early, []);
});

test('linking another browser: the link, the same six digits on both, the friends over', { timeout: 120000 }, async (t) => {
  if (!PHP) {
    t.skip('php is not installed');
    return;
  }
  const { url } = await friendsServer();
  const a = await makeClient(url, 'Alice');
  const b = await makeClient(url, 'Bob');
  await a.enable();
  await b.enable();
  await settle(a, b);
  await befriend(a, b);
  await a.send(b.fingerprint, 'Before the new device');
  await settle(a);

  // A link that is none fails at once
  const none = await makeClient(url, 'Nobody');
  await none.joinLink('not a link');
  assert.equal(none.linkState, 'failed');
  assert.ok(!none.enabled);
  none.close();

  const n = await makeClient(url, 'Alice');
  await a.startLinking();
  assert.equal(a.linkState, 'waiting');
  assert.ok(a.linkText.startsWith('psrpt://link/') && a.linkText.includes('?at='));
  await n.joinLink(a.linkText);
  await until('the six digits on both', () => a.linkState === 'code' && n.linkState === 'code', 30000);
  assert.match(a.linkCode, /^\d{3} \d{3}$/);
  assert.equal(n.linkCode, a.linkCode);
  // Only the device that has the friends sends them
  await n.confirmLinking();
  assert.equal(n.linkState, 'code');
  await a.confirmLinking();
  await until('linked', () => n.linkState === 'done', 30000);
  assert.equal(a.linkState, 'done');
  assert.ok(n.enabled);
  assert.equal(n.fingerprint, a.fingerprint);
  assert.equal(entry(n.friends, 'fp', b.fingerprint).name, 'Bob');
  assert.ok(texts(n, b.fingerprint, true).includes('Before the new device'));
  await settle(a, n);
  // Bob hears of the new device (its card changed) before he writes
  await refresh(b, n, a);
  assert.equal(n.devices.length, 2);
  assert.equal(n.devices.filter((d) => d.me).length, 1);
  // Bob writes once: the new device has it too
  await b.send(a.fingerprint, 'Hello new device');
  await settle(b);
  await refresh(a, n);
  assert.ok(texts(n, b.fingerprint, false).includes('Hello new device'));
  assert.ok(texts(a, b.fingerprint, false).includes('Hello new device'));
  a.cancelLinking();
  n.cancelLinking();
  assert.equal(a.linkState, '');
  // Off on the new device: it leaves the card first, so that friends stop writing to it
  assert.equal((await serverCard(url, a.fingerprint)).devices.length, 2);
  await n.disable();
  assert.deepEqual((await serverCard(url, a.fingerprint)).devices.map((d) => hex(d.id)), a.devices.filter((d) => d.me).map((d) => d.id));
  a.close();
  b.close();
  n.close();
});

test('what a reply carries, as received: only well-formed, the quote on one line and short', () => {
  assert.deepEqual(cleanReply({ reply: 'abc', quote: { from: 'Zm9v', text: 'Hello\n  there\t!' } }),
    { reply: 'abc', quote: { from: 'Zm9v', text: 'Hello there !' } });
  // The reply only as a string of at most 64 characters
  assert.deepEqual(cleanReply({ reply: 42 }), {});
  assert.deepEqual(cleanReply({ reply: '' }), {});
  assert.deepEqual(cleanReply({ reply: 'x'.repeat(65) }), {});
  assert.deepEqual(cleanReply({ reply: 'x'.repeat(64) }), { reply: 'x'.repeat(64) });
  // The quote only with both fields
  assert.deepEqual(cleanReply({ quote: { text: 'no writer' } }), {});
  assert.deepEqual(cleanReply({ quote: { from: 'Zm9v' } }), {});
  assert.deepEqual(cleanReply({ quote: { from: 7, text: 'x' } }), {});
  assert.deepEqual(cleanReply({ quote: { from: 'y'.repeat(65), text: 'x' } }), {});
  assert.deepEqual(cleanReply({ quote: 'a string' }), {});
  assert.deepEqual(cleanReply(null), {});
  // Cut to 160 bytes of UTF-8, never half a character
  const q = cleanReply({ quote: { from: 'Zm9v', text: '🔥'.repeat(60) } }).quote.text;
  assert.equal(q, '🔥'.repeat(40));
  // Nothing else of the quote comes along
  assert.deepEqual(Object.keys(cleanReply({ quote: { from: 'Zm9v', text: 'x', html: '<b>' } }).quote).sort(), ['from', 'text']);
});

test('replies, deleting, mentions and muting: two browsers and a second device', { timeout: 180000 }, async (t) => {
  if (!PHP) {
    t.skip('php is not installed');
    return;
  }
  const { url } = await friendsServer();
  let a = await makeClient(url, 'Alice');
  const b = await makeClient(url, 'Bob');
  await a.enable();
  await b.enable();
  await settle(a, b);
  await befriend(a, b);
  const fa = a.fingerprint;
  const fb = b.fingerprint;
  const a2 = await makeClient(url, 'Alice');
  assert.equal(await a2.restoreBackup(await a.saveBackup('correct horse battery'), 'correct horse battery'), true);
  await settle(a, a2);
  await refresh(b, a, a2);
  const events = [];
  const listen = (c) => c.addEventListener('message', (e) => events.push(e.detail));

  // A reply carries the message it answers and a copy of its start
  await b.send(fa, 'Hello Alice, racing at 8?');
  await settle(b);
  await refresh(a, a2);
  const hello = entry(a.messages(fb), 'text', 'Hello Alice, racing at 8?').id;
  await a.send(fb, 'Yes!', hello);
  // An id this chat does not have: no reply at all
  await a.send(fb, 'Nothing to answer', 'no such message');
  await settle(a);
  await refresh(b, a2);
  const yes = entry(b.messages(fa), 'text', 'Yes!');
  assert.equal(yes.reply, hello);
  assert.deepEqual(yes.quote, { from: fb, text: 'Hello Alice, racing at 8?' });
  assert.deepEqual(yes.quoted, { id: hello, from: fb, name: 'Bob', text: 'Hello Alice, racing at 8?', deleted: false, found: true });
  assert.ok(!('reply' in entry(b.messages(fa), 'text', 'Nothing to answer')));
  // The own other device has it too (through sync), with the quote
  const yes2 = entry(a2.messages(fb), 'text', 'Yes!');
  assert.equal(yes2.reply, hello);
  assert.equal(yes2.quoted.name, 'Bob');

  // Deleted by its writer: a stand-in everywhere, the reply now quotes "Deleted message"
  assert.equal(await a.deleteMessage(fb, hello), false, 'not one\'s own');
  assert.equal(await b.deleteMessage(fa, hello), true);
  await settle(b);
  await refresh(a, a2);
  for (const [x, chat] of [[a, fb], [a2, fb], [b, fa]]) {
    const gone = entry(x.messages(chat), 'id', hello);
    assert.equal(gone.deleted, true);
    assert.ok(!('text' in gone) && !('quote' in gone) && !('reactions' in gone));
    assert.equal(gone.from, fb);
    const answer = entry(x.messages(chat), 'text', 'Yes!');
    assert.equal(answer.quoted.text, 'Deleted message');
    assert.equal(answer.quoted.deleted, true);
  }
  assert.equal(await b.deleteMessage(fa, hello), false, 'once');
  assert.equal(entry(a.chats, 'id', fb).last, 'Nothing to answer');

  // Deleted on the own other device: through sync here too
  const nothing = entry(a.messages(fb), 'text', 'Nothing to answer').id;
  assert.equal(await a2.deleteMessage(fb, nothing), true);
  await settle(a2);
  await refresh(a, b);
  assert.equal(entry(a.messages(fb), 'id', nothing).deleted, true);
  assert.equal(entry(b.messages(fa), 'id', nothing).deleted, true);
  // No reactions on a stand-in
  await b.react(fa, nothing, '🔥');
  assert.ok(!('reactions' in entry(b.messages(fa), 'id', nothing)));

  // A message deleted before it was read is nothing to read
  await refresh(a);
  await a.markRead(fb);
  await settle(a);
  await b.send(fa, 'oops, wrong chat');
  await settle(b);
  const oops = entry(b.messages(fa), 'text', 'oops, wrong chat').id;
  await b.deleteMessage(fa, oops);
  await settle(b);
  await refresh(a);
  assert.equal(entry(a.chats, 'id', fb).unread, 0);
  assert.equal(entry(a.chats, 'id', fb).last, 'Message deleted');

  // Muted: no notification and nothing in the total, unless a message names Alice
  listen(a);
  await a.setMute(fb, Date.now() + 3600000);
  assert.ok(a.mutedUntil(fb) > Date.now());
  assert.equal(a2.mutedUntil(fb), 0, 'each device its own');
  await b.send(fa, 'quiet one');
  await settle(b);
  await refresh(a);
  assert.equal(events.length, 0);
  const muted = entry(a.chats, 'id', fb);
  assert.equal(muted.muted, true);
  assert.equal(muted.unread, 1);
  assert.equal(a.unread, 0);
  await b.send(fa, 'hey @alice, look');
  await settle(b);
  await refresh(a);
  assert.equal(events.length, 1);
  assert.equal(events[0].mention, true);
  assert.equal(entry(a.chats, 'id', fb).mentions, 1);
  assert.equal(a.unread, 1);
  assert.equal(entry(a.messages(fb), 'text', 'hey @alice, look').mention, true);
  assert.equal(entry(a.messages(fb), 'text', 'quiet one').mention, false);
  // Back on: all of it counts, and tells
  await a.setMute(fb, 0);
  assert.equal(a.mutedUntil(fb), 0);
  assert.equal(a.unread, 2);
  await b.send(fa, 'loud again');
  await settle(b);
  await refresh(a);
  assert.equal(events.length, 2);
  assert.equal(events[1].mention, false);
  await a.setMute(fb, MUTE_FOREVER);
  assert.equal(a.mutedUntil(fb), MUTE_FOREVER);
  await a.setMute(fb, 0);

  // A quote of a message the chat no longer has: the copy, with the friend's name; without a
  // copy, "A message from earlier". And a forged delete of somebody else's message is ignored.
  a.close();
  const state = await a.store.load();
  const msgs = state.chats[fb].m;
  const answerAt = msgs.findIndex((m) => m.text === 'Yes!');
  msgs.splice(0, msgs.length, ...msgs.filter((m) => m.id !== hello));
  msgs.push({ id: 'from-earlier', at: Date.now(), text: 'An old answer', from: fa, mine: true, state: 'sent', reply: 'gone-long-ago' });
  // Bob's "quiet one" made to look like Alice's own here, as a forger would
  const quiet = msgs.find((m) => m.text === 'quiet one');
  quiet.from = fa;
  quiet.mine = true;
  await a.store.save(state);
  assert.ok(answerAt >= 0);
  a = await makeClient(url, 'Alice', a.store);
  await settle(a);
  const copy = entry(a.messages(fb), 'text', 'Yes!');
  assert.deepEqual(copy.quoted, { id: hello, from: fb, name: 'Bob', text: 'Hello Alice, racing at 8?', deleted: false, found: false });
  assert.deepEqual(entry(a.messages(fb), 'id', 'from-earlier').quoted,
    { id: 'gone-long-ago', from: '', name: '', text: 'A message from earlier', deleted: false, found: false });
  assert.equal(await a.deleteMessage(fb, quiet.id), true);
  await settle(a);
  await refresh(b);
  assert.equal(entry(b.messages(fa), 'id', quiet.id).text, 'quiet one', 'only the writer deletes');
  assert.ok(!entry(b.messages(fa), 'id', quiet.id).deleted);
  a.close();
  a2.close();
  b.close();
});

test('linking stopped on the other device: the new device does not wait for ever', { timeout: 120000 }, async (t) => {
  if (!PHP) {
    t.skip('php is not installed');
    return;
  }
  const { url } = await friendsServer();
  const a = await makeClient(url, 'Alice');
  const c = await makeClient(url, 'Carol');
  await a.enable();
  await c.enable();
  await settle(a, c);
  // Stopped after the six digits came up
  const n = await makeClient(url, 'Alice');
  await a.startLinking();
  await n.joinLink(a.linkText);
  await until('the six digits', () => a.linkState === 'code' && n.linkState === 'code', 30000);
  a.cancelLinking();
  // Stopped before anybody came: the box is gone when the new device asks
  const late = await makeClient(url, 'Carol');
  await c.startLinking();
  const text = c.linkText;
  c.cancelLinking();
  await settle(c);
  await late.joinLink(text);
  await until('the stopped link failed', () => n.linkState === 'failed', 15000);
  assert.match(n.problem, /run out/);
  assert.ok(!n.enabled);
  assert.equal(late.linkState, 'waiting');
  await until('the gone link failed', () => late.linkState === 'failed', 30000);
  assert.match(late.problem, /run out/);
  assert.ok(!late.enabled);
  a.close();
  c.close();
  n.close();
  late.close();
});

test('a group: Alice (two devices), Bob, and Carol, who is no friend of Bob', { timeout: 300000 }, async (t) => {
  if (!PHP) {
    t.skip('php is not installed');
    return;
  }
  const { url } = await friendsServer();
  const a = await makeClient(url, 'Alice');
  const b = await makeClient(url, 'Bob');
  const c = await makeClient(url, 'Carol');
  for (const x of [a, b, c]) await x.enable();
  await settle(a, b, c);
  await befriend(a, b);
  await befriend(c, a);
  const fa = a.fingerprint;
  const fb = b.fingerprint;
  const fcarol = c.fingerprint;
  const a2 = await makeClient(url, 'Alice');
  assert.equal(await a2.restoreBackup(await a.saveBackup('correct horse battery'), 'correct horse battery'), true);
  await settle(a, a2);
  await refresh(b, c, a, a2);

  const gid = await a.createGroup('  Friday   racing ', [fb, fcarol, 'not a friend']);
  assert.ok(gid && a.isGroup(gid));
  assert.equal(base64UrlDecode(gid)[0] & 0x80, 0x80);
  assert.equal(entry(a.chats, 'id', gid).kind, 'group');
  assert.equal(entry(a.chats, 'id', gid).last, 'No messages yet');
  await settle(a);
  // The group to everybody, then everybody's sender keys through its box
  for (let round = 0; round < 3; round++) await refresh(b, c, a2, a);
  assert.ok(b.isGroup(gid) && c.isGroup(gid) && a2.isGroup(gid));
  const seenByCarol = c.group(gid);
  assert.equal(seenByCarol.name, 'Friday racing');
  assert.equal(seenByCarol.members.length, 3);
  assert.ok(!seenByCarol.admin && a.group(gid).admin && a2.group(gid).admin);
  assert.equal(entry(seenByCarol.members, 'fp', fb).name, 'Bob');
  assert.equal(entry(seenByCarol.members, 'fp', fb).friend, false);
  assert.equal(entry(seenByCarol.members, 'fp', fcarol).me, true);
  assert.equal(entry(c.chats, 'id', gid).kind, 'group');
  assert.equal(entry(c.chats, 'id', gid).members, 3);
  // A note in the chat: who added them
  assert.ok(c.messages(gid).some((m) => m.system && m.text === 'Alice added you to the group'));

  await b.send(gid, 'Hi all');
  await settle(b);
  await refresh(a, a2, c);
  assert.ok(texts(a, gid, false).includes('Hi all'));
  assert.ok(texts(a2, gid, false).includes('Hi all'));
  assert.ok(texts(c, gid, false).includes('Hi all'));
  // Carol knows Bob only from the group: his name comes with it
  assert.equal(entry(c.messages(gid), 'text', 'Hi all').name, 'Bob');
  assert.equal(entry(c.chats, 'id', gid).last, 'Bob: Hi all');
  await c.send(gid, 'Hey, Carol here');
  await a2.send(gid, 'Alice on the laptop');
  await settle(c, a2);
  await refresh(b, a);
  assert.ok(texts(b, gid, false).includes('Hey, Carol here'));
  assert.ok(texts(b, gid, false).includes('Alice on the laptop'));
  assert.ok(texts(a, gid, true).includes('Alice on the laptop'));
  // Bob's "Hi all", read by Alice and Carol
  await a.markRead(gid);
  await c.markRead(gid);
  await settle(a, c);
  await refresh(b, a2);
  assert.equal(entry(b.messages(gid), 'text', 'Hi all').readBy, 2);
  assert.equal(entry(a2.chats, 'id', gid).unread, 0);
  // Who read how far, for their pictures: Alice and Carol (whom Bob knows only from the group)
  const readersOfBob = b.readers(gid);
  assert.deepEqual(readersOfBob.map((r) => r.fp), [fa, fcarol].sort());
  assert.equal(entry(readersOfBob, 'fp', fcarol).name, 'Carol');
  assert.equal(entry(readersOfBob, 'fp', fcarol).avatar, '');
  assert.equal(entry(readersOfBob, 'fp', fa).name, 'Alice');
  assert.ok(entry(readersOfBob, 'fp', fa).at >= entry(b.messages(gid), 'text', 'Hi all').at);
  assert.ok(!c.readers(gid).some((r) => r.fp === fcarol), 'never oneself');
  // Read receipts off: none shown
  await b.setReadReceipts(false);
  assert.ok(!('readBy' in entry(b.messages(gid), 'text', 'Hi all')));
  assert.deepEqual(b.readers(gid), []);
  await b.setReadReceipts(true);

  // Reactions in the group, also from Carol, who is no friend of Bob: everybody has them, Alice's other device too
  const hiAll = entry(c.messages(gid), 'text', 'Hi all').id;
  await c.react(gid, hiAll, '🔥');
  await a.react(gid, hiAll, '🔥');
  await a.react(gid, hiAll, '👍');
  await settle(a, c);
  await refresh(b, a2, c, a);
  for (const x of [a, a2, b, c]) {
    const r = entry(x.messages(gid), 'id', hiAll).reactions;
    assert.deepEqual([...r['🔥']].sort(), [fa, fcarol].sort());
    assert.deepEqual(r['👍'], [fa]);
  }
  // Taken back on Alice's other device: gone everywhere
  await a2.react(gid, hiAll, '👍');
  await settle(a2);
  await refresh(a, b, c);
  for (const x of [a, a2, b, c]) {
    const r = entry(x.messages(gid), 'id', hiAll).reactions;
    assert.ok(!('👍' in r) && r['🔥'].length === 2);
  }
  // Not one emoji, or no such message: nothing
  await b.react(gid, hiAll, 'ok');
  await b.react(gid, 'no such message', '👍');
  await settle(b);
  await refresh(a);
  assert.deepEqual(Object.keys(entry(a.messages(gid), 'id', hiAll).reactions), ['🔥']);

  // A reply in the group: Carol answers Bob, whom she knows only from the group
  const carolsEvents = [];
  c.addEventListener('message', (e) => carolsEvents.push(e.detail));
  await c.send(gid, 'Count me in', hiAll);
  await settle(c);
  await refresh(a, a2, b);
  for (const x of [a, a2, b]) {
    const answer = entry(x.messages(gid), 'text', 'Count me in');
    assert.equal(answer.reply, hiAll);
    assert.deepEqual(answer.quote, { from: fb, text: 'Hi all' });
    assert.equal(answer.quoted.name, 'Bob');
    assert.equal(answer.quoted.found, true);
  }
  // A mention of Carol: marked for her, and her notification says so
  await b.send(gid, '@Carol bring the wheel');
  await settle(b);
  await refresh(c, a);
  assert.equal(entry(c.messages(gid), 'text', '@Carol bring the wheel').mention, true);
  assert.equal(entry(a.messages(gid), 'text', '@Carol bring the wheel').mention, false);
  assert.equal(carolsEvents.at(-1).mention, true);
  assert.equal(entry(c.chats, 'id', gid).mentions, 1);
  // Carol mutes the group: Bob's next message tells her nothing, a mention still does
  await c.markRead(gid);
  await c.setMute(gid, MUTE_FOREVER);
  const before = carolsEvents.length;
  await b.send(gid, 'just chatting');
  await settle(b);
  await refresh(c);
  assert.equal(carolsEvents.length, before);
  assert.equal(c.unread, 0);
  assert.equal(entry(c.chats, 'id', gid).unread, 1);
  await a.send(gid, 'right, @carol?');
  await settle(a);
  await refresh(c);
  assert.equal(carolsEvents.length, before + 1);
  assert.equal(c.unread, 1);
  await c.setMute(gid, 0);
  // Deleting in a group: only one's own; gone for everybody, the reply quoting it says so
  const countMeIn = entry(b.messages(gid), 'text', 'Count me in').id;
  assert.equal(await b.deleteMessage(gid, countMeIn), false);
  assert.equal(await c.deleteMessage(gid, countMeIn), true);
  await b.send(gid, 'Great', countMeIn);
  await settle(b, c);
  await refresh(a, a2, b, c);
  for (const x of [a, a2, b, c]) {
    assert.equal(entry(x.messages(gid), 'id', countMeIn).deleted, true);
    // Written while Bob still had it: the quote of it now says it is gone
    const great = entry(x.messages(gid), 'text', 'Great');
    assert.equal(great.quoted.text, 'Deleted message');
    assert.equal(great.quoted.deleted, true);
  }

  // A plan, and answers to it
  const t1 = Date.now() + 3600000;
  await b.sendPlan(gid, 'Racing', [t1, t1 + 3600000]);
  await settle(b);
  await refresh(a, c);
  const plan = a.messages(gid).find((m) => m.plan);
  assert.ok(plan && plan.plan.title === 'Racing' && plan.plan.times.length === 2);
  assert.equal(plan.text, 'When: Racing?');
  await a.vote(gid, plan.id, [2, 1]);
  await c.vote(gid, plan.id, [0, 2]);
  await settle(a, c);
  await refresh(b, a2);
  const votes = b.messages(gid).find((m) => m.id === plan.id).votes;
  assert.deepEqual(votes[fa], [2, 1]);
  assert.deepEqual(votes[fcarol], [0, 2]);
  assert.deepEqual(a2.messages(gid).find((m) => m.id === plan.id).votes[fa], [2, 1]);
  // And in a chat between two friends, synced to the own other device
  await a.sendPlan(fb, '', [t1]);
  await settle(a);
  await refresh(b, a2);
  const plan1 = b.messages(fa).find((m) => m.plan);
  assert.equal(plan1.text, 'When shall we play?');
  await b.vote(fa, plan1.id, [1]);
  await settle(b);
  await refresh(a, a2);
  assert.deepEqual(a.messages(fb).find((m) => m.id === plan1.id).votes[fb], [1]);
  // Reactions between two friends: to a card too, and Alice's own synced to her other device
  await b.react(fa, plan1.id, '😮');
  await a.react(fb, plan1.id, '❤️');
  await settle(a, b);
  await refresh(a, a2, b);
  for (const [x, chat] of [[a, fb], [a2, fb], [b, fa]]) {
    const r = x.messages(chat).find((m) => m.id === plan1.id).reactions;
    assert.deepEqual(r, { '😮': [fb], '❤️': [fa] });
  }
  // At most three different ones of one player: the fourth is refused, on both sides
  for (const emoji of ['😂', '🔥', '👍']) await b.react(fa, plan1.id, emoji);
  await settle(b);
  await refresh(a);
  const ofBob = (x, chat) => Object.entries(x.messages(chat).find((m) => m.id === plan1.id).reactions)
    .filter(([, who]) => who.includes(fb)).map(([emoji]) => emoji).sort();
  assert.deepEqual(ofBob(b, fa), ['😂', '😮', '🔥'].sort());
  assert.deepEqual(ofBob(a, fb), ['😂', '😮', '🔥'].sort());
  // The friend's picture for "read": with the friend's name
  await b.send(fa, 'See you then');
  await settle(b);
  await refresh(a);
  await a.markRead(fb);
  await settle(a);
  await refresh(b);
  assert.deepEqual(b.readers(fa).map((r) => [r.fp, r.name]), [[fa, 'Alice']]);
  assert.deepEqual(a2.messages(fb).find((m) => m.id === plan1.id).votes[fb], [1]);

  // A report: only what the player chose, opened, for the server's operator
  assert.equal(await c.report(gid, [entry(c.messages(gid), 'text', 'Hi all').id], 'spam'), true);

  // Carol taken out and banned: she reads nothing new, and only an admin could add her again
  await a.removeFromGroup(gid, fcarol, true);
  await settle(a);
  for (let round = 0; round < 2; round++) await refresh(c, b, a2, a);
  assert.equal(c.group(gid).left, true);
  assert.ok(c.messages(gid).some((m) => m.system && m.text === 'Alice took you out of the group'));
  assert.equal(b.group(gid).members.length, 2);
  assert.deepEqual(b.group(gid).banned.map((x) => x.fp), [fcarol]);
  await b.send(gid, 'Only us now');
  await settle(b);
  await refresh(c, a);
  assert.ok(texts(a, gid, false).includes('Only us now'));
  assert.ok(!texts(c, gid, false).includes('Only us now'));
  // Admins only
  await b.removeFromGroup(gid, fa, false);
  await b.renameGroup(gid, 'Mine now');
  await settle(b);
  assert.equal(b.group(gid).name, 'Friday racing');

  // Bob made an admin, then no longer one; a new name and everybody may add people
  await a.setGroupAdmin(gid, fb, true);
  await settle(a);
  await refresh(b, a2);
  assert.ok(b.group(gid).admin);
  await a.setGroupAdmin(gid, fb, false);
  await a.renameGroup(gid, 'Friday racing club');
  await a.setGroupAdd(gid, 'all');
  await settle(a);
  for (let round = 0; round < 2; round++) await refresh(b, a2);
  assert.ok(!b.group(gid).admin);
  assert.equal(b.group(gid).name, 'Friday racing club');
  assert.equal(b.group(gid).add, 'all');

  // Bob leaves: the admin takes him out
  await b.leaveGroup(gid);
  await settle(b);
  assert.ok(b.group(gid).left);
  assert.equal(b.messages(gid).at(-1).text, 'You left the group');
  for (let round = 0; round < 3; round++) await refresh(a, a2, b);
  assert.equal(a.group(gid).members.length, 1);
  assert.equal(a2.group(gid).members.length, 1);

  for (const x of [a, a2, b, c]) x.close();
  assert.deepEqual(early, []);
});

// ------------------------------------------------------------------ the page and the app

/** The app's client as a peer: friends-client-test --peer. */
function startPeer(url, name, seconds) {
  const folder = fs.mkdtempSync(path.join(os.tmpdir(), 'psrpt-friends-peer-'));
  const proc = spawn(PEER, [url, '--peer', folder, name, String(seconds)], { stdio: ['pipe', 'pipe', 'ignore'] });
  const lines = [];
  readline.createInterface({ input: proc.stdout }).on('line', (line) => lines.push(line));
  const stop = () => {
    if (proc.exitCode === null && proc.signalCode === null) proc.kill('SIGKILL');
    fs.rmSync(folder, { recursive: true, force: true });
  };
  cleanup.push(stop);
  return {
    lines,
    /** The first line the test accepts, once it came. */
    async line(accept, ms = 30000) {
      const end = Date.now() + ms;
      for (;;) {
        const hit = lines.find(accept);
        if (hit !== undefined) return hit;
        if (Date.now() > end || proc.exitCode !== null) throw new Error(`the app's client did not say it; it said:\n${lines.join('\n')}`);
        await sleep(50);
      }
    },
    async quit() {
      if (proc.exitCode === null) {
        proc.stdin.write('quit\n');
        let timer;
        await new Promise((resolve) => {
          proc.once('exit', resolve);
          timer = setTimeout(resolve, 10000);
        });
        clearTimeout(timer);
      }
      proc.stdin.end();
      stop();
    },
  };
}

test('the page and the app: a request the app takes, statuses, and chats both ways', { timeout: 180000 }, async (t) => {
  if (!PHP) {
    t.skip('php is not installed');
    return;
  }
  if (!HAVE_PEER) {
    t.skip('no build/gui/friends-client-test: cmake --build build --target friends-client-test');
    return;
  }
  const { url, www } = await friendsServer();
  const peer = startPeer(url, 'Mara', 150);
  const link = (await peer.line((l) => l.startsWith('LINK '))).slice(5);
  const peerFp = (await peer.line((l) => l.startsWith('FP '))).slice(3);
  assert.ok(link.startsWith('psrpt://friend/'));

  const c = await makeClient(url, 'Bea in a browser');
  await c.enable();
  await settle(c);
  const looked = await c.lookUp(link);
  assert.ok(looked.ok && looked.name === 'Mara');

  // ---- the request goes to the app, which says yes
  await c.sendRequest(link, 'Hi from the browser');
  await settle(c);
  const out = entry(c.requests, 'fp', peerFp);
  assert.ok(out.fp && !out.unsent && out.name === 'Mara');
  await peer.line((l) => l === `ACCEPTED ${c.fingerprint}`);
  await until('the app as a friend, with its status', async () => {
    await refresh(c);
    const f = entry(c.friends, 'fp', peerFp);
    return f.name === 'Mara' && f.activity === 'Playing Peer Game on PS5';
  });
  // The app is online as the app, not in a browser
  assert.equal(entry(c.friends, 'fp', peerFp).state, 'online');
  assert.equal(c.requests.length, 0);

  // ---- a message there, its echo back
  await c.send(peerFp, 'Hello app');
  await settle(c);
  assert.equal(c.messages(peerFp).at(-1).state, 'sent');
  await peer.line((l) => l === `GOT ${c.fingerprint} Hello app`);
  await until('the echo', async () => {
    await refresh(c);
    return texts(c, peerFp, false).includes('echo: Hello app');
  });

  // ---- several in a row: the app gets them in order, and answers each
  c.send(peerFp, 'one');
  c.send(peerFp, 'two');
  c.send(peerFp, 'three');
  await settle(c);
  await peer.line((l) => l === `GOT ${c.fingerprint} three`);
  const got = peer.lines.filter((l) => l.startsWith(`GOT ${c.fingerprint} `)).map((l) => l.slice(`GOT ${c.fingerprint} `.length));
  assert.deepEqual(got, ['Hello app', 'one', 'two', 'three']);
  await until('the three echoes', async () => {
    await refresh(c);
    return texts(c, peerFp, false).length === 4;
  });
  assert.deepEqual(texts(c, peerFp, false), ['echo: Hello app', 'echo: one', 'echo: two', 'echo: three']);
  assert.equal(c.unread, 4);

  // ---- a link: the app makes the preview of its echo, the page shows it (and never opens the link)
  fs.writeFileSync(path.join(www, 'track.jpg'), Buffer.from(TINY_JPEG, 'base64'));
  fs.writeFileSync(path.join(www, 'track.html'), '<html><head><meta property="og:title" content="Friday&#39;s track">' +
    '<meta property="og:site_name" content="Racing club"><meta property="og:image" content="track.jpg"></head></html>');
  const page = new URL('track.html', url).href;
  await c.send(peerFp, `Track: ${page}`);
  await settle(c);
  await until('the echo with its preview', async () => {
    await refresh(c);
    return texts(c, peerFp, false).includes(`echo: Track: ${page}`);
  });
  const echo = c.messages(peerFp).find((m) => m.text === `echo: Track: ${page}`);
  assert.equal(echo.embed.url, page);
  assert.equal(echo.embed.title, 'Friday\'s track');
  assert.equal(echo.embed.site, 'Racing club');
  assert.ok(echo.embed.image.startsWith('data:image/jpeg;base64,/9j/'));
  // The page's own message has none: a browser cannot look at other sites
  assert.ok(!c.messages(peerFp).find((m) => m.text === `Track: ${page}`).embed);
  // The app's echo is a reply to what it answers
  assert.equal(echo.quoted.found, true);
  assert.equal(echo.quoted.text, `Track: ${page}`);

  // ---- replies and deleting, between the page and the app
  await c.send(peerFp, 'Answering your echo', echo.id);
  await settle(c);
  await peer.line((l) => l === `QUOTE ${c.fingerprint} echo: Track: ${page}`);
  const mineOne = c.messages(peerFp).find((m) => m.text === 'one' && m.mine);
  assert.equal(await c.deleteMessage(peerFp, mineOne.id), true);
  await settle(c);
  await peer.line((l) => l === `GONE ${mineOne.id}`);
  await c.send(peerFp, 'delete your echo');
  await settle(c);
  const deletedLine = await peer.line((l) => l.startsWith('DELETED '));
  const echoGone = deletedLine.slice('DELETED '.length);
  await until('the app\'s echo deleted here too', async () => {
    await refresh(c);
    const m = c.messages(peerFp).find((x) => x.id === echoGone);
    return !!m && m.deleted === true;
  });

  await peer.quit();
  assert.deepEqual(early, []);
});

test('the page and the app in a group: the app checks the page\'s versions, sender keys both ways, a member who is no friend of the app',
  { timeout: 240000 }, async (t) => {
    if (!PHP) {
      t.skip('php is not installed');
      return;
    }
    if (!HAVE_PEER) {
      t.skip('no build/gui/friends-client-test: cmake --build build --target friends-client-test');
      return;
    }
    const { url } = await friendsServer();
    const peer = startPeer(url, 'Noa', 200);
    const link = (await peer.line((l) => l.startsWith('LINK '))).slice(5);
    const peerFp = (await peer.line((l) => l.startsWith('FP '))).slice(3);
    const a = await makeClient(url, 'Bea');
    const c = await makeClient(url, 'Cid');
    await a.enable();
    await c.enable();
    await settle(a, c);
    await a.sendRequest(link, '');
    await settle(a);
    await peer.line((l) => l === `ACCEPTED ${a.fingerprint}`);
    await until('the app as a friend', async () => {
      await refresh(a);
      return entry(a.friends, 'fp', peerFp).name === 'Noa';
    });
    await befriend(a, c);

    // The app takes the group from its friend (a version it checks), and says it was added: that
    // note, echoed into the group, opens in both browsers with the app's sender key
    const gid = await a.createGroup('Page and app', [peerFp, c.fingerprint]);
    await settle(a);
    await peer.line((l) => l === `GOT ${gid} Bea added you to the group`, 60000);
    await until('the app\'s echo in both browsers', async () => {
      await refresh(a, c);
      return texts(a, gid, false).includes('echo: Bea added you to the group')
        && texts(c, gid, false).includes('echo: Bea added you to the group');
    }, 60000);
    // Cid is no friend of the app's player: the name comes with the group
    assert.equal(entry(c.messages(gid), 'text', 'echo: Bea added you to the group').name, 'Noa');

    // The browsers' sender keys reach the app through the group's box: the app reads both
    await c.send(gid, 'Hello from Cid');
    await settle(c);
    await peer.line((l) => l === `GOT ${gid} Hello from Cid`, 60000);
    await a.send(gid, 'Hello from Bea');
    await settle(a);
    await peer.line((l) => l === `GOT ${gid} Hello from Bea`, 60000);
    await until('both echoes in both browsers', async () => {
      await refresh(a, c);
      return ['echo: Hello from Cid', 'echo: Hello from Bea'].every((x) => texts(a, gid, false).includes(x) && texts(c, gid, false).includes(x));
    }, 60000);

    await peer.quit();
    a.close();
    c.close();
    assert.deepEqual(early, []);
  });
