// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Mouse and keyboard for a game on the host's computer, when the host lets this player
// (docs/pc-source.md, section 7). A click on the picture captures the mouse (pointer lock);
// Esc lets it go, as browsers do. Keys travel as USB HID usages: the key's place on the
// keyboard, not its letter, so WASD stay WASD on any layout.

const HID = {
  KeyA: 0x04, KeyB: 0x05, KeyC: 0x06, KeyD: 0x07, KeyE: 0x08, KeyF: 0x09, KeyG: 0x0a, KeyH: 0x0b,
  KeyI: 0x0c, KeyJ: 0x0d, KeyK: 0x0e, KeyL: 0x0f, KeyM: 0x10, KeyN: 0x11, KeyO: 0x12, KeyP: 0x13,
  KeyQ: 0x14, KeyR: 0x15, KeyS: 0x16, KeyT: 0x17, KeyU: 0x18, KeyV: 0x19, KeyW: 0x1a, KeyX: 0x1b,
  KeyY: 0x1c, KeyZ: 0x1d,
  Digit1: 0x1e, Digit2: 0x1f, Digit3: 0x20, Digit4: 0x21, Digit5: 0x22, Digit6: 0x23, Digit7: 0x24,
  Digit8: 0x25, Digit9: 0x26, Digit0: 0x27,
  Enter: 0x28, Escape: 0x29, Backspace: 0x2a, Tab: 0x2b, Space: 0x2c, Minus: 0x2d, Equal: 0x2e,
  BracketLeft: 0x2f, BracketRight: 0x30, Backslash: 0x31, Semicolon: 0x33, Quote: 0x34, Backquote: 0x35,
  Comma: 0x36, Period: 0x37, Slash: 0x38, CapsLock: 0x39,
  F1: 0x3a, F2: 0x3b, F3: 0x3c, F4: 0x3d, F5: 0x3e, F6: 0x3f, F7: 0x40, F8: 0x41, F9: 0x42, F10: 0x43,
  F11: 0x44, F12: 0x45, PrintScreen: 0x46, ScrollLock: 0x47,
  Insert: 0x49, Home: 0x4a, PageUp: 0x4b, Delete: 0x4c, End: 0x4d, PageDown: 0x4e,
  ArrowRight: 0x4f, ArrowLeft: 0x50, ArrowDown: 0x51, ArrowUp: 0x52,
  NumLock: 0x53, NumpadDivide: 0x54, NumpadMultiply: 0x55, NumpadSubtract: 0x56, NumpadAdd: 0x57,
  NumpadEnter: 0x58, Numpad1: 0x59, Numpad2: 0x5a, Numpad3: 0x5b, Numpad4: 0x5c, Numpad5: 0x5d,
  Numpad6: 0x5e, Numpad7: 0x5f, Numpad8: 0x60, Numpad9: 0x61, Numpad0: 0x62, NumpadDecimal: 0x63,
  IntlBackslash: 0x64, ContextMenu: 0x65,
  ControlLeft: 0xe0, ShiftLeft: 0xe1, AltLeft: 0xe2, MetaLeft: 0xe3,
  ControlRight: 0xe4, ShiftRight: 0xe5, AltRight: 0xe6, MetaRight: 0xe7,
};

const MAX_KEYS = 16;

export class KeyMouse {
  /**
   * @param {HTMLElement} target what a click captures the mouse on (the picture)
   * @param {(state: object) => void} onChange gets {buttons, moveX, moveY, wheelY, wheelX, keys}
   * @param {(e: KeyboardEvent) => boolean} isShortcut keys the page keeps (the stream menu's)
   */
  constructor(target, onChange, isShortcut) {
    this.target = target;
    this.onChange = onChange;
    this.isShortcut = isShortcut;
    this.on = false;
    this.state = { buttons: 0, moveX: 0, moveY: 0, wheelY: 0, wheelX: 0, keys: [] };
    this.handlers = {
      click: () => {
        if (this.on && !this.locked && this.target.requestPointerLock) {
          try {
            const p = this.target.requestPointerLock({ unadjustedMovement: true });
            if (p && p.catch) p.catch(() => this.target.requestPointerLock());
          } catch (e) {
            this.target.requestPointerLock();
          }
        }
      },
      lockChange: () => {
        if (!this.locked) this.letGo();
      },
      keydown: (e) => this.key(e, true),
      keyup: (e) => this.key(e, false),
      mousemove: (e) => {
        if (!this.locked) return;
        const scale = window.devicePixelRatio || 1;
        this.state.moveX = (this.state.moveX + Math.round(e.movementX * scale)) | 0;
        this.state.moveY = (this.state.moveY + Math.round(e.movementY * scale)) | 0;
        this.emit();
      },
      button: (e) => {
        if (!this.locked) return;
        e.preventDefault();
        // The same bits as on the wire: left, right, middle, back, forward
        this.state.buttons = e.buttons & 0x1f;
        this.emit();
      },
      wheel: (e) => {
        if (!this.locked) return;
        e.preventDefault();
        // In 1/120 of a notch, up and right positive; browsers count pixels, lines or pages down
        const unit = e.deltaMode === 1 ? 40 : e.deltaMode === 2 ? 120 : 1.2;
        this.state.wheelY = (this.state.wheelY - Math.round(e.deltaY * unit)) | 0;
        this.state.wheelX = (this.state.wheelX + Math.round(e.deltaX * unit)) | 0;
        this.emit();
      },
      menu: (e) => {
        if (this.locked) e.preventDefault();
      },
      blur: () => this.letGo(),
    };
  }

  get locked() {
    return document.pointerLockElement === this.target;
  }

  static supported() {
    return !!(document.body && document.body.requestPointerLock);
  }

  setOn(on) {
    if (on === this.on) return;
    this.on = on;
    const h = this.handlers;
    if (on) {
      this.target.addEventListener('click', h.click);
      document.addEventListener('pointerlockchange', h.lockChange);
      // Before the page's own key handling (the keyboard as a controller)
      window.addEventListener('keydown', h.keydown, true);
      window.addEventListener('keyup', h.keyup, true);
      document.addEventListener('mousemove', h.mousemove);
      document.addEventListener('mousedown', h.button);
      document.addEventListener('mouseup', h.button);
      document.addEventListener('wheel', h.wheel, { passive: false });
      document.addEventListener('contextmenu', h.menu);
      window.addEventListener('blur', h.blur);
    } else {
      this.target.removeEventListener('click', h.click);
      document.removeEventListener('pointerlockchange', h.lockChange);
      window.removeEventListener('keydown', h.keydown, true);
      window.removeEventListener('keyup', h.keyup, true);
      document.removeEventListener('mousemove', h.mousemove);
      document.removeEventListener('mousedown', h.button);
      document.removeEventListener('mouseup', h.button);
      document.removeEventListener('wheel', h.wheel);
      document.removeEventListener('contextmenu', h.menu);
      window.removeEventListener('blur', h.blur);
      if (this.locked) document.exitPointerLock();
      this.letGo();
    }
  }

  key(e, down) {
    if (!this.locked) return;
    if (down && this.isShortcut && this.isShortcut(e)) {
      // The stream menu opens and the mouse comes back
      document.exitPointerLock();
      return;
    }
    const hid = HID[e.code];
    if (!hid) return;
    e.preventDefault();
    e.stopImmediatePropagation();
    if (e.repeat) return;
    const keys = this.state.keys;
    const at = keys.indexOf(hid);
    if (down && at < 0 && keys.length < MAX_KEYS) keys.push(hid);
    else if (!down && at >= 0) keys.splice(at, 1);
    else return;
    this.emit();
  }

  /** Nothing held any more: the mouse came back, the window went to the background. */
  letGo() {
    if (!this.state.buttons && !this.state.keys.length) return;
    this.state.buttons = 0;
    this.state.keys = [];
    this.emit();
  }

  emit() {
    this.onChange({ ...this.state, keys: this.state.keys.slice() });
  }
}
