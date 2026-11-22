// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The browser page's DualSense over WebHID (web/js/dualsense.js), without a controller:
// reports built byte by byte as the controller sends them (the layout of Linux'
// hid-playstation and SDL), and what goes back checked the same way.
// Run: node --test test/web

import { test } from 'node:test';
import assert from 'node:assert/strict';
import zlib from 'node:zlib';

import { DualSense } from '../../web/js/dualsense.js';

/** An input report after its number: USB 0x01 (63 bytes) or Bluetooth 0x31 (77, one more byte in front). */
function inputReport({ bluetooth = false, gyro = [0, 0, 0], accel = [0, 8192, 0], ticks = 0, touches = [], status = 0x05 } = {}) {
  const b = new Uint8Array(63);
  const v = new DataView(b.buffer);
  b.set([0x80, 0x80, 0x80, 0x80], 0);
  for (let i = 0; i < 3; i++) {
    v.setInt16(15 + i * 2, gyro[i], true);
    v.setInt16(21 + i * 2, accel[i], true);
  }
  v.setUint32(27, ticks, true);
  for (let i = 0; i < 2; i++) {
    const o = 32 + i * 4;
    const t = touches[i];
    if (!t) {
      b[o] = 0x80;
      continue;
    }
    b[o] = t.id & 0x7f;
    b[o + 1] = t.x & 0xff;
    b[o + 2] = ((t.x >> 8) & 0x0f) | ((t.y & 0x0f) << 4);
    b[o + 3] = t.y >> 4;
  }
  b[52] = status;
  if (!bluetooth) return b;
  const bt = new Uint8Array(77);
  bt[0] = 0x01;
  bt.set(b, 1);
  return bt;
}

/** A DualSense with a made-up device that keeps what is sent to it. */
function withDevice(bluetooth) {
  const ds = new DualSense();
  const sent = [];
  ds.device = { opened: true, sendReport: async (id, data) => sent.push({ id, data: new Uint8Array(data) }) };
  ds.bluetooth = bluetooth;
  return { ds, sent };
}

const settle = () => new Promise((resolve) => setTimeout(resolve, 0));

test('USB and Bluetooth input reports read the same', () => {
  for (const bluetooth of [false, true]) {
    const ds = new DualSense();
    ds.handleReport(bluetooth ? 0x31 : 0x01, inputReport({
      bluetooth,
      gyro: [164, -328, 0],
      accel: [0, 8192, -8192],
      touches: [{ id: 5, x: 1000, y: 500 }],
      status: 0x07,
    }));
    assert.equal(ds.bluetooth, bluetooth);
    assert.equal(ds.reports, 1);
    // 164 raw units are 10 degrees a second without the controller's calibration
    assert.ok(Math.abs(ds.motion.gyro[0] - (10 * Math.PI) / 180) < 1e-9);
    assert.ok(Math.abs(ds.motion.gyro[1] + (20 * Math.PI) / 180) < 1e-9);
    assert.deepEqual(ds.motion.accel, [0, 1, -1]);
    assert.equal(ds.touches[0].x, 1000);
    assert.equal(ds.touches[0].y, 500);
    assert.ok(ds.touches[0].id >= 0, 'the finger is down');
    assert.equal(ds.touches[1].id, -1, 'no second finger');
    assert.equal(ds.battery, 75);
  }
});

test('the short report a controller sends over Bluetooth first is left alone', () => {
  const ds = new DualSense();
  ds.handleReport(0x01, new Uint8Array(9));
  assert.equal(ds.reports, 0);
  assert.equal(ds.motion, null);
});

test('a finger keeps its number while it stays, a new one gets another', () => {
  const ds = new DualSense();
  ds.handleReport(0x01, inputReport({ touches: [{ id: 3, x: 10, y: 20 }] }));
  const first = ds.touches[0].id;
  ds.handleReport(0x01, inputReport({ touches: [{ id: 3, x: 40, y: 50 }] }));
  assert.equal(ds.touches[0].id, first);
  assert.equal(ds.touches[0].x, 40);
  ds.handleReport(0x01, inputReport({ touches: [{ id: 4, x: 40, y: 50 }] }));
  assert.notEqual(ds.touches[0].id, first);
  ds.handleReport(0x01, inputReport());
  assert.equal(ds.touches[0].id, -1, 'lifted');
});

test('the battery: tenths on the battery, the cable while charging or full, nothing on a battery problem', () => {
  const ds = new DualSense();
  const battery = (status) => {
    ds.handleReport(0x01, inputReport({ status }));
    return ds.battery;
  };
  assert.equal(battery(0x00), 5);
  assert.equal(battery(0x09), 95);
  assert.equal(battery(0x0a), 100);
  assert.equal(battery(0x13), 254, 'charging');
  assert.equal(battery(0x2a), 254, 'full on the cable');
  assert.equal(battery(0xa5), 255, 'voltage or temperature out of range');
  assert.equal(battery(0xb5), 255, 'temperature error');
  assert.equal(battery(0xf5), 255, 'charging error');
});

test('the controller\'s own calibration of its sensors is used', () => {
  const ds = new DualSense();
  // Feature report 0x05 with its number in front: biases, +/- ranges, speeds, accelerometer ranges
  const raw = new Uint8Array(41);
  const v = new DataView(raw.buffer);
  raw[0] = 0x05;
  const put = (i, value) => v.setInt16(1 + i * 2, value, true);
  [2, -3, 4].forEach((bias, i) => put(i, bias));
  // Each gyroscope axis: +8200 and -8200 for 2 x 540 degrees a second
  [3, 5, 7].forEach((i) => put(i, 8200));
  [4, 6, 8].forEach((i) => put(i, -8200));
  put(9, 540);
  put(10, 540);
  // Each accelerometer axis: +8192 and -8192 for 1 g either way
  [11, 13, 15].forEach((i) => put(i, 8192));
  [12, 14, 16].forEach((i) => put(i, -8192));
  ds.readCalibration(raw);
  assert.ok(ds.calibration, 'taken as sane');
  ds.handleReport(0x01, inputReport({ gyro: [2 + 164, -3, 4], accel: [0, 8192, 0] }));
  const degrees = (ds.motion.gyro[0] * 180) / Math.PI;
  assert.ok(Math.abs(degrees - (164 * 1080) / 16400) < 1e-6);
  assert.ok(Math.abs(ds.motion.gyro[1]) < 1e-12 && Math.abs(ds.motion.gyro[2]) < 1e-12, 'the biases are taken off');
  assert.ok(Math.abs(ds.motion.accel[1] - 1) < 1e-9);

  // A gyroscope range of only 200 raw units for 2 x 540 degrees a second is not a DualSense
  [3, 5, 7].forEach((i) => put(i, 100));
  [4, 6, 8].forEach((i) => put(i, -100));
  ds.readCalibration(raw);
  assert.equal(ds.calibration, null, 'nonsense is left out');
});

test('USB: triggers, light bar and player lights where the controller expects them', async () => {
  const { ds, sent } = withDevice(false);
  const left = new Uint8Array(10).map((_, i) => i + 1);
  const right = new Uint8Array(10).map((_, i) => 0x40 + i);
  ds.setTriggers(0x21, left, 0x26, right);
  await settle();
  ds.setLed(10, 20, 30);
  ds.setPlayerIndex(2);
  await settle();
  await settle();
  const last = sent[sent.length - 1];
  assert.equal(last.id, 0x02);
  assert.equal(last.data.length, 47);
  assert.equal(last.data[0], 0x04 | 0x08, 'both triggers');
  assert.equal(last.data[1], 0x04 | 0x10, 'light bar and player lights');
  assert.equal(last.data[10], 0x26);
  assert.deepEqual(Array.from(last.data.subarray(11, 21)), Array.from(right));
  assert.equal(last.data[21], 0x21);
  assert.deepEqual(Array.from(last.data.subarray(22, 32)), Array.from(left));
  assert.equal(last.data[43], 0x0a, 'player 2');
  assert.deepEqual(Array.from(last.data.subarray(44, 47)), [10, 20, 30]);
});

test('the light bar is taken from the controller\'s animation once it is up, then the colour comes again', async () => {
  const { ds, sent } = withDevice(false);
  ds.out.lightOut = true;
  ds.setLed(1, 2, 3);
  await settle();
  assert.ok(sent.every((r) => r.data[38] === 0), 'not right after connecting');
  for (let i = 0; i < 10; i++) ds.handleReport(0x01, inputReport());
  for (let i = 0; i < 5; i++) await settle();
  const setup = sent.findIndex((r) => r.data[38] === 0x02);
  assert.ok(setup >= 0, 'sent once the controller sends its reports');
  assert.equal(sent[setup].data[41], 0x02, 'light out');
  assert.equal(sent[setup].data[0] | sent[setup].data[1], 0, 'nothing else in it');
  assert.equal(sent.filter((r) => r.data[38] === 0x02).length, 1, 'only once');
  const after = sent.slice(setup + 1);
  assert.ok(after.some((r) => r.data[44] === 1 && r.data[45] === 2 && r.data[46] === 3), 'the colour again after it');
});

test('Bluetooth: counter, marker and the checksum the controller checks', async () => {
  const { ds, sent } = withDevice(true);
  for (let i = 0; i < 17; i++) {
    ds.setLed(i, 0, 0);
    await settle();
  }
  assert.equal(sent.length, 17);
  sent.forEach((r, i) => {
    assert.equal(r.id, 0x31);
    assert.equal(r.data.length, 77);
    assert.equal(r.data[0], (i % 16) << 4, 'the counter goes round at 16');
    assert.equal(r.data[1], 0x10);
    assert.equal(r.data[2 + 44], i, 'the common part after the two bytes');
    const crc = zlib.crc32(Buffer.concat([Buffer.from([0xa2, 0x31]), Buffer.from(r.data.subarray(0, 73))]));
    assert.equal(new DataView(r.data.buffer, r.data.byteOffset).getUint32(73, true), crc >>> 0);
  });
});
