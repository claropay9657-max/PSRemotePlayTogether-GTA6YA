// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The mouse as part of a controller: its movement moves a stick, its buttons and wheel are
// buttons (keypad.js works that out). While the player plays the mouse is caught, as in the
// app; a browser lets that happen only right after a click or a key press, so a click on the
// picture catches it, and so does the key for it (F8). Esc always gives it back, as browsers
// do. While it is not caught the mouse is the page's own.

import { buttonInput } from './keypad.js';

export class MouseAim {
  /**
   * @param {HTMLElement} target what a click catches the mouse on (the picture)
   * @param {import('./keypad.js').Converter} keypad
   * @param {() => boolean} free whether the mouse may be caught right now (no menu open, nobody else has it)
   * @param {(caught: boolean, own: boolean) => void} onChange the mouse was caught, or came back; `own`: because this was switched off, not because the player or the browser took it back
   */
  constructor(target, keypad, free, onChange) {
    this.target = target;
    this.keypad = keypad;
    this.free = free || (() => true);
    this.onChange = onChange || (() => {});
    this.on = false;
    this.wheelRest = 0;
    /** Whether the mouse was caught when last looked: a browser also reports changes that are none. */
    this.wasCaught = false;
    /** When the browser was last asked for the mouse: one question at a time. */
    this.askedAt = -1e9;
    this.handlers = {
      click: () => {
        if (this.on && this.free()) this.catchNow();
      },
      lockChange: () => {
        const caught = this.caught;
        if (caught === this.wasCaught) return;
        this.wasCaught = caught;
        if (!caught) {
          // Its buttons will not be seen coming up
          this.keypad.releaseMouse();
          this.wheelRest = 0;
        }
        this.onChange(caught);
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
    if (!on) {
      // Its own doing: nobody is told by the browser any more
      const was = this.wasCaught;
      this.letGo();
      this.wasCaught = false;
      this.keypad.releaseMouse();
      this.wheelRest = 0;
      if (was) this.onChange(false, true);
    }
  }

  /** Catches the mouse now. A browser only does that right after a click or a key press; otherwise nothing happens. */
  catchNow() {
    if (this.caught || !this.target.requestPointerLock) return;
    // A click on a button in the picture reaches the picture too: asked once, not twice
    const now = performance.now();
    if (now - this.askedAt < 250) return;
    this.askedAt = now;
    const plain = () => {
      try {
        const p = this.target.requestPointerLock();
        if (p && p.catch) p.catch(() => {});
      } catch (e) {
        // not now, then
      }
    };
    // As the mouse itself reports it, without the system's acceleration, where the browser can
    try {
      const p = this.target.requestPointerLock({ unadjustedMovement: true });
      if (p && p.catch) p.catch(plain);
    } catch (e) {
      plain();
    }
  }

  /** Gives the mouse back to the page. */
  letGo() {
    if (this.caught) document.exitPointerLock();
  }
}
