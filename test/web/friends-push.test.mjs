// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Notifications from friends while the page is closed (docs/friends.md, "Web only"):
// - the page's push-only service worker (web/push-sw.js), run here with a stand-in for what a
//   browser gives it: what it shows for a push, and where a click on it leads;
// - the client's side of it (pushKey, setPush in web/js/friends/client.js) against a real
//   friends server (php -S, in its test mode) that pushes to a stand-in push service on this
//   machine: an empty push for a request and for a message, none once switched off.
// Without php, the second part is skipped.
// Run: node --experimental-default-type=module --test test/web/*.test.mjs

import { after, test } from 'node:test';
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import http from 'node:http';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

import { base64UrlDecode, base64UrlEncode } from '../../web/js/bytes.js';
import { FriendsClient } from '../../web/js/friends/client.js';
import { MemoryStore } from '../../web/js/friends/store.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '..', '..');
const PHP = spawnSync('php', ['-v'], { stdio: 'ignore' }).status === 0;
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const cleanup = [];

after(() => {
  for (const f of cleanup.reverse()) {
    try {
      f();
    } catch (e) {
      // gone already
    }
  }
});

// ------------------------------------------------------------------ the service worker

const WORKER = fs.readFileSync(path.join(root, 'web', 'push-sw.js'), 'utf8');
const SCOPE = 'https://example.org/PSRemotePlayTogether/';
const CHROME = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0 Safari/537.36';
const FIREFOX = 'Mozilla/5.0 (X11; Linux x86_64; rv:133.0) Gecko/20100101 Firefox/133.0';
// A page on an iPhone's home screen says no "Safari"
const IPHONE_HOME = 'Mozilla/5.0 (iPhone; CPU iPhone OS 17_2 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Mobile/15E148';

/** The worker in a context of its own, with what a browser would give it. */
function loadWorker(ua, windows) {
  const listeners = {};
  const shown = [];
  const opened = [];
  const self = {
    navigator: { userAgent: ua },
    registration: {
      scope: SCOPE,
      showNotification: async (title, options) => {
        shown.push({ title, options });
      },
    },
    clients: {
      matchAll: async (query) => {
        // (made in the worker's own context: compared as a plain copy)
        assert.deepEqual({ ...query }, { type: 'window', includeUncontrolled: true });
        return windows;
      },
      openWindow: async (url) => {
        opened.push(url);
      },
    },
    addEventListener: (type, listener) => {
      listeners[type] = listener;
    },
  };
  vm.runInNewContext(WORKER, { self }, { filename: 'push-sw.js' });
  return { listeners, shown, opened };
}

/** Dispatches an event to a listener and waits for what it hands to waitUntil(). */
async function dispatch(listener, event) {
  let work = null;
  // The event's own fields stay where they are, unread unless the worker reads them
  const e = Object.create(event);
  e.waitUntil = (p) => {
    work = p;
  };
  listener(e);
  assert.ok(work, 'the worker keeps itself alive for the work');
  await work;
}

function windowAt(url, focused, visibilityState = 'visible') {
  const w = { url, focused, visibilityState, focusedNow: false, posted: [] };
  w.focus = async () => {
    w.focusedNow = true;
    return w;
  };
  w.postMessage = (m) => w.posted.push({ ...m });
  return w;
}

test('the service worker: a push shows that something is new, and nothing of it', async () => {
  const sw = loadWorker(CHROME, []);
  assert.deepEqual(Object.keys(sw.listeners).sort(), ['notificationclick', 'push']);
  let read = false;
  // The push is empty; the worker does not even look
  await dispatch(sw.listeners.push, { get data() { read = true; return null; } });
  assert.equal(read, false);
  assert.equal(sw.shown.length, 1);
  assert.equal(sw.shown[0].title, 'PSRemotePlayTogether');
  assert.equal(sw.shown[0].options.body, 'Something new from your friends');
  // One at a time: a second push replaces the first
  assert.equal(sw.shown[0].options.tag, 'psrpt-friends');
  assert.equal(sw.shown[0].options.icon, 'img/icon-192.png');
  assert.ok(fs.existsSync(path.join(root, 'web', 'img', 'icon-192.png')));
  assert.equal(sw.opened.length, 0);
});

test('the service worker: the page in front fetches itself, except where WebKit wants a notification each time', async () => {
  for (const ua of [CHROME, FIREFOX]) {
    const front = loadWorker(ua, [windowAt(SCOPE, true)]);
    await dispatch(front.listeners.push, {});
    assert.equal(front.shown.length, 0, ua);
    // Open but behind other windows, or hidden: a notification
    const behind = loadWorker(ua, [windowAt(SCOPE, false), windowAt(SCOPE, true, 'hidden')]);
    await dispatch(behind.listeners.push, {});
    assert.equal(behind.shown.length, 1, ua);
  }
  const iphone = loadWorker(IPHONE_HOME, [windowAt(SCOPE, true)]);
  await dispatch(iphone.listeners.push, {});
  assert.equal(iphone.shown.length, 1);
});

test('the service worker: a click on the notification shows the friends', async () => {
  // The page is open: in front, and told to open the friends panel
  const other = windowAt('https://example.org/elsewhere/', false);
  const page = windowAt(`${SCOPE}?x=1`, false);
  const open = loadWorker(CHROME, [other, page]);
  let closed = false;
  await dispatch(open.listeners.notificationclick, { notification: { close: () => { closed = true; } } });
  assert.equal(closed, true);
  assert.equal(page.focusedNow, true);
  assert.deepEqual(page.posted, [{ type: 'psrpt-friends' }]);
  assert.equal(other.focusedNow, false);
  assert.deepEqual(open.opened, []);
  // It is not: the page, at the friends
  const none = loadWorker(CHROME, [other]);
  await dispatch(none.listeners.notificationclick, { notification: { close: () => {} } });
  assert.deepEqual(none.opened, [`${SCOPE}#friends`]);
});

// ------------------------------------------------------------------ the client and the server

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

/** What plays a browser's push service: writes down every request. */
async function pushService() {
  const got = [];
  const server = http.createServer((req, res) => {
    const chunks = [];
    req.on('data', (c) => chunks.push(c));
    req.on('end', () => {
      got.push({ method: req.method, url: req.url, headers: req.headers, body: Buffer.concat(chunks) });
      res.writeHead(201);
      res.end();
    });
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  cleanup.push(() => server.close());
  return { got, base: `http://127.0.0.1:${server.address().port}` };
}

/** php -S with the server script in its test mode, pushing only to this machine. */
async function friendsServer() {
  const work = fs.mkdtempSync(path.join(os.tmpdir(), 'psrpt-friends-push-'));
  fs.mkdirSync(path.join(work, 'www'));
  fs.mkdirSync(path.join(work, 'data'));
  fs.copyFileSync(path.join(root, 'server', 'friends', 'psrpt_friends.php'), path.join(work, 'www', 'psrpt_friends.php'));
  const log = fs.openSync(path.join(work, 'php.log'), 'w');
  const port = await freePort();
  const proc = spawn('php', ['-S', `127.0.0.1:${port}`, '-t', path.join(work, 'www')], {
    env: { ...process.env, PSRPT_FRIENDS_DIR: path.join(work, 'data'), PSRPT_FRIENDS_TEST: '1', PSRPT_FRIENDS_PUSH_HOSTS: '127.0.0.1' },
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
      if ((await fetch(url)).ok) return url;
    } catch (e) {
      // not listening yet
    }
    await sleep(25);
  }
  throw new Error('the friends server did not start');
}

async function makeClient(url, name) {
  const c = new FriendsClient({ timers: false });
  await c.setStorage(new MemoryStore());
  await c.setServer(url);
  await c.setProfile(name);
  await c.enable();
  await c.idle();
  return c;
}

/** A browser's subscription keys: a P-256 point and 16 random bytes, as base64url. */
async function subscriptionKeys() {
  const pair = await crypto.subtle.generateKey({ name: 'ECDH', namedCurve: 'P-256' }, true, ['deriveBits']);
  const raw = new Uint8Array(await crypto.subtle.exportKey('raw', pair.publicKey));
  return { p256dh: base64UrlEncode(raw), auth: base64UrlEncode(crypto.getRandomValues(new Uint8Array(16))) };
}

async function until(what, check, ms = 15000) {
  const end = Date.now() + ms;
  while (!(await check())) {
    if (Date.now() > end) throw new Error(`waited in vain for ${what}`);
    await sleep(100);
  }
}

test('the client: a browser\'s subscription, the empty pushes it brings, and switching it off', { timeout: 120000 }, async (t) => {
  if (!PHP) {
    t.skip('php is not installed');
    return;
  }
  const url = await friendsServer();
  const service = await pushService();
  const bea = await makeClient(url, 'Bea');
  const cid = await makeClient(url, 'Cid');

  // The server's own key, the same every time
  const key = await bea.pushKey();
  assert.ok(key instanceof Uint8Array && key.length === 65 && key[0] === 4);
  assert.deepEqual(await bea.pushKey(), key);

  const keys = await subscriptionKeys();
  const endpoint = `${service.base}/push/bea`;
  // Not a push service the server knows, or keys that are none: refused
  assert.equal(await bea.setPush({ endpoint: 'https://example.org/push', ...keys }), false);
  assert.equal(await bea.setPush({ endpoint, p256dh: keys.p256dh.slice(4), auth: keys.auth }), false);
  assert.equal(await bea.setPush({ endpoint, ...keys }), true);

  // A friend request into Bea's request box: an empty push
  await cid.sendRequest(bea.link, 'hello');
  await cid.idle();
  await until('a push for the request', () => service.got.length >= 1);
  const first = service.got[0];
  assert.equal(first.method, 'POST');
  assert.equal(first.url, '/push/bea');
  assert.equal(first.body.length, 0);
  assert.equal(first.headers.ttl, '86400');
  const auth = /^vapid t=([A-Za-z0-9_-]+)\.([A-Za-z0-9_-]+)\.([A-Za-z0-9_-]+), k=([A-Za-z0-9_-]+)$/.exec(first.headers.authorization || '');
  assert.ok(auth, first.headers.authorization);
  assert.deepEqual(base64UrlDecode(auth[4]), key);
  const claims = JSON.parse(Buffer.from(base64UrlDecode(auth[2])).toString('utf8'));
  assert.equal(claims.aud, service.base);

  // A message into Bea's inbox: another one
  await bea.refresh();
  await bea.idle();
  await bea.accept(cid.fingerprint);
  await bea.idle();
  await cid.refresh();
  await cid.idle();
  assert.equal(cid.friends.length, 1);
  const before = service.got.length;
  await cid.send(bea.fingerprint, 'are you there?');
  await cid.idle();
  await until('a push for the message', () => service.got.length > before);
  assert.ok(service.got.every((g) => g.url === '/push/bea' && g.body.length === 0));

  // Switched off: the server forgets the address, nothing comes any more
  assert.equal(await bea.setPush(null), true);
  const settled = service.got.length;
  await cid.send(bea.fingerprint, 'hello again');
  await cid.idle();
  await sleep(1500);
  assert.equal(service.got.length, settled);
  await bea.refresh();
  await bea.idle();
  assert.ok(bea.messages(cid.fingerprint).some((m) => m.text === 'hello again'));

  // Friends switched off: nothing to sign with, nothing sent
  await bea.disable();
  assert.equal(await bea.setPush({ endpoint, ...keys }), false);
  bea.close();
  cid.close();
});
