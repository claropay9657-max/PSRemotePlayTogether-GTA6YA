// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The keyboard and the mouse as a controller: keys, mouse buttons and the wheel are buttons
// of the controller or directions of its sticks, and the mouse's movement moves a stick, so
// that a game that only knows controllers can be played the way a game on a computer is.
//
// This is the part that works it out, the same way the app does (a port of
// gui/src/keymousepad.cpp; test/web/keypad.test.mjs holds it to moments worked out by the
// app's own code). It knows no page and no storage: it is told what was pressed and how far
// the mouse moved, with the time in milliseconds, and says what the controller looks like.
//
// An input is written as a short word: "k:KeyW" is a key by its place on the keyboard (what
// a browser calls its code, so WASD stay WASD on any layout), "m:1" to "m:5" a mouse button
// (left, right, middle, back, forward), "w:up" and "w:down" a notch of the wheel.

import * as P from './proto.js';

/** What an input can be bound to, in the order the app has them. */
export const FUNCTIONS = [
  ['cross', '✕ Cross'], ['circle', '○ Circle'], ['square', '□ Square'], ['triangle', '△ Triangle'],
  ['dpad_up', 'D-pad up'], ['dpad_down', 'D-pad down'], ['dpad_left', 'D-pad left'], ['dpad_right', 'D-pad right'],
  ['l1', 'L1'], ['r1', 'R1'], ['l2', 'L2'], ['r2', 'R2'], ['l3', 'L3'], ['r3', 'R3'],
  ['options', 'Options'], ['create', 'Create'], ['touchpad', 'Touchpad'], ['ps', 'PS'],
  ['left_up', 'Left stick up'], ['left_down', 'Left stick down'], ['left_left', 'Left stick left'], ['left_right', 'Left stick right'],
  ['right_up', 'Right stick up'], ['right_down', 'Right stick down'], ['right_left', 'Right stick left'], ['right_right', 'Right stick right'],
  ['walk', 'Walk: the left stick part of the way'],
];

/** How many inputs one function takes. */
export const SLOTS = 3;

const BUTTONS = [
  ['cross', P.BTN.Cross], ['circle', P.BTN.Circle], ['square', P.BTN.Square], ['triangle', P.BTN.Triangle],
  ['dpad_up', P.BTN.DpadUp], ['dpad_down', P.BTN.DpadDown], ['dpad_left', P.BTN.DpadLeft], ['dpad_right', P.BTN.DpadRight],
  ['l1', P.BTN.L1], ['r1', P.BTN.R1], ['l3', P.BTN.L3], ['r3', P.BTN.R3],
  ['options', P.BTN.Options], ['create', P.BTN.Share], ['touchpad', P.BTN.Touchpad], ['ps', P.BTN.Ps],
];

/** A mouse speed, in the mouse's own steps a millisecond, that pushes the stick all the way at sensitivity 1. */
export const FULL_SPEED = 3.0;
/** One notch of the wheel is a press this long, and the next one waits this long after it. */
const WHEEL_PRESS_MS = 50;
const WHEEL_GAP_MS = 40;
/** No more notches than this wait to be pressed: a spin of the wheel is not a minute of presses. */
const WHEEL_WAITING = 6;
const HALF_DIAGONAL = 0.70710678118654752440; // 1 / sqrt(2)

/** Acceleration, as the app names it: even, fine at slow speed, quick at slow speed. */
export const CURVES = {
  even: [0.2, 0.4, 0.6, 0.8],
  fine: [0.08, 0.23, 0.44, 0.70],
  quick: [0.38, 0.58, 0.74, 0.87],
};

export const isMouse = (input) => input.startsWith('m:') || input.startsWith('w:');

function clamped(v, low, high) {
  // Not a number counts as the lowest
  if (!(v >= low)) return low;
  return v > high ? high : v;
}

/** Rounded the way the app rounds: a half goes away from zero. */
const rounded = (v) => (v < 0 ? -Math.round(-v) : Math.round(v));
const stickValue = (v) => rounded(clamped(v, -1, 1) * 32767);

/** The curve as it may be: every point between 0 and 1 and none below the one before it. */
export function curveMadeValid(curve) {
  let floor = 0;
  return [0, 1, 2, 3].map((i) => {
    floor = clamped(Number(curve && curve[i]), floor, 1);
    return floor;
  });
}

/** Where the curve puts a speed between 0 and 1. */
export function curveAt(curve, speed) {
  if (!(speed > 0)) return 0;
  if (speed >= 1) return 1;
  // Six points a fifth apart, straight lines between them
  const points = [0, curve[0], curve[1], curve[2], curve[3], 1];
  const at = speed * 5;
  const part = Math.min(Math.floor(at), 4);
  return points[part] + (points[part + 1] - points[part]) * (at - part);
}

/** "even", "fine" or "quick" if the curve is one of those, otherwise "". */
export function curveName(curve) {
  for (const [name, points] of Object.entries(CURVES)) {
    if (points.every((p, i) => Math.abs(p - curve[i]) < 0.005)) return name;
  }
  return '';
}

/** How the mouse aims, and the rest that can be set; what the app starts with. */
export function defaultOptions() {
  return {
    /** The mouse aims: its movement moves a stick, and its buttons and wheel count. */
    aim: false,
    /** The stick the mouse moves: the right one, or the left one. */
    rightStick: true,
    /** 1 is as it comes; across, and up and down. */
    sensX: 1,
    sensY: 1,
    /** How far the stick goes at 20, 40, 60 and 80 % of the speed that pushes it all the way. */
    curve: CURVES.even.slice(),
    /** The smallest movement of the mouse already pushes the stick this far, past what a game ignores; 0 to 0.5. */
    deadZone: 0.12,
    /** The mouse's speed is taken over this long, in ms: longer is calmer and later. */
    smoothingMs: 24,
    /** While the aim button is held, the mouse counts this much; 1 is no difference. */
    aimFactor: 0.6,
    /** The button that means aiming: "l2", "l1" or "" for none. */
    aimButton: 'l2',
    invertY: false,
    /** How far the left stick goes while Walk is held. */
    walk: 0.5,
  };
}

export class Converter {
  constructor() {
    this.options = defaultOptions();
    /** @type {Map<string, string[]>} function -> its inputs */
    this.bindings = new Map();
    /** @type {Map<string, string>} input -> the function it is bound to */
    this.byInput = new Map();
    /** @type {Map<string, string>} what is held down, and what for */
    this.down = new Map();
    /** @type {Map<string, number>} how many inputs hold a function */
    this.held = new Map();
    this.moves = [];
    this.pulses = [];
  }

  setOptions(o) {
    const wasAiming = this.options.aim;
    const d = defaultOptions();
    const n = { ...d, ...(o || {}) };
    this.options = {
      aim: !!n.aim,
      rightStick: n.rightStick !== false,
      sensX: clamped(Number(n.sensX), 0.05, 10),
      sensY: clamped(Number(n.sensY), 0.05, 10),
      curve: Array.isArray(n.curve) && n.curve.length === 4 ? curveMadeValid(n.curve) : CURVES.even.slice(),
      deadZone: clamped(Number(n.deadZone), 0, 0.5),
      smoothingMs: Math.trunc(clamped(Number(n.smoothingMs), 1, 500)),
      aimFactor: clamped(Number(n.aimFactor), 0.05, 2),
      aimButton: n.aimButton === 'l2' || n.aimButton === 'l1' ? n.aimButton : '',
      invertY: !!n.invertY,
      walk: clamped(Number(n.walk), 0.1, 1),
    };
    // The mouse is somebody else's again: its buttons will not be seen coming up
    if (wasAiming && !this.options.aim) this.releaseMouse();
  }

  /**
   * New bindings: everything held is let go, since what it was held for may be gone.
   * @param {Object<string, string[]>} bindings function -> inputs; an input bound twice counts for the first
   */
  setBindings(bindings) {
    this.releaseAll();
    this.bindings = new Map();
    this.byInput = new Map();
    for (const [id] of FUNCTIONS) {
      const kept = [];
      for (const input of (bindings && bindings[id]) || []) {
        if (typeof input !== 'string' || !/^(k:\w+|m:[1-5]|w:(up|down))$/.test(input)) continue;
        if (kept.length >= SLOTS || this.byInput.has(input)) continue;
        kept.push(input);
        this.byInput.set(input, id);
      }
      this.bindings.set(id, kept);
    }
  }

  /** The bindings as they were taken, for keeping: function -> inputs. */
  getBindings() {
    const out = {};
    for (const [id, inputs] of this.bindings) out[id] = inputs.slice();
    return out;
  }

  /** Whether anything is bound to this input (a mouse button only while the mouse aims). */
  bound(input) {
    return this.byInput.has(input) && (this.options.aim || !isMouse(input));
  }

  /**
   * A key or a mouse button went down or came up.
   * @returns {string} what it is bound to; "" for nothing, or a mouse button while the mouse does not aim
   */
  press(input, isDown) {
    if (!input || input.startsWith('w:')) return '';
    if (isMouse(input) && !this.options.aim) return '';
    if (isDown) {
      const already = this.down.get(input);
      if (already) return already; // held already: a key that repeats
      const f = this.byInput.get(input);
      if (!f) return '';
      this.down.set(input, f);
      this.held.set(f, (this.held.get(f) || 0) + 1);
      return f;
    }
    const f = this.down.get(input);
    if (!f) return '';
    this.down.delete(input);
    const count = this.held.get(f) || 0;
    if (count > 0) this.held.set(f, count - 1);
    return f;
  }

  /** The wheel turned by this many notches, up counting positive. */
  wheel(notches, nowMs) {
    if (!this.options.aim || !notches) return;
    const f = this.byInput.get(notches > 0 ? 'w:up' : 'w:down');
    if (!f) return;
    for (let i = Math.abs(notches); i > 0 && this.pulses.length < WHEEL_WAITING; i--) {
      // One press for each notch, one after the other, so that the game counts them
      let from = nowMs;
      for (const pulse of this.pulses) {
        if (pulse.f === f && pulse.until + WHEEL_GAP_MS > from) from = pulse.until + WHEEL_GAP_MS;
      }
      this.pulses.push({ f, from, until: from + WHEEL_PRESS_MS });
    }
  }

  /** The mouse moved by this much; does nothing while it does not aim. */
  move(dx, dy, nowMs) {
    if (!this.options.aim || (dx === 0 && dy === 0)) return;
    this.moves.push({ at: nowMs, dx, dy });
    // Whoever asks for the state drops what is old; this only keeps a mouse nobody asks about from piling up
    while (this.moves.length > 4096) this.moves.shift();
  }

  /** Lets go of everything: the window lost the keyboard, a menu opened. */
  releaseAll() {
    this.down.clear();
    this.held.clear();
    this.moves.length = 0;
    this.pulses.length = 0;
  }

  /** Lets go of what the mouse holds: it came back to the page. */
  releaseMouse() {
    for (const [input, f] of this.down) {
      if (!isMouse(input)) continue;
      this.down.delete(input);
      const count = this.held.get(f) || 0;
      if (count > 0) this.held.set(f, count - 1);
    }
    this.moves.length = 0;
    this.pulses.length = 0;
  }

  isHeld(f, nowMs) {
    if (!f) return false;
    if ((this.held.get(f) || 0) > 0) return true;
    return this.pulses.some((pulse) => pulse.f === f && pulse.from <= nowMs && nowMs < pulse.until);
  }

  /** The controller as it is now: {buttons, l2, r2, lx, ly, rx, ry}, sticks from -32767 to 32767. */
  state(nowMs) {
    this.pulses = this.pulses.filter((pulse) => pulse.until > nowMs);
    const o = this.options;
    const window = o.smoothingMs;
    while (this.moves.length && this.moves[0].at + window <= nowMs) this.moves.shift();

    const pad = { buttons: 0, l2: 0, r2: 0, lx: 0, ly: 0, rx: 0, ry: 0 };
    for (const [f, bit] of BUTTONS) {
      if (this.isHeld(f, nowMs)) pad.buttons |= bit;
    }
    pad.l2 = this.isHeld('l2', nowMs) ? 255 : 0;
    pad.r2 = this.isHeld('r2', nowMs) ? 255 : 0;

    // The sticks from keys: two opposite ones cancel, two neighbours meet on the circle's edge
    const fromKeys = (up, down, left, right) => {
      let x = (this.isHeld(right, nowMs) ? 1 : 0) - (this.isHeld(left, nowMs) ? 1 : 0);
      let y = (this.isHeld(down, nowMs) ? 1 : 0) - (this.isHeld(up, nowMs) ? 1 : 0);
      if (x !== 0 && y !== 0) {
        x *= HALF_DIAGONAL;
        y *= HALF_DIAGONAL;
      }
      return [x, y];
    };
    let [leftX, leftY] = fromKeys('left_up', 'left_down', 'left_left', 'left_right');
    let [rightX, rightY] = fromKeys('right_up', 'right_down', 'right_left', 'right_right');
    if (this.isHeld('walk', nowMs)) {
      leftX *= o.walk;
      leftY *= o.walk;
    }

    // The mouse: how fast it is over the last moments says how far the stick goes
    if (o.aim && this.moves.length) {
      let movedX = 0;
      let movedY = 0;
      for (const move of this.moves) {
        movedX += move.dx;
        movedY += move.dy;
      }
      const slower = this.isHeld(o.aimButton, nowMs) ? o.aimFactor : 1;
      const x = ((movedX / window) * o.sensX * slower) / FULL_SPEED;
      const y = (((movedY / window) * o.sensY * slower) / FULL_SPEED) * (o.invertY ? -1 : 1);
      const speed = Math.hypot(x, y);
      if (speed > 1e-9) {
        const reach = o.deadZone + (1 - o.deadZone) * curveAt(o.curve, speed < 1 ? speed : 1);
        if (o.rightStick) {
          rightX += (x / speed) * reach;
          rightY += (y / speed) * reach;
        } else {
          leftX += (x / speed) * reach;
          leftY += (y / speed) * reach;
        }
      }
    }

    // A stick may be anywhere inside its circle, not in the corners of the square around it
    const intoCircle = (x, y) => {
      const length = Math.hypot(x, y);
      return length > 1 ? [x / length, y / length] : [x, y];
    };
    [leftX, leftY] = intoCircle(leftX, leftY);
    [rightX, rightY] = intoCircle(rightX, rightY);
    pad.lx = stickValue(leftX);
    pad.ly = stickValue(leftY);
    pad.rx = stickValue(rightX);
    pad.ry = stickValue(rightY);
    return pad;
  }

  /** Whether the controller changes without anything more being pressed: the mouse just moved, a notch of the wheel is still pressed. */
  busy(nowMs) {
    if (this.pulses.some((pulse) => pulse.until > nowMs)) return true;
    const last = this.moves[this.moves.length - 1];
    return this.options.aim && !!last && last.at + this.options.smoothingMs > nowMs;
  }
}

// ------------------------------------------------------------------ what comes with the page

/**
 * Two ways to start from. "classic" is the keys the page always had: both sticks and every
 * button on the keyboard. "shooter" is mouse and WASD as in a game on a computer.
 *
 * Not everything the app's layouts use is a browser's to give: Esc always gives the mouse
 * back, the F keys and Ctrl belong to the browser (Ctrl with W closes the tab), so here
 * Options is Enter, PS is P and Circle is C alone.
 */
export const LAYOUTS = {
  classic: {
    left_up: ['k:KeyW'], left_left: ['k:KeyA'], left_down: ['k:KeyS'], left_right: ['k:KeyD'],
    dpad_up: ['k:ArrowUp'], dpad_left: ['k:ArrowLeft'], dpad_down: ['k:ArrowDown'], dpad_right: ['k:ArrowRight'],
    cross: ['k:KeyK'], circle: ['k:KeyL'], square: ['k:KeyJ'], triangle: ['k:KeyI'],
    l1: ['k:KeyQ'], r1: ['k:KeyE'], l2: ['k:KeyZ'], r2: ['k:KeyC'],
    l3: ['k:KeyF'], r3: ['k:KeyG'],
    right_up: ['k:KeyT'], right_down: ['k:KeyV'], right_left: ['k:KeyR'], right_right: ['k:KeyY'],
    options: ['k:Enter'], create: ['k:Backspace'], ps: ['k:KeyP'], touchpad: ['k:KeyM'],
  },
  shooter: {
    left_up: ['k:KeyW'], left_left: ['k:KeyA'], left_down: ['k:KeyS'], left_right: ['k:KeyD'],
    r2: ['m:1'], l2: ['m:2'],
    cross: ['k:Space'], circle: ['k:KeyC'], square: ['k:KeyR', 'k:KeyE'], triangle: ['k:Digit1', 'w:up', 'w:down'],
    l1: ['k:KeyQ', 'm:4'], r1: ['k:KeyG', 'm:5'],
    l3: ['k:ShiftLeft'], r3: ['k:KeyF', 'm:3'],
    dpad_up: ['k:ArrowUp'], dpad_left: ['k:ArrowLeft'], dpad_down: ['k:ArrowDown'], dpad_right: ['k:ArrowRight'],
    options: ['k:Enter'], create: ['k:Backspace'], ps: ['k:KeyP'], touchpad: ['k:Tab', 'k:KeyM'],
    walk: ['k:AltLeft'],
  },
};

const KEY_NAMES = {
  ArrowUp: '↑', ArrowDown: '↓', ArrowLeft: '←', ArrowRight: '→',
  Space: 'Space', Enter: 'Enter', Backspace: 'Backspace', Tab: 'Tab', Escape: 'Esc', CapsLock: 'Caps Lock',
  ShiftLeft: 'Shift', ShiftRight: 'Right Shift', ControlLeft: 'Ctrl', ControlRight: 'Right Ctrl',
  AltLeft: 'Alt', AltRight: 'Right Alt', MetaLeft: 'Meta', MetaRight: 'Right Meta',
  Minus: '-', Equal: '=', BracketLeft: '[', BracketRight: ']', Backslash: '\\', Semicolon: ';', Quote: "'",
  Backquote: '`', Comma: ',', Period: '.', Slash: '/', IntlBackslash: '<',
  Insert: 'Insert', Delete: 'Delete', Home: 'Home', End: 'End', PageUp: 'Page Up', PageDown: 'Page Down',
};
const MOUSE_NAMES = ['', 'Left button', 'Right button', 'Middle button', 'Back button', 'Forward button'];

/** An input in words, for a key cap: "W", "Shift", "Left button", "Wheel up". */
export function inputLabel(input) {
  if (input.startsWith('m:')) return MOUSE_NAMES[Number(input.slice(2))] || 'Mouse button';
  if (input === 'w:up') return 'Wheel up';
  if (input === 'w:down') return 'Wheel down';
  const code = input.slice(2);
  if (KEY_NAMES[code]) return KEY_NAMES[code];
  if (/^Key[A-Z]$/.test(code)) return code.slice(3);
  if (/^Digit\d$/.test(code)) return code.slice(5);
  if (/^Numpad/.test(code)) return 'Num ' + code.slice(6);
  return code;
}

/** The input a key press means. */
export const keyInput = (e) => (e.code ? 'k:' + e.code : '');
/** The input a mouse button means: a browser counts left, middle, right, back, forward from 0. */
export const buttonInput = (e) => 'm:' + ([1, 3, 2, 4, 5][e.button] || 0);
