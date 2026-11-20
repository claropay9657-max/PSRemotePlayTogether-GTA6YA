// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The player's controller: what the browser's Gamepad API reports, turned into
// the controller state the host expects. Without a controller the keyboard can
// stand in.

import * as P from './proto.js';

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

/** The keyboard as a controller, by the place of the key (not the letter printed on it). */
export const KEYBOARD_MAP = [
  ['KeyW', 'Left stick up'], ['KeyA', 'Left stick left'], ['KeyS', 'Left stick down'], ['KeyD', 'Left stick right'],
  ['ArrowUp', 'D-pad up'], ['ArrowLeft', 'D-pad left'], ['ArrowDown', 'D-pad down'], ['ArrowRight', 'D-pad right'],
  ['KeyK', '✕ Cross'], ['KeyL', '○ Circle'], ['KeyJ', '□ Square'], ['KeyI', '△ Triangle'],
  ['KeyQ', 'L1'], ['KeyE', 'R1'], ['KeyZ', 'L2'], ['KeyC', 'R2'],
  ['KeyF', 'L3'], ['KeyG', 'R3'],
  ['KeyT', 'Right stick up'], ['KeyV', 'Right stick down'], ['KeyR', 'Right stick left'], ['KeyY', 'Right stick right'],
  ['Enter', 'Options'], ['Backspace', 'Create'], ['KeyP', 'PS'], ['KeyM', 'Touchpad'],
];

const KEY_BUTTONS = {
  ArrowUp: P.BTN.DpadUp,
  ArrowDown: P.BTN.DpadDown,
  ArrowLeft: P.BTN.DpadLeft,
  ArrowRight: P.BTN.DpadRight,
  KeyK: P.BTN.Cross,
  KeyL: P.BTN.Circle,
  KeyJ: P.BTN.Square,
  KeyI: P.BTN.Triangle,
  KeyQ: P.BTN.L1,
  KeyE: P.BTN.R1,
  KeyF: P.BTN.L3,
  KeyG: P.BTN.R3,
  Enter: P.BTN.Options,
  Backspace: P.BTN.Share,
  KeyP: P.BTN.Ps,
  KeyM: P.BTN.Touchpad,
};

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
    this.keys = new Set();
    this.keyboardOn = true;
    this.keyboardUsed = false;
    this.kind = P.CONTROLLER_NONE;
    this.label = '';
    this.lastRumble = { left: 0, right: 0, at: 0 };
    this.onKeyDown = (e) => this.key(e, true);
    this.onKeyUp = (e) => this.key(e, false);
    this.onBlur = () => this.keys.clear();
    window.addEventListener('keydown', this.onKeyDown);
    window.addEventListener('keyup', this.onKeyUp);
    window.addEventListener('blur', this.onBlur);
  }

  key(e, down) {
    if (!this.keyboardOn || this.typing()) {
      if (!down) this.keys.delete(e.code);
      return;
    }
    if (e.ctrlKey || e.metaKey || e.altKey) return;
    const known = KEYBOARD_MAP.some((k) => k[0] === e.code);
    if (!known) return;
    // The page must not scroll or go back while somebody plays
    e.preventDefault();
    if (down) {
      this.keys.add(e.code);
      this.keyboardUsed = true;
    } else this.keys.delete(e.code);
  }

  gamepads() {
    if (!navigator.getGamepads) return [];
    const out = [];
    for (const g of navigator.getGamepads()) if (g && g.connected) out.push(g);
    return out;
  }

  /** The controller state right now, of all controllers and the keyboard together. */
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
    this.kind = kind;
    this.label = label;

    if (this.keys.size) {
      for (const code of this.keys) {
        const bit = KEY_BUTTONS[code];
        if (bit) pad.buttons |= bit;
      }
      const axis = (minus, plus) => (this.keys.has(plus) ? 32767 : 0) + (this.keys.has(minus) ? -32768 : 0);
      const kx = axis('KeyA', 'KeyD');
      const ky = axis('KeyW', 'KeyS');
      if (kx || ky) {
        pad.lx = Math.max(-32768, Math.min(32767, kx));
        pad.ly = Math.max(-32768, Math.min(32767, ky));
      }
      const krx = axis('KeyR', 'KeyY');
      const kry = axis('KeyT', 'KeyV');
      if (krx || kry) {
        pad.rx = Math.max(-32768, Math.min(32767, krx));
        pad.ry = Math.max(-32768, Math.min(32767, kry));
      }
      if (this.keys.has('KeyZ')) pad.l2 = 255;
      if (this.keys.has('KeyC')) pad.r2 = 255;
    }
    return pad;
  }

  /** Makes the controllers rumble as the game asks (0..255 each); 0 and 0 stops it. */
  rumble(left, right) {
    const now = performance.now();
    const same = left === this.lastRumble.left && right === this.lastRumble.right;
    // An effect ends by itself, so a lasting one is started again in time
    if (same && (now - this.lastRumble.at < 700 || (!left && !right))) return;
    this.lastRumble = { left, right, at: now };
    for (const g of this.gamepads()) {
      const actuator = g.vibrationActuator;
      if (!actuator) continue;
      try {
        if (!left && !right) {
          if (actuator.reset) actuator.reset();
          else if (actuator.playEffect) actuator.playEffect('dual-rumble', { duration: 1, strongMagnitude: 0, weakMagnitude: 0 });
        } else if (actuator.playEffect) {
          actuator.playEffect('dual-rumble', { duration: 1000, strongMagnitude: left / 255, weakMagnitude: right / 255 });
        }
      } catch (e) {
        // this browser or controller does not rumble
      }
    }
  }

  close() {
    this.rumble(0, 0);
    window.removeEventListener('keydown', this.onKeyDown);
    window.removeEventListener('keyup', this.onKeyUp);
    window.removeEventListener('blur', this.onBlur);
  }
}
