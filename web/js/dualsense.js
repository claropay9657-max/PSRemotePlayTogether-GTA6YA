// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// What the Gamepad API leaves out of a DualSense: the touchpad, the motion
// sensors, the battery, the adaptive triggers, the light bar and the player
// lights. Browsers with WebHID (Chrome, Edge) can talk to the controller
// directly for those. Buttons and sticks still come from the Gamepad API.
//
// The layout of the reports is the one Linux' hid-playstation driver and SDL
// use.

import { TOUCHPAD_HEIGHT, TOUCHPAD_WIDTH } from './proto.js';

const SONY = 0x054c;
const DUALSENSE = 0x0ce6;
const DUALSENSE_EDGE = 0x0df2;

const REPORT_USB_IN = 0x01;
const REPORT_BT_IN = 0x31;
const REPORT_USB_OUT = 0x02;
const REPORT_BT_OUT = 0x31;
const REPORT_CALIBRATION = 0x05;

/** Raw units of the sensors when the controller's own calibration cannot be read. */
const GYRO_PER_DEG_S = 16.4;
const ACCEL_PER_G = 8192;

const FLAG0_RIGHT_TRIGGER = 0x04;
const FLAG0_LEFT_TRIGGER = 0x08;
const FLAG1_LIGHTBAR = 0x04;
const FLAG1_PLAYER_LEDS = 0x10;

/** The five player lights for player 1 to 5, the way the console shows them. */
const PLAYER_LEDS = [0x00, 0x04, 0x0a, 0x15, 0x1b, 0x1f];

let crcTable = null;
function crc32(bytes) {
  if (!crcTable) {
    crcTable = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
      crcTable[n] = c >>> 0;
    }
  }
  let crc = 0xffffffff;
  for (let i = 0; i < bytes.length; i++) crc = crcTable[(crc ^ bytes[i]) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

/**
 * Keeps track of how the controller is held, from its gyroscope and
 * accelerometer (Madgwick's filter, as in the app's lib/src/orientation.c).
 */
class Orientation {
  constructor() {
    // 90 degrees around x, which the filter expects
    this.w = Math.SQRT1_2;
    this.x = Math.SQRT1_2;
    this.y = 0;
    this.z = 0;
    this.samples = 0;
    this.lastTicks = 0;
  }

  /** @param {number} ticks the sensor's own clock: a 32 bit counter of thirds of a microsecond */
  update(g, a, ticks) {
    this.samples++;
    if (this.samples <= 1) {
      this.lastTicks = ticks;
      return;
    }
    const delta = (ticks - this.lastTicks) >>> 0; // the counter starts over now and then
    this.lastTicks = ticks;
    const dt = Math.min(0.1, delta / 3e6);
    const beta = this.samples < 30 ? 20 : 0.05;
    let q0 = this.w;
    let q1 = this.x;
    let q2 = this.y;
    let q3 = this.z;
    let [gx, gy, gz] = g;
    let [ax, ay, az] = a;

    let d0 = 0.5 * (-q1 * gx - q2 * gy - q3 * gz);
    let d1 = 0.5 * (q0 * gx + q2 * gz - q3 * gy);
    let d2 = 0.5 * (q0 * gy - q1 * gz + q3 * gx);
    let d3 = 0.5 * (q0 * gz + q1 * gy - q2 * gx);

    if (ax !== 0 || ay !== 0 || az !== 0) {
      let n = 1 / Math.sqrt(ax * ax + ay * ay + az * az);
      ax *= n;
      ay *= n;
      az *= n;
      const q0q0 = q0 * q0;
      const q1q1 = q1 * q1;
      const q2q2 = q2 * q2;
      const q3q3 = q3 * q3;
      let s0 = 4 * q0 * q2q2 + 2 * q2 * ax + 4 * q0 * q1q1 - 2 * q1 * ay;
      let s1 = 4 * q1 * q3q3 - 2 * q3 * ax + 4 * q0q0 * q1 - 2 * q0 * ay - 4 * q1 + 8 * q1 * q1q1 + 8 * q1 * q2q2 + 4 * q1 * az;
      let s2 = 4 * q0q0 * q2 + 2 * q0 * ax + 4 * q2 * q3q3 - 2 * q3 * ay - 4 * q2 + 8 * q2 * q1q1 + 8 * q2 * q2q2 + 4 * q2 * az;
      let s3 = 4 * q1q1 * q3 - 2 * q1 * ax + 4 * q2q2 * q3 - 2 * q2 * ay;
      n = s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3;
      if (n > 0.000001) {
        n = 1 / Math.sqrt(n);
        d0 -= beta * s0 * n;
        d1 -= beta * s1 * n;
        d2 -= beta * s2 * n;
        d3 -= beta * s3 * n;
      }
    }
    q0 += d0 * dt;
    q1 += d1 * dt;
    q2 += d2 * dt;
    q3 += d3 * dt;
    const n = 1 / Math.sqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    this.w = q0 * n;
    this.x = q1 * n;
    this.y = q2 * n;
    this.z = q3 * n;
  }

  /** As the console wants it: [x, y, z, w], turned back by the 90 degrees. */
  quaternion() {
    const c = Math.SQRT1_2;
    const s = -Math.SQRT1_2;
    return [
      c * this.x + s * this.w,
      c * this.y - s * this.z,
      c * this.z + s * this.y,
      c * this.w - s * this.x,
    ];
  }
}

export class DualSense {
  constructor() {
    this.device = null;
    this.bluetooth = false;
    this.outSeq = 0;
    this.calibration = null;
    this.orientation = new Orientation();
    /** What the controller says right now; touches as the app's protocol wants them. */
    this.touches = [{ id: -1, x: 0, y: 0 }, { id: -1, x: 0, y: 0 }];
    this.touchIdNext = 0;
    this.rawIds = [-1, -1];
    this.motion = null;
    /** 0..100, 254 on a cable, 255 unknown: as the protocol counts it. */
    this.battery = 255;
    this.reports = 0;
    this.lastReportMs = 0;
    this.out = {
      triggerLeft: new Uint8Array(11),
      triggerRight: new Uint8Array(11),
      led: [0, 0, 0],
      playerIndex: 0,
      dirty: false,
    };
    this.sending = false;
    this.onChange = null;
    this.onInput = (e) => this.handleReport(e.reportId, new Uint8Array(e.data.buffer, e.data.byteOffset, e.data.byteLength));
    this.onDisconnect = (e) => {
      if (this.device && e.device === this.device) this.detach();
    };
    if (DualSense.supported()) navigator.hid.addEventListener('disconnect', this.onDisconnect);
  }

  static supported() {
    return typeof navigator !== 'undefined' && !!navigator.hid;
  }

  get connected() {
    return !!this.device && this.device.opened;
  }

  /** Whether reports with touchpad and motion are arriving. */
  get live() {
    return this.connected && performance.now() - this.lastReportMs < 1000;
  }

  /**
   * Asks the player to pick the controller (the browser shows its own list).
   * Has to be called from a click.
   * @returns {Promise<string>} empty if it worked, otherwise why not
   */
  async request() {
    if (!DualSense.supported()) return 'This browser cannot talk to the controller directly (it lacks WebHID). Chrome and Edge can.';
    try {
      const devices = await navigator.hid.requestDevice({
        filters: [{ vendorId: SONY, productId: DUALSENSE }, { vendorId: SONY, productId: DUALSENSE_EDGE }],
      });
      if (!devices.length) return 'No controller was chosen.';
      return await this.attach(devices[0]);
    } catch (e) {
      return 'The controller could not be opened: ' + (e && e.message ? e.message : e);
    }
  }

  /** Opens a controller the player has allowed on an earlier visit, without asking again. */
  async reconnect() {
    if (!DualSense.supported() || this.connected) return;
    try {
      for (const device of await navigator.hid.getDevices()) {
        if (device.vendorId === SONY && (device.productId === DUALSENSE || device.productId === DUALSENSE_EDGE)) {
          if (!(await this.attach(device))) return;
        }
      }
    } catch (e) {
      // then there is none
    }
  }

  async attach(device) {
    this.detach();
    try {
      if (!device.opened) await device.open();
    } catch (e) {
      return 'The controller could not be opened: ' + (e && e.message ? e.message : e)
        + '. Another program may be using it.';
    }
    this.device = device;
    this.orientation = new Orientation();
    this.reports = 0;
    device.addEventListener('inputreport', this.onInput);
    // Over Bluetooth the controller only sends its full reports once this has been
    // asked for; it is also where its own calibration of the sensors stands
    try {
      const view = await device.receiveFeatureReport(REPORT_CALIBRATION);
      this.readCalibration(new Uint8Array(view.buffer, view.byteOffset, view.byteLength));
    } catch (e) {
      this.calibration = null;
    }
    this.out.dirty = true;
    if (this.onChange) this.onChange();
    return '';
  }

  detach() {
    const device = this.device;
    this.device = null;
    this.motion = null;
    this.touches = [{ id: -1, x: 0, y: 0 }, { id: -1, x: 0, y: 0 }];
    this.rawIds = [-1, -1];
    this.battery = 255;
    if (device) {
      device.removeEventListener('inputreport', this.onInput);
      device.close().catch(() => {});
    }
    if (this.onChange) this.onChange();
  }

  readCalibration(raw) {
    // Some systems hand the report over with its number in front, some without
    const b = raw.length >= 41 && raw[0] === REPORT_CALIBRATION ? raw.subarray(1) : raw;
    if (b.length < 34) return;
    const v = new DataView(b.buffer, b.byteOffset, b.byteLength);
    const s = (i) => v.getInt16(i * 2, true);
    const gyroBias = [s(0), s(1), s(2)];
    const plus = [s(3), s(5), s(7)];
    const minus = [s(4), s(6), s(8)];
    const speed2x = s(9) + s(10);
    const accPlus = [s(11), s(13), s(15)];
    const accMinus = [s(12), s(14), s(16)];
    const gyroScale = [];
    const accBias = [];
    const accScale = [];
    for (let i = 0; i < 3; i++) {
      const range = plus[i] - minus[i];
      // Degrees per second for one raw unit
      gyroScale.push(range > 0 && speed2x > 0 ? speed2x / range : 1 / GYRO_PER_DEG_S);
      const range2g = accPlus[i] - accMinus[i];
      accBias.push(range2g > 0 ? accPlus[i] - range2g / 2 : 0);
      accScale.push(range2g > 0 ? 2 / range2g : 1 / ACCEL_PER_G);
    }
    // A controller that says nonsense is used with the usual values instead
    const sane = gyroScale.every((g) => g > 0.03 && g < 0.12) && accScale.every((a) => a > 0.00006 && a < 0.0003);
    this.calibration = sane ? { gyroBias, gyroScale, accBias, accScale } : null;
  }

  handleReport(id, data) {
    // After the report's number: over Bluetooth one more byte, then the same in both cases
    let b;
    if (id === REPORT_USB_IN && data.length >= 54) {
      b = data;
      this.bluetooth = false;
    } else if (id === REPORT_BT_IN && data.length >= 55) {
      b = data.subarray(1);
      this.bluetooth = true;
    } else return; // the short report a controller sends over Bluetooth before it is asked for more
    const v = new DataView(b.buffer, b.byteOffset, b.byteLength);
    this.reports++;
    this.lastReportMs = performance.now();

    const cal = this.calibration;
    const gyro = [];
    const accel = [];
    for (let i = 0; i < 3; i++) {
      const g = v.getInt16(15 + i * 2, true);
      const a = v.getInt16(21 + i * 2, true);
      const degrees = cal ? (g - cal.gyroBias[i]) * cal.gyroScale[i] : g / GYRO_PER_DEG_S;
      gyro.push((degrees * Math.PI) / 180);
      accel.push(cal ? (a - cal.accBias[i]) * cal.accScale[i] : a / ACCEL_PER_G);
    }
    this.orientation.update(gyro, accel, v.getUint32(27, true));
    this.motion = { gyro, accel, orient: this.orientation.quaternion() };

    for (let i = 0; i < 2; i++) {
      const o = 32 + i * 4;
      const active = (b[o] & 0x80) === 0;
      const rawId = b[o] & 0x7f;
      const x = ((b[o + 2] & 0x0f) << 8) | b[o + 1];
      const y = (b[o + 3] << 4) | (b[o + 2] >> 4);
      const t = this.touches[i];
      if (!active) {
        t.id = -1;
        this.rawIds[i] = -1;
        continue;
      }
      // A finger that was not there before, or a new finger in the same place, gets a new number
      if (t.id < 0 || this.rawIds[i] !== rawId) {
        t.id = this.touchIdNext;
        this.touchIdNext = (this.touchIdNext + 1) & 0x7f;
        this.rawIds[i] = rawId;
      }
      t.x = Math.min(TOUCHPAD_WIDTH, x);
      t.y = Math.min(TOUCHPAD_HEIGHT, y);
    }

    if (b.length > 52) {
      const level = b[52] & 0x0f;
      const charging = b[52] >> 4;
      // Charging or full on the cable; otherwise tenths
      this.battery = charging === 1 || charging === 2 ? 254 : Math.min(100, level * 10 + 5);
    }
    if (this.out.dirty) this.flush();
  }

  // ------------------------------------------------------------------ what the game sends back

  /**
   * The adaptive triggers, as the console sends them: a kind of effect and ten
   * bytes of what it is made of, for each side.
   */
  setTriggers(typeLeft, left, typeRight, right) {
    const l = new Uint8Array(11);
    const r = new Uint8Array(11);
    l[0] = typeLeft;
    l.set(left.subarray(0, 10), 1);
    r[0] = typeRight;
    r.set(right.subarray(0, 10), 1);
    this.out.triggerLeft = l;
    this.out.triggerRight = r;
    this.out.dirty = true;
    this.flush();
  }

  setLed(r, g, b) {
    this.out.led = [r, g, b];
    this.out.dirty = true;
    this.flush();
  }

  setPlayerIndex(index) {
    this.out.playerIndex = index;
    this.out.dirty = true;
    this.flush();
  }

  /** Lets the triggers go and the lights be the controller's own again. */
  release() {
    this.out.triggerLeft = new Uint8Array(11);
    this.out.triggerRight = new Uint8Array(11);
    this.out.dirty = true;
    this.flush();
  }

  async flush() {
    if (!this.connected || this.sending || !this.out.dirty) return;
    this.sending = true;
    this.out.dirty = false;
    // The part that is the same over USB and Bluetooth
    const c = new Uint8Array(47);
    c[0] = FLAG0_RIGHT_TRIGGER | FLAG0_LEFT_TRIGGER;
    c[1] = FLAG1_LIGHTBAR | FLAG1_PLAYER_LEDS;
    c.set(this.out.triggerRight, 10);
    c.set(this.out.triggerLeft, 21);
    c[43] = PLAYER_LEDS[Math.max(0, Math.min(5, this.out.playerIndex))];
    c[44] = this.out.led[0];
    c[45] = this.out.led[1];
    c[46] = this.out.led[2];
    try {
      if (!this.bluetooth) await this.device.sendReport(REPORT_USB_OUT, c);
      else {
        // Bluetooth: a counter and a marker in front, a checksum behind
        const report = new Uint8Array(77);
        report[0] = (this.outSeq << 4) & 0xf0;
        this.outSeq = (this.outSeq + 1) & 0x0f;
        report[1] = 0x10;
        report.set(c, 2);
        const checked = new Uint8Array(2 + 73);
        checked[0] = 0xa2;
        checked[1] = REPORT_BT_OUT;
        checked.set(report.subarray(0, 73), 2);
        const crc = crc32(checked);
        report[73] = crc & 0xff;
        report[74] = (crc >>> 8) & 0xff;
        report[75] = (crc >>> 16) & 0xff;
        report[76] = (crc >>> 24) & 0xff;
        await this.device.sendReport(REPORT_BT_OUT, report);
      }
    } catch (e) {
      // the controller went away in the middle of it
    }
    this.sending = false;
    if (this.out.dirty) this.flush();
  }

  close() {
    this.release();
    if (DualSense.supported()) navigator.hid.removeEventListener('disconnect', this.onDisconnect);
    // A moment for the triggers to be let go
    const device = this.device;
    this.device = null;
    if (device) {
      device.removeEventListener('inputreport', this.onInput);
      setTimeout(() => device.close().catch(() => {}), 200);
    }
  }
}
