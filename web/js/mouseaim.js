// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The mouse as part of a controller: its movement moves a stick, its buttons and wheel are
// buttons (keypad.js works that out). A click on the picture catches the mouse, as games do;
// Esc gives it back, as browsers do. While it is not caught the mouse is the page's own.

import { buttonInput } from './keypad.js';

export class MouseAim {
  /**
   * @param {HTMLElement} target what a click catches the mouse on (the picture)
   * @param {import('./keypad.js').Converter} keypad
   * @param {() => boolean} free whether the mouse may be caught right now (no menu open, nobody else has it)
   * @param {(caught: boolean) => void} onChange the mouse was caught, or came back
   */
  constructor(target, keypad, free, onChange) {
    this.target = target;
    this.keypad = keypad;
    this.free = free || (() => true);
    this.onChange = onChange || (() => {});
    this.on = false;
    this.wheelRest = 0;
    this.handlers = {
      click: () => {
        if (!this.on || this.caught || !this.free() || !this.target.requestPointerLock) return;
        // As the mouse itself reports it, without the system's acceleration, where the browser can
        try {
          const p = this.target.requestPointerLock({ unadjustedMovement: true });
          if (p && p.catch) p.catch(() => this.target.requestPointerLock());
        } catch (e) {
          this.target.requestPointerLock();
        }
      },
      lockChange: () => {
        if (!this.caught) {
          // Its buttons will not be seen coming up
          this.keypad.releaseMouse();
          this.wheelRest = 0;
        }
        this.onChange(this.caught);
      },
      mousemove: (e) => {
        if (!this.caught) return;
        const scale = window.devicePixelRatio || 1;
        this.keypad.move(e.movementX * scale, e.movementY * scale, performance.now());
      },
      mousedown: (e) => this.button(e, true),
      mouseup: (e) => this.button(e, false),
      // The buttons at the mouse's side would take the browser back and forward
      auxclick: (e) => {
        if (this.caught) e.preventDefault();
      },
      wheel: (e) => {
        if (!this.caught) return;
        e.preventDefault();
        // Browsers count pixels, lines or pages, down positive: a notch is about 100 pixels or three lines
        const notch = e.deltaMode === 1 ? 3 : e.deltaMode === 2 ? 1 : 100;
        this.wheelRest -= e.deltaY / notch;
        while (Math.abs(this.wheelRest) >= 1) {
          const step = this.wheelRest > 0 ? 1 : -1;
          this.keypad.wheel(step, performance.now());
          this.wheelRest -= step;
        }
      },
      menu: (e) => {
        if (this.caught) e.preventDefault();
      },
    };
  }

  /** The mouse is the game's right now. */
  get caught() {
    return document.pointerLockElement === this.target;
  }

  /** Whether this browser can catch the mouse at all (a phone cannot). */
  static supported() {
    return !!(document.body && document.body.requestPointerLock);
  }

  button(e, down) {
    if (!this.caught) return;
    e.preventDefault();
    this.keypad.press(buttonInput(e), down);
  }

  setOn(on) {
    if (on === this.on) return;
    this.on = on;
    const h = this.handlers;
    const act = on ? 'addEventListener' : 'removeEventListener';
    this.target[act]('click', h.click);
    document[act]('pointerlockchange', h.lockChange);
    document[act]('mousemove', h.mousemove);
    document[act]('mousedown', h.mousedown);
    document[act]('mouseup', h.mouseup);
    document[act]('auxclick', h.auxclick);
    document[act]('wheel', h.wheel, on ? { passive: false } : undefined);
    document[act]('contextmenu', h.menu);
    if (!on) this.letGo();
  }

  /** Gives the mouse back to the page. */
  letGo() {
    if (this.caught) document.exitPointerLock();
  }
}
