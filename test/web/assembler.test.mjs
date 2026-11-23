// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The browser page putting the picture's fragments back together (FrameAssembler in
// web/js/video.js): what it counts when the picture has to start over with a key frame,
// which is how the page tells a hiccup from a connection that cannot carry the picture.
// Run: node --test test/web

import { test } from 'node:test';
import assert from 'node:assert/strict';

import { FrameAssembler } from '../../web/js/video.js';
import { VIDEO_KEY } from '../../web/js/proto.js';

/** A frame small enough for one fragment. */
function fragment(frameSeq, key, captureMs) {
  const data = new Uint8Array(100).fill(frameSeq & 0xff);
  return { frameSeq, flags: key ? VIDEO_KEY : 0, framesLost: 0, totalSize: data.length, fragCount: 1, fragIndex: 0, captureMs, data };
}

function delivered(a) {
  const seqs = [];
  for (let f = a.popReady(); f; f = a.popReady()) seqs.push(f.seq);
  return seqs;
}

test('the first key frame is a start, not a new start', () => {
  const a = new FrameAssembler();
  a.onFragment(fragment(0, true, 0), 0);
  a.onFragment(fragment(1, false, 17), 17);
  assert.deepEqual(delivered(a), [0, 1]);
  assert.equal(a.stats.restarts, 0);
});

test('a frame that never comes makes the picture start over with the next key frame, and that is counted', () => {
  const a = new FrameAssembler();
  let now = 0;
  a.onFragment(fragment(0, true, now), now);
  a.onFragment(fragment(1, false, now += 17), now);
  assert.deepEqual(delivered(a), [0, 1]);

  // Frame 2 is lost for good; what follows it cannot be shown
  for (let seq = 3; seq <= 20; seq++) a.onFragment(fragment(seq, false, now += 17), now);
  assert.deepEqual(delivered(a), []);
  // Asked for again in vain, then given up on: a key frame is asked for
  let asked = false;
  for (let i = 0; i < 100 && !asked; i++) asked = a.poll(now += 10).needKeyframe;
  assert.ok(asked, 'no key frame was asked for');
  assert.equal(a.stats.restarts, 0);

  a.onFragment(fragment(21, true, now += 17), now);
  a.onFragment(fragment(22, false, now += 17), now);
  assert.deepEqual(delivered(a), [21, 22]);
  assert.equal(a.stats.restarts, 1);
});

test('a key frame that overtakes a gap is a new start too', () => {
  const a = new FrameAssembler();
  let now = 0;
  a.onFragment(fragment(0, true, now), now);
  assert.deepEqual(delivered(a), [0]);
  // The host held frames 1 to 4 back and goes on with a key frame
  a.onFragment(fragment(5, true, now += 400), now);
  a.onFragment(fragment(6, false, now += 17), now);
  assert.deepEqual(delivered(a), [5, 6]);
  assert.equal(a.stats.restarts, 1);
});
