// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The device check of the browser page (web/js/devicecheck.js) against the app's
// (gui/src/devicecheck.cpp): the test chime has to be the app's chime, and the microphone's
// level has to be counted the way the app counts it.
//
// chime-vectors.json holds what the app's own code makes: every 1009th frame of
// devicecheck::Chime (and the last ones, where it fades out) as the two 16-bit samples the
// app plays, and the sum of all samples' sizes. It was written by compiling that function on
// its own; made again if the app's chime changes.
// Run: node --test test/web

import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { CHIME_STEPS_MS, MIC_GATE, chime, levelOf } from '../../web/js/devicecheck.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const app = JSON.parse(fs.readFileSync(path.join(here, 'chime-vectors.json'), 'utf8'));

/** A sample as the app hands it to the speakers. */
const played = (v) => Math.trunc(Math.fround(Math.max(-1, Math.min(1, v)) * 32767)) + 0;

test('the test chime is the app\'s chime, sample for sample', () => {
  const sound = chime();
  assert.equal(sound.rate, 48000);
  assert.equal(sound.left.length, app.frames);
  assert.equal(sound.right.length, app.frames);
  assert.deepEqual(CHIME_STEPS_MS, app.steps);
  assert.ok(app.at.length > 300);
  for (const [frame, left, right] of app.at) {
    assert.equal(played(sound.left[frame]), left, `left at frame ${frame}`);
    assert.equal(played(sound.right[frame]), right, `right at frame ${frame}`);
  }
  let sum = 0;
  for (let i = 0; i < app.frames; i++) sum += Math.abs(played(sound.left[i])) + Math.abs(played(sound.right[i]));
  assert.equal(sum, app.sum);
});

test('the chime plays on the left, then on the right, then on both, and ends without a click', () => {
  const { left, right, rate } = chime();
  const loudest = (samples, fromMs, toMs) => {
    let peak = 0;
    for (let i = Math.floor((fromMs * rate) / 1000); i < Math.floor((toMs * rate) / 1000); i++) peak = Math.max(peak, Math.abs(samples[i]));
    return peak;
  };
  // Left alone first
  assert.ok(loudest(left, 0, 850) > 0.2);
  assert.equal(loudest(right, 0, 850), 0);
  // Then the right, louder than what still rings on the left
  assert.ok(loudest(right, 850, 1700) > 0.2);
  assert.ok(loudest(right, 1000, 1700) > loudest(left, 1000, 1700) * 2);
  // Then both
  assert.ok(loudest(left, 1700, 2200) > 0.05 && loudest(right, 1700, 2200) > 0.05);
  // Never harsh, and silent at the very end
  assert.ok(loudest(left, 0, 3200) <= 0.35 + 1e-6 && loudest(right, 0, 3200) <= 0.35 + 1e-6);
  assert.equal(played(left[left.length - 1]), 0);
  assert.equal(played(right[right.length - 1]), 0);
  assert.ok(Math.abs(left[left.length - 48]) < 0.01 && Math.abs(right[right.length - 48]) < 0.01);
});

test('the microphone\'s level is counted as the app counts it', () => {
  // -60 dBFS to 0 dBFS onto 0 to 100; the app works on 16-bit samples, the page on -1 to 1
  assert.equal(levelOf(0), 0);
  assert.equal(levelOf(0.5 / 32768), 0);
  assert.equal(levelOf(1), 100);
  assert.equal(levelOf(2), 100);
  assert.equal(levelOf(0.001), 0);
  assert.equal(levelOf(0.01), 33);
  assert.equal(levelOf(0.1), 66);
  // The app's own gate, an RMS of 160 on 16 bits, is a little under 23 there (where the app draws its mark)
  assert.equal(levelOf(160 / 32768), 22);
  // The mark is where this page's voice chat starts to send
  assert.equal(MIC_GATE, 26);
  assert.ok(levelOf(0.006) >= MIC_GATE - 1);
});
