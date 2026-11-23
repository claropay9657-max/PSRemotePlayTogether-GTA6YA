// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Sessions joined before (web/js/recent.js), kept as the app keeps them
// (gui/src/coplayrecent.cpp): newest first, one per invite key, at most eight, and
// whether the host shares now from one quiet look at the meeting point.
// Run: node --test test/web

import { test } from 'node:test';
import assert from 'node:assert/strict';

import { base64UrlEncode } from '../../web/js/bytes.js';
import { parseInvite } from '../../web/js/meet.js';
import {
  CLOSED, MAX_RECENT, OPEN, UNKNOWN, addJoined, consoleOf, isHostNote, joinedText, lookForHost, readRecent,
  removeRecent, writeRecent,
} from '../../web/js/recent.js';

function key(n) {
  const bytes = new Uint8Array(16).fill(n);
  return base64UrlEncode(bytes);
}

test('a whole link, the part behind "#", the key alone and an invite of the app all read', () => {
  const k = key(7);
  for (const text of [`https://crypto90.github.io/PSRemotePlayTogether/#${k}`, `#${k}`, k, `psrpt://203.0.113.7:9410/${k}`,
    `  https://example.org/play/#${k}?meet=meet.example.org/m.php  `]) {
    const invite = parseInvite(text);
    assert.ok(invite, text);
    assert.equal(base64UrlEncode(invite.secret), k);
  }
  assert.equal(parseInvite(`https://example.org/play/#${k}?meet=meet.example.org/m.php`).meetUrl, 'https://meet.example.org/m.php');
  assert.equal(parseInvite('https://example.org/'), null);
  assert.equal(parseInvite('https://example.org/#not-a-key'), null);
});

test('newest first, one per key, never more than eight', () => {
  let list = [];
  list = addJoined(list, `#${key(1)}`, 'Ana', 'ps5', null, 1000);
  list = addJoined(list, `psrpt://-/${key(2)}`, 'Ben', 'pc', { title: 'Late night', game: 'Racing', cover: 'https://image.api.playstation.com/x.png' }, 2000);
  assert.deepEqual(list.map((e) => e.host), ['Ben', 'Ana']);
  assert.equal(list[0].title, 'Late night');
  // The same session again, by its link this time: on top, still with what the room said
  list = addJoined(list, `https://x.example/#${key(2)}`, 'Ben', 'pc', null, 3000);
  assert.equal(list.length, 2);
  assert.equal(list[0].key, key(2));
  assert.equal(list[0].title, 'Late night');
  assert.equal(list[0].game, 'Racing');
  assert.equal(list[0].joined, 3000);
  assert.equal(list[0].invite, `https://x.example/#${key(2)}`);
  for (let i = 10; i < 20; i++) list = addJoined(list, `#${key(i)}`, `P${i}`, 'ps4', null, i);
  assert.equal(list.length, MAX_RECENT);
  assert.equal(list[0].host, 'P19');
  // What is no invite changes nothing
  assert.equal(addJoined(list, 'hello', 'X', 'ps5', null), list);
  list = removeRecent(list, key(19));
  assert.equal(list.length, MAX_RECENT - 1);
  assert.equal(list[0].host, 'P18');
});

test('stored and read back; what does not read is left out', () => {
  let list = addJoined([], `#${key(3)}`, 'Cleo', 'xbox', null, 5);
  const stored = writeRecent(list);
  assert.ok(!stored.includes('"key"'));
  assert.deepEqual(readRecent(stored), list);
  const odd = JSON.stringify([
    { invite: 'nonsense', host: 'A' },
    { invite: `#${key(4)}`, host: 'B'.repeat(80), console: 'gameboy', cover: 'https://evil.example/x.png', joined: 'soon' },
    { invite: `#${key(4)}`, host: 'twice' },
    null, 5,
  ]);
  const read = readRecent(odd);
  assert.equal(read.length, 1);
  assert.equal(read[0].host.length, 32);
  assert.equal(read[0].console, '');
  assert.equal(read[0].cover, '');
  assert.equal(read[0].joined, 0);
  assert.deepEqual(readRecent('{not json'), []);
  assert.deepEqual(readRecent('{"a":1}'), []);
  assert.deepEqual(readRecent(''), []);
});

test('what the game runs on, from the host\'s welcome', () => {
  assert.equal(consoleOf({ ps5: true, source: 0 }), 'ps5');
  assert.equal(consoleOf({ ps5: false, source: 0 }), 'ps4');
  assert.equal(consoleOf({ ps5: true, source: 1 }), 'pc');
  assert.equal(consoleOf({ ps5: true, source: 2 }), 'xbox');
  assert.equal(consoleOf(null), '');
});

test('a host\'s own note is told by its header; the look leaves no note and a new id each time', async () => {
  const note = new Uint8Array(14 + 16 + 15);
  note[0] = 0x05;
  const hostNote = base64UrlEncode(note);
  note[0] = 0x06;
  const otherNote = base64UrlEncode(note);
  assert.ok(isHostNote(hostNote));
  assert.ok(!isHostNote(otherNote));
  assert.ok(!isHostNote(base64UrlEncode(new Uint8Array([5, 1, 2]))));
  assert.ok(!isHostNote('***'));

  const entry = { invite: `#${key(9)}?meet=meet.example.org/m.php` };
  const asked = [];
  let answer = { notes: [hostNote], pollMs: 3000 };
  const visit = async (url, room, id, text) => {
    asked.push({ url, room, id: Array.from(id).join(','), text });
    if (answer instanceof Error) throw answer;
    return answer;
  };
  assert.equal(await lookForHost(entry, false, undefined, visit), OPEN);
  assert.equal(asked[0].url, 'https://meet.example.org/m.php');
  assert.equal(asked[0].text, '');
  assert.equal(asked[0].room.length, 22);
  answer = { notes: [otherNote, 'abc'], pollMs: 3000 };
  assert.equal(await lookForHost(entry, false, undefined, visit), CLOSED);
  assert.notEqual(asked[1].id, asked[0].id);
  assert.equal(asked[1].room, asked[0].room);
  answer = { notes: [], pollMs: 0 };
  assert.equal(await lookForHost(entry, false, undefined, visit), CLOSED);
  answer = new Error('the meeting point answered with HTTP 503');
  assert.equal(await lookForHost(entry, false, undefined, visit), UNKNOWN);
  assert.equal(await lookForHost({ invite: 'nonsense' }, false, undefined, visit), UNKNOWN);
});

test('when it was joined, in the app\'s words', () => {
  const now = Date.UTC(2026, 10, 23, 12, 0, 0);
  assert.equal(joinedText(0, now), 'Joined before');
  assert.equal(joinedText(now - 20 * 1000, now), 'Joined just now');
  assert.equal(joinedText(now - 5 * 60000, now), 'Joined 5 min ago');
  assert.equal(joinedText(now - 3 * 3600000, now), 'Joined 3 h ago');
  assert.equal(joinedText(now - 26 * 3600000, now), 'Joined yesterday');
  assert.equal(joinedText(now - 5 * 86400000, now), 'Joined 5 days ago');
  assert.ok(joinedText(now - 40 * 86400000, now).startsWith('Joined on '));
});
