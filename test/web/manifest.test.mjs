// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The browser page as something that can be installed on a home screen: its manifest says
// what a browser needs, every picture it names is there in the size it says, the page
// points to it, and the page keeps nothing on the device (it has to be the newest every
// time it is opened: the host's app counts on that).
// Run: node --test test/web

import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const web = path.join(path.dirname(fileURLToPath(import.meta.url)), '..', '..', 'web');
const manifest = JSON.parse(fs.readFileSync(path.join(web, 'manifest.webmanifest'), 'utf8'));
const page = fs.readFileSync(path.join(web, 'index.html'), 'utf8');

/** Width and height of a PNG, from its first chunk. */
function pngSize(file) {
  const b = fs.readFileSync(file);
  assert.deepEqual([...b.subarray(0, 8)], [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a], `${file} is not a PNG`);
  assert.equal(b.toString('latin1', 12, 16), 'IHDR');
  return [b.readUInt32BE(16), b.readUInt32BE(20)];
}

test('the manifest says what a browser needs to install the page', () => {
  assert.equal(manifest.name, 'PSRemotePlayTogether');
  assert.ok(manifest.short_name.length > 0 && manifest.short_name.length <= 13, 'a short name fits under an icon');
  assert.ok(['fullscreen', 'standalone'].includes(manifest.display));
  assert.match(manifest.background_color, /^#[0-9a-f]{6}$/i);
  assert.match(manifest.theme_color, /^#[0-9a-f]{6}$/i);
});

test('it starts at the page itself, wherever the page is served from', () => {
  // The page is served from a folder (…/PSRemotePlayTogether/), and from others when somebody hosts it themselves
  assert.equal(manifest.start_url, './');
  assert.equal(manifest.scope, './');
  for (const icon of manifest.icons) assert.ok(!/^([a-z]+:|\/)/i.test(icon.src), `${icon.src} is not relative`);
});

test('every picture it names is there, in the size it says', () => {
  assert.ok(manifest.icons.length >= 3);
  for (const icon of manifest.icons) {
    const [w, h] = pngSize(path.join(web, icon.src));
    assert.equal(icon.sizes, `${w}x${h}`, icon.src);
    assert.equal(icon.type, 'image/png');
  }
  const has = (size, purpose) => manifest.icons.some((i) => i.sizes === size && i.purpose === purpose);
  assert.ok(has('192x192', 'any'), 'a small one');
  assert.ok(has('512x512', 'any'), 'a large one');
  assert.ok(has('512x512', 'maskable'), 'one a phone can cut its own shape from');
});

test('the page points to the manifest and has a picture for iOS', () => {
  assert.match(page, /<link rel="manifest" href="manifest\.webmanifest">/);
  const touch = page.match(/<link rel="apple-touch-icon" href="([^"]+)">/);
  assert.ok(touch, 'no apple-touch-icon');
  assert.deepEqual(pngSize(path.join(web, touch[1])), [180, 180]);
  assert.match(page, /<meta name="apple-mobile-web-app-capable" content="yes">/);
  // What the page's own rules allow: pictures and the manifest from the page itself
  assert.match(page, /default-src 'self'/);
});

/** A script without its comments, for what it does rather than what it says. */
function code(file) {
  return fs.readFileSync(file, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '').replace(/(^|[^:\\])\/\/.*$/gm, '$1');
}

test('nothing of the page is kept on the device', () => {
  // A copy kept by a service worker could be older than the host's app expects. The one service
  // worker there is takes friends' push messages only (push-sw.js, docs/friends.md "Web only"),
  // registered by js/friends/push.js once the player switches notifications on
  const registering = [];
  for (const file of fs.readdirSync(path.join(web, 'js'), { recursive: true }).filter((f) => f.endsWith('.js'))) {
    const text = code(path.join(web, 'js', file));
    const calls = [...text.matchAll(/serviceWorker\s*\.\s*register\s*\(\s*([^,)]*)/g)];
    if (calls.length) registering.push(file.split(path.sep).join('/'));
    for (const call of calls) assert.equal(call[1].trim(), "'push-sw.js'", `${file} registers ${call[1]}`);
  }
  assert.deepEqual(registering, ['friends/push.js']);
  // No other script beside the page that could be registered for all of it
  assert.deepEqual(fs.readdirSync(web).filter((f) => /\.m?js$/.test(f)), ['push-sw.js']);
  assert.equal(manifest.serviceworker, undefined);

  // And that one leaves loading the page alone: no fetch handler, no cache, nothing loaded into it
  const worker = code(path.join(web, 'push-sw.js'));
  const listened = [...worker.matchAll(/addEventListener\(\s*['"]([a-z]+)['"]/g)].map((m) => m[1]).sort();
  assert.deepEqual(listened, ['notificationclick', 'push']);
  assert.ok(!/\bself\s*\.\s*on[a-z]+\s*=/.test(worker), 'push-sw.js sets an on… handler');
  assert.ok(!/['"`]fetch['"`]|\bfetch\s*\(/.test(worker), 'push-sw.js answers or makes fetches');
  assert.ok(!/\bcaches\b|\bCache(Storage)?\b|\bindexedDB\b/.test(worker), 'push-sw.js keeps something');
  assert.ok(!/importScripts|\bimport\s*\(|^\s*import\s/m.test(worker), 'push-sw.js loads code');
  assert.ok(!/skipWaiting|clients\s*\.\s*claim/.test(worker), 'push-sw.js takes over pages');
});
