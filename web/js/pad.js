// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The player's controller: what the browser's Gamepad API reports, turned into
// the controller state the host expects. A DualSense can add its touchpad and
// motion sensors (dualsense.js), the keyboard and the mouse (keypad.js) and the
// controls on the screen (touch.js) can stand in for a controller. The other way
// round, what the game sends back becomes rumble.

import * as P from './proto.js';
import { Converter, LAYOUTS, keyInput } from './keypad.js';

/** Where the buttons of a "standard" gamepad go. */
const STANDARD_BUTTONS = [
  [0, P.BTN.Cross],
  [1, P.BTN.Circle],
  [2, P.BTN.Square],
  [3, P.BTN.Triangle],
  [4, P.BTN.L1],
  [5, P.BTN.R1],
  [8, P.BTN.Share],
  [9, P.BTN.Options],
  [10, P.BTN.L3],
  [11, P.BTN.R3],
  [12, P.BTN.DpadUp],
  [13, P.BTN.DpadDown],
  [14, P.BTN.DpadLeft],
  [15, P.BTN.DpadRight],
  [16, P.BTN.Ps],
  [17, P.BTN.Touchpad],
];

/** The console's haptics arrive as 10 ms of sound; this many are taken together for one rumble. */
const HAPTICS_PER_RUMBLE = 3;
/** Quieter than this is no rumble at all (of 65535). */
const HAPTICS_MIN = 100;

const stick = (v) => Math.max(-32768, Math.min(32767, Math.round(v * 32767)));
const trigger = (v) => Math.max(0, Math.min(255, Math.round(v * 255)));

/** What kind of controller a gamepad is, from the name the browser gives it. */
export function controllerKind(id) {
  const s = (id || '').toLowerCase();
  if (s.includes('dualsense') || s.includes('0ce6') || s.includes('0df2')) return P.CONTROLLER_DUALSENSE;
  if (s.includes('dualshock') || s.includes('05c4') || s.includes('09cc') || (s.includes('054c') && s.includes('wireless controller'))) {
    return P.CONTROLLER_DUALSHOCK4;
  }
  if (s.includes('xbox') || s.includes('xinput') || s.includes('045e')) return P.CONTROLLER_XBOX;
  return P.CONTROLLER_OTHER;
}

export function controllerName(kind) {
  switch (kind) {
    case P.CONTROLLER_DUALSENSE: return 'DualSense';
    case P.CONTROLLER_DUALSHOCK4: return 'DualShock 4';
    case P.CONTROLLER_XBOX: return 'Xbox controller';
    case P.CONTROLLER_OTHER: return 'Controller';
    default: return '';
  }
}

export class Pad {
  /** @param {function(): boolean} typing whether the player is writing somewhere right now */
  constructor(typing) {
    this.typing = typing || (() => false);
    // The keyboard and the mouse as a controller; the keys the page always had until the player sets others
    this.keypad = new Converter();
    this.keypad.setBindings(LAYOUTS.classic);
    /** "Keyboard as controller": the keys play. */
    this.keyboardOn = true;
    /** Whether a game is on the screen: only then are keys the game's (set by the page). */
    this.playing = () => true;
    /** The key that catches the mouse for aiming and lets it go was pressed (set by the page). */
    this.onMouseKey = null;
    this.keyboardUsed = false;
    // The mouse as the touchpad: one finger, and whether the touchpad is pressed
    this.mouseFinger = null;
    this.mouseTouchId = 0;
    this.mouseTouchPress = false;
    this.kind = P.CONTROLLER_NONE;
    this.label = '';
    this.battery = P.BATTERY_UNKNOWN;
    /** @type {import('./touch.js').TouchControls|null} */
    this.touch = null;
    /** @type {import('./dualsense.js').DualSense|null} */
    this.dualsense = null;
    // What the game wants felt: its rumble (0..255 each), and its haptics boiled down to a strength (0..1)
    this.gameRumble = { left: 0, right: 0 };
    this.haptic = 0;
    this.hapticQueue = [];
    this.hapticAtMs = 0;
    this.hapticsScale = 1;
    this.applied = { strong: 0, weak: 0, at: 0 };
    this.onKeyDown = (e) => this.key(e, true);
    this.onKeyUp = (e) => this.key(e, false);
    this.onBlur = () => {
      this.keypad.releaseAll();
      this.setMouseTouch(null);
      this.mouseTouchPress = false;
    };
    window.addEventListener('keydown', this.onKeyDown);
    window.addEventListener('keyup', this.onKeyUp);
    window.addEventListener('blur', this.onBlur);
  }

  key(e, down) {
    const input = keyInput(e);
    // A key always comes up, whatever happened since it went down
    if (!down) {
      if (this.keypad.press(input, false)) e.preventDefault();
      return;
    }
    if (!this.keyboardOn || !this.playing() || this.typing()) return;
    // A key together with ⌘ or Ctrl is the browser's own, unless Ctrl itself is a key to play with
    const modifier = /^(Control|Meta|Alt|Shift)/.test(e.code);
    const ctrlPlays = this.keypad.bound('k:ControlLeft') || this.keypad.bound('k:ControlRight');
    if (!modifier && (e.metaKey || (e.ctrlKey && !ctrlPlays))) return;
    if (!this.keypad.bound(input)) return;
    // The page must not scroll, go back or move the focus while somebody plays
    e.preventDefault();
    if (e.repeat) return;
    if (this.keypad.press(input, true) === 'mouse') {
      // No button of the controller: it catches the mouse for aiming and lets it go
      if (this.onMouseKey) this.onMouseKey();
      return;
    }
    this.keyboardUsed = true;
  }

  /**
   * The mouse as the touchpad, as in the app: a finger at x, y of the picture (0 to 1 each),
   * or null when it comes up. The first call puts the finger down, the next ones move it.
   */
  setMouseTouch(point) {
    if (!point) {
      this.mouseFinger = null;
      return;
    }
    const within = (v) => Math.max(0, Math.min(1, v));
    const x = Math.trunc(within(point.x) * P.TOUCHPAD_WIDTH);
    const y = Math.trunc(within(point.y) * P.TOUCHPAD_HEIGHT);
    if (this.mouseFinger) {
      this.mouseFinger.x = x;
      this.mouseFinger.y = y;
    } else {
      this.mouseFinger = { id: this.mouseTouchId, x, y };
      this.mouseTouchId = (this.mouseTouchId + 1) & 0x7f;
    }
  }

  gamepads() {
    if (!navigator.getGamepads) return [];
    const out = [];
    try {
      for (const g of navigator.getGamepads()) if (g && g.connected) out.push(g);
    } catch (e) {
      // a page that the browser does not let near controllers
    }
    return out;
  }

  /** The controller state right now, of all controllers, the keyboard and the screen together. */
  read() {
    const pad = P.emptyPad();
    const pads = this.gamepads();
    let kind = P.CONTROLLER_NONE;
    let label = '';
    for (const g of pads) {
      if (!kind) {
        kind = controllerKind(g.id);
        label = g.id;
      }
      // Anything but the standard layout would need a mapping of its own; most
      // controllers are reported in the standard one
      for (const [index, bit] of STANDARD_BUTTONS) {
        if (g.buttons[index] && g.buttons[index].pressed) pad.buttons |= bit;
      }
      if (g.buttons[6]) pad.l2 = Math.max(pad.l2, trigger(g.buttons[6].value));
      if (g.buttons[7]) pad.r2 = Math.max(pad.r2, trigger(g.buttons[7].value));
      const axes = g.axes;
      const lx = stick(axes[0] || 0);
      const ly = stick(axes[1] || 0);
      const rx = stick(axes[2] || 0);
      const ry = stick(axes[3] || 0);
      // Of several controllers the one that is moved further counts
      if (Math.abs(lx) + Math.abs(ly) > Math.abs(pad.lx) + Math.abs(pad.ly)) {
        pad.lx = lx;
        pad.ly = ly;
      }
      if (Math.abs(rx) + Math.abs(ry) > Math.abs(pad.rx) + Math.abs(pad.ry)) {
        pad.rx = rx;
        pad.ry = ry;
      }
    }

    // A DualSense that was opened directly adds what the Gamepad API leaves out
    let battery = P.BATTERY_UNKNOWN;
    const ds = this.dualsense;
    if (ds && ds.live) {
      if (!kind) kind = P.CONTROLLER_DUALSENSE;
      if (ds.touches.some((t) => t.id >= 0)) {
        pad.touches = ds.touches.map((t) => ({ id: t.id, x: t.x, y: t.y }));
        pad.touchIdNext = ds.touchIdNext;
      }
      if (ds.motion) pad.motion = ds.motion;
      battery = ds.battery;
    }
    this.kind = kind;
    this.label = label;
    this.battery = battery;

    // The mouse as the touchpad, unless a real one is being touched
    if (this.mouseFinger && !pad.touches.some((t) => t.id >= 0)) {
      pad.touches = [{ id: this.mouseFinger.id, x: this.mouseFinger.x, y: this.mouseFinger.y }, { id: -1, x: 0, y: 0 }];
      pad.touchIdNext = this.mouseTouchId;
    }
    if (this.mouseTouchPress) pad.buttons |= P.BTN.Touchpad;

    // The keyboard and the mouse: their buttons count with the controller's, their sticks instead of its own
    const keys = this.keypad.state(performance.now());
    pad.buttons |= keys.buttons;
    pad.l2 = Math.max(pad.l2, keys.l2);
    pad.r2 = Math.max(pad.r2, keys.r2);
    if (keys.lx || keys.ly) {
      pad.lx = keys.lx;
      pad.ly = keys.ly;
    }
    if (keys.rx || keys.ry) {
      pad.rx = keys.rx;
      pad.ry = keys.ry;
    }

    if (this.touch) this.touch.apply(pad);
    return pad;
  }

  /** What the player holds, for the others to see: a controller, the keyboard or the screen. */
  get shownKind() {
    if (this.kind) return this.kind;
    if (this.keyboardUsed || (this.touch && this.touch.used)) return P.CONTROLLER_OTHER;
    return P.CONTROLLER_NONE;
  }

  // ------------------------------------------------------------------ what the game sends back

  /** The game's rumble (0..255 each); 0 and 0 stops it. */
  rumble(left, right) {
    this.gameRumble = { left, right };
    this.applyRumble();
  }

  /**
   * 10 ms of the console's haptics: 3 kHz stereo sound, 16 bit. A browser
   * cannot play it into a controller, so its loudness becomes rumble, the way
   * the app does it for controllers without haptics.
   * @param {Uint8Array} pcm
   */
  haptics(pcm) {
    const samples = pcm.length >> 2;
    if (!samples) return;
    const view = new DataView(pcm.buffer, pcm.byteOffset, pcm.byteLength);
    let left = 0;
    let right = 0;
    for (let i = 0; i < samples; i++) {
      left += Math.abs(view.getInt16(i * 4, true)) * 2;
      right += Math.abs(view.getInt16(i * 4 + 2, true)) * 2;
    }
    left /= samples;
    right /= samples;
    const strength = Math.max(left > HAPTICS_MIN ? left : 0, right > HAPTICS_MIN ? right : 0);
    this.hapticQueue.push(Math.min(65535, strength));
    if (this.hapticQueue.length < HAPTICS_PER_RUMBLE) return;
    let sum = 0;
    for (const s of this.hapticQueue) sum += s;
    this.hapticQueue.length = 0;
    this.haptic = Math.min(1, (sum / HAPTICS_PER_RUMBLE / 65535) * this.hapticsScale);
    this.hapticAtMs = performance.now();
    this.applyRumble();
  }

  /** Call regularly: keeps a lasting rumble going and ends one whose source went quiet. */
  tick() {
    if (this.haptic && performance.now() - this.hapticAtMs > 150) this.haptic = 0;
    this.applyRumble();
  }

  applyRumble() {
    const strong = Math.max(this.gameRumble.left / 255, this.haptic);
    const weak = Math.max(this.gameRumble.right / 255, this.haptic);
    const now = performance.now();
    const same = Math.abs(strong - this.applied.strong) < 0.02 && Math.abs(weak - this.applied.weak) < 0.02;
    // An effect ends by itself, so a lasting one is started again in time
    if (same && (now - this.applied.at < 700 || (!strong && !weak))) return;
    this.applied = { strong, weak, at: now };
    for (const g of this.gamepads()) {
      const actuator = g.vibrationActuator;
      if (!actuator) continue;
      try {
        if (!strong && !weak) {
          if (actuator.reset) actuator.reset();
          else if (actuator.playEffect) actuator.playEffect('dual-rumble', { duration: 1, strongMagnitude: 0, weakMagnitude: 0 });
        } else if (actuator.playEffect) {
          actuator.playEffect('dual-rumble', { duration: 1000, strongMagnitude: strong, weakMagnitude: weak });
        }
      } catch (e) {
        // this browser or controller does not rumble
      }
    }
  }

  /** Whether any connected controller can rumble in this browser. */
  canRumble() {
    return this.gamepads().some((g) => g.vibrationActuator && (g.vibrationActuator.playEffect || g.vibrationActuator.reset));
  }

  stopFeedback() {
    this.gameRumble = { left: 0, right: 0 };
    this.haptic = 0;
    this.hapticQueue.length = 0;
    this.applyRumble();
  }

  close() {
    this.stopFeedback();
    window.removeEventListener('keydown', this.onKeyDown);
    window.removeEventListener('keyup', this.onKeyUp);
    window.removeEventListener('blur', this.onBlur);
  }
}
