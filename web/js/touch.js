// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// A controller on the screen, for phones and tablets without one: two sticks,
// the d-pad, the four face buttons, shoulder buttons and triggers, and the
// small ones in the middle. Fingers on the game picture itself work like
// fingers on the controller's touchpad.

import { BTN, TOUCHPAD_HEIGHT, TOUCHPAD_WIDTH } from './proto.js';

const el = (tag, cls, text) => {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text) e.textContent = text;
  return e;
};

export class TouchControls {
  /**
   * @param {HTMLElement} container where the controls are put
   * @param {HTMLElement} surface the game picture: fingers on it are the touchpad
   */
  constructor(container, surface) {
    this.container = container;
    this.surface = surface;
    this.visible = false;
    this.buttons = 0;
    this.l2 = 0;
    this.r2 = 0;
    this.sticks = { l: { x: 0, y: 0 }, r: { x: 0, y: 0 } };
    /** pointer id -> what it holds down */
    this.pointers = new Map();
    this.touches = [{ id: -1, x: 0, y: 0 }, { id: -1, x: 0, y: 0 }];
    this.touchIdNext = 0;
    /** pointer id -> {slot, startX, startY, startMs, moved} */
    this.fingers = new Map();
    this.tapUntil = 0;
    this.used = false;
    this.build();
    this.onSurfaceDown = (e) => this.surfaceDown(e);
    this.onSurfaceMove = (e) => this.surfaceMove(e);
    this.onSurfaceUp = (e) => this.surfaceUp(e);
    surface.addEventListener('pointerdown', this.onSurfaceDown);
    surface.addEventListener('pointermove', this.onSurfaceMove);
    surface.addEventListener('pointerup', this.onSurfaceUp);
    surface.addEventListener('pointercancel', this.onSurfaceUp);
  }

  /** Whether this device is one to offer the controls on by itself. */
  static likely() {
    return typeof matchMedia === 'function' && matchMedia('(pointer: coarse)').matches
      && (navigator.maxTouchPoints || 0) > 0;
  }

  build() {
    const c = this.container;
    c.replaceChildren();
    const button = (cls, label, attrs) => {
      const b = el('div', 't-btn ' + cls, label);
      for (const [k, v] of Object.entries(attrs)) b.dataset[k] = v;
      c.append(b);
      return b;
    };
    const stick = (side) => {
      const s = el('div', 't-stick t-stick-' + side);
      s.dataset.stick = side;
      s.append(el('div', 't-knob'));
      c.append(s);
    };
    stick('l');
    stick('r');
    const dpad = el('div', 't-dpad');
    dpad.dataset.dpad = '1';
    for (const dir of ['up', 'right', 'down', 'left']) dpad.append(el('div', 't-dir t-' + dir));
    c.append(dpad);
    const face = el('div', 't-face');
    for (const [cls, label, name] of [['t-tri', '△', 'Triangle'], ['t-cir', '○', 'Circle'], ['t-cro', '✕', 'Cross'], ['t-squ', '□', 'Square']]) {
      const b = el('div', 't-btn t-round ' + cls, label);
      b.dataset.btn = name;
      face.append(b);
    }
    c.append(face);
    button('t-l2', 'L2', { trigger: 'l2' });
    button('t-l1', 'L1', { btn: 'L1' });
    button('t-r2', 'R2', { trigger: 'r2' });
    button('t-r1', 'R1', { btn: 'R1' });
    button('t-l3', 'L3', { btn: 'L3' });
    button('t-r3', 'R3', { btn: 'R3' });
    button('t-create', 'Create', { btn: 'Share' });
    button('t-pad', 'Touchpad', { btn: 'Touchpad' });
    button('t-options', 'Options', { btn: 'Options' });
    button('t-ps', 'PS', { btn: 'Ps' });

    c.addEventListener('pointerdown', (e) => this.down(e));
    c.addEventListener('pointermove', (e) => this.move(e));
    c.addEventListener('pointerup', (e) => this.up(e));
    c.addEventListener('pointercancel', (e) => this.up(e));
    c.addEventListener('contextmenu', (e) => e.preventDefault());
  }

  show(on) {
    this.visible = on;
    this.container.hidden = !on;
    if (!on) this.releaseAll();
  }

  releaseAll() {
    for (const held of this.pointers.values()) this.release(held);
    this.pointers.clear();
    this.fingers.clear();
    this.buttons = 0;
    this.l2 = 0;
    this.r2 = 0;
    this.sticks.l = { x: 0, y: 0 };
    this.sticks.r = { x: 0, y: 0 };
    for (const t of this.touches) t.id = -1;
  }

  // ------------------------------------------------------------------ the controls

  down(e) {
    const target = e.target.closest('[data-btn], [data-trigger], [data-stick], [data-dpad]');
    if (!target || !this.visible) return;
    e.preventDefault();
    this.used = true;
    try {
      target.setPointerCapture(e.pointerId);
    } catch (err) {
      // an element that cannot hold on to a pointer still gets its events while the finger stays on it
    }
    const held = { target, dpad: 0 };
    this.pointers.set(e.pointerId, held);
    target.classList.add('held');
    if (target.dataset.btn) this.buttons |= BTN[target.dataset.btn];
    else if (target.dataset.trigger) this[target.dataset.trigger] = 255;
    this.track(held, e);
  }

  move(e) {
    const held = this.pointers.get(e.pointerId);
    if (held) this.track(held, e);
  }

  /** Follows a finger on a stick or on the d-pad. */
  track(held, e) {
    const target = held.target;
    const rect = target.getBoundingClientRect();
    const dx = e.clientX - (rect.left + rect.width / 2);
    const dy = e.clientY - (rect.top + rect.height / 2);
    if (target.dataset.stick) {
      const radius = rect.width / 2;
      const length = Math.hypot(dx, dy);
      const scale = length > radius ? radius / length : 1;
      const x = (dx * scale) / radius;
      const y = (dy * scale) / radius;
      this.sticks[target.dataset.stick] = { x, y };
      const knob = target.firstElementChild;
      knob.style.transform = `translate(${x * radius * 0.6}px, ${y * radius * 0.6}px)`;
    } else if (target.dataset.dpad) {
      this.buttons &= ~held.dpad;
      held.dpad = 0;
      // Eight directions; the middle is none
      if (Math.hypot(dx, dy) > rect.width * 0.12) {
        const angle = Math.atan2(dy, dx);
        const sector = Math.round(angle / (Math.PI / 4));
        const map = {
          0: BTN.DpadRight, 1: BTN.DpadRight | BTN.DpadDown, 2: BTN.DpadDown, 3: BTN.DpadDown | BTN.DpadLeft,
          4: BTN.DpadLeft, '-4': BTN.DpadLeft, '-3': BTN.DpadLeft | BTN.DpadUp, '-2': BTN.DpadUp, '-1': BTN.DpadUp | BTN.DpadRight,
        };
        held.dpad = map[sector] || 0;
      }
      this.buttons |= held.dpad;
      target.classList.toggle('up', !!(held.dpad & BTN.DpadUp));
      target.classList.toggle('down', !!(held.dpad & BTN.DpadDown));
      target.classList.toggle('left', !!(held.dpad & BTN.DpadLeft));
      target.classList.toggle('right', !!(held.dpad & BTN.DpadRight));
    }
  }

  up(e) {
    const held = this.pointers.get(e.pointerId);
    if (!held) return;
    this.pointers.delete(e.pointerId);
    this.release(held);
  }

  release(held) {
    const target = held.target;
    target.classList.remove('held', 'up', 'down', 'left', 'right');
    // Another finger may hold the same button
    const still = (pick) => Array.from(this.pointers.values()).some((h) => h !== held && pick(h));
    if (target.dataset.btn) {
      if (!still((h) => h.target === target)) this.buttons &= ~BTN[target.dataset.btn];
    } else if (target.dataset.trigger) {
      if (!still((h) => h.target === target)) this[target.dataset.trigger] = 0;
    } else if (target.dataset.stick) {
      this.sticks[target.dataset.stick] = { x: 0, y: 0 };
      target.firstElementChild.style.transform = '';
    } else if (target.dataset.dpad) this.buttons &= ~held.dpad;
  }

  // ------------------------------------------------------------------ the picture as touchpad

  surfacePoint(e) {
    // Where on the picture, not on the black bars around it
    const rect = this.surface.getBoundingClientRect();
    const pw = this.surface.width || 16;
    const ph = this.surface.height || 9;
    const scale = Math.min(rect.width / pw, rect.height / ph);
    const w = pw * scale;
    const h = ph * scale;
    const x = (e.clientX - (rect.left + (rect.width - w) / 2)) / w;
    const y = (e.clientY - (rect.top + (rect.height - h) / 2)) / h;
    return {
      x: Math.round(Math.max(0, Math.min(1, x)) * TOUCHPAD_WIDTH),
      y: Math.round(Math.max(0, Math.min(1, y)) * TOUCHPAD_HEIGHT),
    };
  }

  surfaceDown(e) {
    if (!this.visible || e.pointerType !== 'touch') return;
    const slot = this.touches.findIndex((t) => t.id < 0);
    if (slot < 0) return;
    const p = this.surfacePoint(e);
    this.touches[slot] = { id: this.touchIdNext, x: p.x, y: p.y };
    this.touchIdNext = (this.touchIdNext + 1) & 0x7f;
    this.fingers.set(e.pointerId, { slot, startX: e.clientX, startY: e.clientY, startMs: performance.now(), moved: false });
    this.used = true;
  }

  surfaceMove(e) {
    const f = this.fingers.get(e.pointerId);
    if (!f) return;
    const p = this.surfacePoint(e);
    this.touches[f.slot].x = p.x;
    this.touches[f.slot].y = p.y;
    if (Math.hypot(e.clientX - f.startX, e.clientY - f.startY) > 12) f.moved = true;
  }

  surfaceUp(e) {
    const f = this.fingers.get(e.pointerId);
    if (!f) return;
    this.fingers.delete(e.pointerId);
    this.touches[f.slot].id = -1;
    // A short tap without moving is a click of the touchpad
    if (!f.moved && performance.now() - f.startMs < 250) this.tapUntil = performance.now() + 90;
  }

  // ------------------------------------------------------------------

  /** Adds what is held on the screen to a controller state. */
  apply(pad) {
    if (!this.visible) return;
    pad.buttons |= this.buttons;
    if (performance.now() < this.tapUntil) pad.buttons |= BTN.Touchpad;
    pad.l2 = Math.max(pad.l2, this.l2);
    pad.r2 = Math.max(pad.r2, this.r2);
    const axis = (v) => Math.max(-32768, Math.min(32767, Math.round(v * 32767)));
    if (this.sticks.l.x || this.sticks.l.y) {
      pad.lx = axis(this.sticks.l.x);
      pad.ly = axis(this.sticks.l.y);
    }
    if (this.sticks.r.x || this.sticks.r.y) {
      pad.rx = axis(this.sticks.r.x);
      pad.ry = axis(this.sticks.r.y);
    }
    if (this.touches.some((t) => t.id >= 0)) {
      pad.touches = this.touches.map((t) => ({ id: t.id, x: t.x, y: t.y }));
      pad.touchIdNext = this.touchIdNext;
    }
  }
}
