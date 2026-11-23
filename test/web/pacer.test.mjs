// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Which decoded frame the browser page shows at each refresh of the screen
// (FramePacer in web/js/video.js), without a browser: frames that are decoded at
// given times are played on a screen that refreshes at given times, the way the
// page's player does it, and what the viewer gets to see is counted.
// Run: node --test test/web

import { test } from 'node:test';
import assert from 'node:assert/strict';

import { FramePacer } from '../../web/js/video.js';

/**
 * @param {FramePacer} pacer
 * @param {number[]} decodedAt when each frame is decoded, in ms, rising
 * @param {number} refresh the screen's refresh interval in ms
 * @returns {{shown: number[], skipped: number[], repeats: number, delays: number[], longestLine: number}}
 *   shown: the frames in the order they were shown; repeats: refreshes between the first and
 *   the last frame at which the screen had nothing new; delays: for each shown frame, how long
 *   after being decoded it was shown
 */
function play(pacer, decodedAt, refresh) {
  const out = { shown: [], skipped: [], repeats: 0, delays: [], longestLine: 0 };
  let next = 0;
  const end = decodedAt[decodedAt.length - 1] + refresh * 4;
  for (let tick = refresh; tick <= end; tick += refresh) {
    while (next < decodedAt.length && decodedAt[next] <= tick) {
      out.skipped.push(...pacer.offer(next));
      out.longestLine = Math.max(out.longestLine, pacer.length);
      next++;
    }
    const frame = pacer.take();
    if (frame === null) {
      if (out.shown.length && next < decodedAt.length) out.repeats++;
      continue;
    }
    out.shown.push(frame);
    out.delays.push(tick - decodedAt[frame]);
  }
  return out;
}

/** The same numbers every run. */
function noise(seed) {
  let s = seed >>> 0;
  return () => {
    s = (Math.imul(s, 1664525) + 1013904223) >>> 0;
    return s / 4294967296;
  };
}

const HZ60 = 1000 / 60;

/** Frames of a 60 fps stream that arrive about when the screen refreshes, a few ms early or late. */
function nearTheRefresh(count, spread, seed) {
  const rnd = noise(seed);
  const at = [];
  for (let i = 0; i < count; i++) at.push((i + 1) * HZ60 + (rnd() * 2 - 1) * spread);
  return at;
}

test('with nothing held, the newest frame wins and the others are skipped', () => {
  const p = new FramePacer(0);
  assert.deepEqual(p.offer('a'), []);
  assert.deepEqual(p.offer('b'), ['a']);
  assert.deepEqual(p.offer('c'), ['b']);
  assert.equal(p.length, 1);
  assert.equal(p.take(), 'c');
  assert.equal(p.take(), null);
});

test('one frame may wait; a third one pushes the oldest out', () => {
  const p = new FramePacer();
  assert.equal(p.hold, 1);
  assert.deepEqual(p.offer('a'), []);
  assert.deepEqual(p.offer('b'), []);
  assert.deepEqual(p.offer('c'), ['a']);
  assert.equal(p.take(), 'b');
  assert.equal(p.take(), 'c');
  assert.equal(p.take(), null);
  p.offer('d');
  p.offer('e');
  assert.deepEqual(p.clear(), ['d', 'e']);
  assert.equal(p.length, 0);
});

test('frames that arrive a little early or late are all shown, evenly, instead of being skipped', () => {
  // The case that stutters without holding: every frame comes within 3 ms of a refresh
  const decodedAt = nearTheRefresh(600, 3, 7);
  const asBefore = play(new FramePacer(0), decodedAt, HZ60);
  const held = play(new FramePacer(1), decodedAt, HZ60);

  // Shown as they come: many frames are never seen, and as often the screen has nothing new
  assert.ok(asBefore.skipped.length > 100, `only ${asBefore.skipped.length} skipped`);
  assert.ok(asBefore.repeats > 100, `only ${asBefore.repeats} repeats`);

  // One frame held: every frame is seen, in order, one at each refresh
  assert.equal(held.skipped.length, 0);
  assert.equal(held.shown.length, decodedAt.length);
  assert.deepEqual(held.shown, decodedAt.map((_, i) => i));
  assert.ok(held.repeats <= 2, `${held.repeats} repeats`);
  // And none waits longer than one refresh more than it had to
  assert.ok(Math.max(...held.delays) <= 2 * HZ60 + 0.001, `longest wait ${Math.max(...held.delays)} ms`);
});

test('an even stream is not held back at all', () => {
  // Every frame arrives in the middle between two refreshes
  const decodedAt = [];
  for (let i = 0; i < 300; i++) decodedAt.push((i + 1.5) * HZ60);
  const held = play(new FramePacer(1), decodedAt, HZ60);
  assert.equal(held.skipped.length, 0);
  assert.equal(held.repeats, 0);
  assert.equal(held.longestLine, 1);
  assert.ok(Math.max(...held.delays) < HZ60, `longest wait ${Math.max(...held.delays)} ms`);
});

test('after a gap and a burst the picture catches up at once', () => {
  const decodedAt = [];
  for (let i = 0; i < 30; i++) decodedAt.push((i + 1.5) * HZ60);
  // Nothing for 150 ms, then eight frames within 5 ms, then as before
  const burst = decodedAt[decodedAt.length - 1] + 150;
  for (let i = 0; i < 8; i++) decodedAt.push(burst + i * 0.6);
  const after = Math.ceil((burst + 5) / HZ60);
  for (let i = 0; i < 60; i++) decodedAt.push((after + i + 0.5) * HZ60);

  const held = play(new FramePacer(1), decodedAt, HZ60);
  // Of the eight, the newest two are shown; the line never grows beyond two
  assert.deepEqual(held.skipped, [30, 31, 32, 33, 34, 35]);
  assert.ok(held.longestLine <= 2);
  // Whatever came after the burst is shown within two refreshes
  const afterBurst = held.shown.map((f, i) => [f, held.delays[i]]).filter(([f]) => f >= 38);
  assert.ok(afterBurst.length >= 60);
  for (const [frame, delay] of afterBurst) assert.ok(delay <= 2 * HZ60 + 0.001, `frame ${frame} waited ${delay} ms`);
});

test('on a screen slower than the stream the picture does not fall behind', () => {
  // 60 frames a second on a screen that refreshes 50 times a second
  const decodedAt = [];
  for (let i = 0; i < 600; i++) decodedAt.push((i + 1) * HZ60 + 3);
  const refresh = 1000 / 50;
  const held = play(new FramePacer(1), decodedAt, refresh);
  // About one frame in six cannot be shown
  assert.ok(held.skipped.length >= 90 && held.skipped.length <= 110, `${held.skipped.length} skipped`);
  assert.ok(held.longestLine <= 2);
  assert.ok(Math.max(...held.delays) <= 2 * refresh + 0.001, `longest wait ${Math.max(...held.delays)} ms`);
  // What is shown is in order
  for (let i = 1; i < held.shown.length; i++) assert.ok(held.shown[i] > held.shown[i - 1]);
});
