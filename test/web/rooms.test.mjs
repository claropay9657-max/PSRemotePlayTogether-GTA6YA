// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The public rooms as the page lists them (web/js/rooms.js): the app's four orders,
// full rooms last whatever the order, and before that the rooms a browser can join.
// Run: node --test test/web

import { test } from 'node:test';
import assert from 'node:assert/strict';

import { filterRooms } from '../../web/js/rooms.js';

function room(title, players, max, since, extra = {}) {
  return { title, game: '', host: 'H', lang: 'en', console: 'ps5', players, max, since, full: players >= max, web: true,
    compatible: true, policy: 'open', twitch: [], ...extra };
}

const rooms = [
  room('a', 2, 4, 600),
  room('b', 3, 4, 60),
  room('c', 1, 6, 3000),
  room('full', 4, 4, 10),
  room('app only', 5, 8, 100, { web: false }),
];

test('the four orders of the app, full rooms last', () => {
  const titles = (sort) => filterRooms(rooms, { sort }).map((r) => r.title);
  assert.deepEqual(titles(0), ['b', 'a', 'c', 'app only', 'full']);
  assert.deepEqual(titles(1), ['c', 'a', 'b', 'app only', 'full']);
  assert.deepEqual(titles(2), ['b', 'a', 'c', 'app only', 'full']);
  assert.deepEqual(titles(3), ['c', 'a', 'b', 'app only', 'full']);
  // an order that does not exist is the first one
  assert.deepEqual(titles(9), titles(0));
});

test('the filters still apply', () => {
  assert.deepEqual(filterRooms(rooms, { hideFull: true }).map((r) => r.title), ['b', 'a', 'c', 'app only']);
  assert.deepEqual(filterRooms(rooms, { search: 'app' }).map((r) => r.title), ['app only']);
});
