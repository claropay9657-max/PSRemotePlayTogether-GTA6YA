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

test('nothing of the page is kept on the device', () => {
  // A copy kept by a service worker could be older than the host's app expects
  for (const file of fs.readdirSync(path.join(web, 'js'))) {
    const text = fs.readFileSync(path.join(web, 'js', file), 'utf8');
    assert.ok(!/serviceWorker\s*\.\s*register/.test(text), `${file} registers a service worker`);
  }
  assert.equal(manifest.serviceworker, undefined);
});
