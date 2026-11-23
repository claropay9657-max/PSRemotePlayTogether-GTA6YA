// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The window where the keyboard and the mouse are set up as a controller: the two layouts to
// start from, whether and how the mouse aims, and which key or mouse button is which button
// of the controller. It only shows and changes what keypad.js holds; whoever opens it keeps
// the result (onChange).

import { button, el } from './dom.js';
import { CURVES, FUNCTIONS, LAYOUTS, SLOTS, buttonInput, curveName, inputLabel, keyInput } from './keypad.js';

const percent = (v) => `${Math.round(v * 100)} %`;
const times = (v) => `${v.toFixed(2)} ×`;

export class KeysDialog {
  /**
   * @param {HTMLDialogElement} dialog
   * @param {HTMLElement} body where everything but the title and "Close" goes
   * @param {import('./keypad.js').Converter} keypad
   * @param {{canAim: boolean, onChange: function(): void}} h canAim: this browser can catch the mouse
   */
  constructor(dialog, body, keypad, h) {
    this.dialog = dialog;
    this.body = body;
    this.keypad = keypad;
    this.h = h;
    /** What a press is being waited for: {id, slot}, or null. */
    this.listening = null;
    this.onKey = (e) => this.heard(e);
    // Esc while a press is waited for leaves the key as it is, and the window open
    dialog.addEventListener('cancel', (e) => {
      if (!this.listening) return;
      e.preventDefault();
      this.stopListening();
    });
    dialog.addEventListener('close', () => this.stopListening());
    this.build();
  }

  show() {
    this.build();
    if (this.dialog.showModal) this.dialog.showModal();
    else this.dialog.setAttribute('open', '');
  }

  // ------------------------------------------------------------------ what is set

  setOption(patch) {
    this.keypad.setOptions({ ...this.keypad.options, ...patch });
    this.h.onChange();
  }

  setBindings(bindings) {
    this.keypad.setBindings(bindings);
    this.h.onChange();
    this.buildKeys();
  }

  applyLayout(name) {
    this.stopListening();
    this.keypad.setBindings(LAYOUTS[name]);
    // Mouse and WASD means the mouse aims; keys only means it does not
    this.keypad.setOptions({ ...this.keypad.options, aim: name === 'shooter' && this.h.canAim });
    this.h.onChange();
    this.build();
  }

  /** `input` becomes the `slot`th input of `id`; whatever else it was bound to loses it. */
  assign(id, slot, input) {
    const bindings = this.keypad.getBindings();
    for (const inputs of Object.values(bindings)) {
      const at = inputs.indexOf(input);
      if (at >= 0) inputs.splice(at, 1);
    }
    const inputs = bindings[id] || (bindings[id] = []);
    if (slot < inputs.length) inputs[slot] = input;
    else if (inputs.length < SLOTS) inputs.push(input);
    this.setBindings(bindings);
  }

  remove(id, slot) {
    const bindings = this.keypad.getBindings();
    if (bindings[id]) bindings[id].splice(slot, 1);
    this.setBindings(bindings);
  }

  // ------------------------------------------------------------------ waiting for a press

  listen(id, slot) {
    this.stopListening();
    this.listening = { id, slot };
    const name = FUNCTIONS.find(([f]) => f === id)[1];
    this.catcher = el('div', 'keys-listen');
    this.catcher.append(
      el('b', '', `Press what ${name} should be`),
      el('span', '', 'A key, a mouse button on this box, or the wheel. Esc leaves it as it is.'));
    // Mouse buttons and the wheel are taken on the box, so that a click elsewhere stays a click
    this.catcher.addEventListener('mousedown', (e) => {
      e.preventDefault();
      this.take(buttonInput(e));
    });
    this.catcher.addEventListener('wheel', (e) => {
      e.preventDefault();
      if (e.deltaY) this.take(e.deltaY < 0 ? 'w:up' : 'w:down');
    }, { passive: false });
    this.catcher.addEventListener('contextmenu', (e) => e.preventDefault());
    this.catcher.addEventListener('auxclick', (e) => e.preventDefault());
    this.body.append(this.catcher);
    // Before anything else on the page sees the key
    window.addEventListener('keydown', this.onKey, true);
  }

  heard(e) {
    if (!this.listening) return;
    e.preventDefault();
    e.stopImmediatePropagation();
    if (e.repeat) return;
    if (e.code === 'Escape') {
      this.stopListening();
      return;
    }
    const input = keyInput(e);
    if (input) this.take(input);
  }

  take(input) {
    const { id, slot } = this.listening;
    this.stopListening();
    this.assign(id, slot, input);
  }

  stopListening() {
    if (!this.listening) return;
    this.listening = null;
    window.removeEventListener('keydown', this.onKey, true);
    if (this.catcher) this.catcher.remove();
    this.catcher = null;
  }

  // ------------------------------------------------------------------ building it

  build() {
    this.stopListening();
    const o = this.keypad.options;
    const parts = [];

    // Two ways to start from
    const start = el('div', 'keys-start');
    const way = (name, title, line) => {
      const b = button('keys-way', '', () => this.applyLayout(name));
      b.append(el('b', '', title), el('small', '', line));
      return b;
    };
    start.append(
      way('shooter', 'Mouse and WASD', 'The mouse aims, WASD walks: as in a game on a computer.'),
      way('classic', 'Keys only', 'Both sticks and every button on the keyboard, as before.'));
    parts.push(el('h3', '', 'Start from'), start);

    // Whether the mouse aims, and how
    parts.push(el('h3', '', 'The mouse'));
    const mouse = el('div', 'keys-box');
    const aim = this.toggle('The mouse aims',
      this.h.canAim ? 'Moving the mouse moves a stick. Click the picture to catch the mouse; Esc gives it back.'
        : 'This browser cannot catch the mouse, so here only the keys play.',
      o.aim && this.h.canAim, (on) => {
        this.setOption({ aim: on });
        this.build();
      });
    aim.querySelector('input').disabled = !this.h.canAim;
    mouse.append(aim);
    if (o.aim && this.h.canAim) {
      mouse.append(
        this.slider('Speed left and right', '', 0.2, 3, 0.05, o.sensX, times, (v) => this.setOption({ sensX: v })),
        this.slider('Speed up and down', '', 0.2, 3, 0.05, o.sensY, times, (v) => this.setOption({ sensY: v })),
        this.choice('Acceleration', 'Fine is slower for small movements, quick is faster.',
          [['even', 'Even'], ['fine', 'Fine'], ['quick', 'Quick']], curveName(o.curve),
          (name) => this.setOption({ curve: CURVES[name].slice() })),
        this.slider('Past the game\'s dead zone', 'Games ignore a stick that is only a little off its middle. The smallest movement pushes it this far.',
          0, 0.5, 0.01, o.deadZone, percent, (v) => this.setOption({ deadZone: v })),
        this.slider('Smoothing', 'Longer is calmer, and a little later.', 8, 80, 2, o.smoothingMs, (v) => `${v} ms`,
          (v) => this.setOption({ smoothingMs: v })),
        this.choice('Slower while aiming', 'While this button is held, the mouse counts less.',
          [['l2', 'L2'], ['l1', 'L1'], ['', 'Never']], o.aimButton, (name) => {
            this.setOption({ aimButton: name });
            this.build();
          }));
      if (o.aimButton) {
        mouse.append(this.slider('How much slower', '', 0.2, 1, 0.05, o.aimFactor, percent, (v) => this.setOption({ aimFactor: v })));
      }
      mouse.append(
        this.choice('The mouse moves', '', [['right', 'The right stick'], ['left', 'The left stick']], o.rightStick ? 'right' : 'left',
          (name) => this.setOption({ rightStick: name === 'right' })),
        this.toggle('Up and down the other way round', '', o.invertY, (on) => this.setOption({ invertY: on })),
        el('p', 'keys-note', 'Good to know: a console game reads a stick as how fast to turn, so aiming with a mouse plays well once the speeds fit the game, but not like a game made for a mouse.'));
    }
    parts.push(mouse);

    // Which key is which button
    parts.push(el('h3', '', 'Keys and buttons'),
      el('p', 'keys-hint', `Click one to change it, + adds another (up to ${SLOTS}). Mouse buttons and the wheel count while the mouse aims.`));
    this.keys = el('div', 'keys-list');
    parts.push(this.keys);
    const walk = el('div', 'keys-box');
    walk.append(this.slider('Walking', 'How far the left stick goes while the key for walking is held.',
      0.2, 0.9, 0.05, o.walk, percent, (v) => this.setOption({ walk: v })));
    parts.push(walk);

    this.body.replaceChildren(...parts);
    this.buildKeys();
  }

  buildKeys() {
    if (!this.keys) return;
    const bindings = this.keypad.getBindings();
    this.keys.replaceChildren(...FUNCTIONS.map(([id, name]) => {
      const row = el('div', 'keys-row');
      row.append(el('span', 'keys-name', name));
      const caps = el('span', 'keys-caps');
      const inputs = bindings[id] || [];
      inputs.forEach((input, slot) => {
        const cap = el('span', 'cap');
        const key = button('cap-key', inputLabel(input), () => this.listen(id, slot));
        key.title = `Change what ${name} is`;
        const drop = button('cap-x', '×', () => this.remove(id, slot));
        drop.setAttribute('aria-label', `Take ${inputLabel(input)} away from ${name}`);
        cap.append(key, drop);
        caps.append(cap);
      });
      if (inputs.length < SLOTS) {
        const add = button('cap-add', '+', () => this.listen(id, inputs.length));
        add.setAttribute('aria-label', `Add a key for ${name}`);
        caps.append(add);
      }
      row.append(caps);
      return row;
    }));
  }

  /** An on/off switch with its words. */
  toggle(title, line, on, onChange) {
    const label = el('label', 'toggle sub');
    const text = el('span', 'toggle-text');
    text.append(el('b', '', title));
    if (line) text.append(el('small', '', line));
    const input = el('input');
    input.type = 'checkbox';
    input.checked = on;
    input.addEventListener('change', () => onChange(input.checked));
    label.append(text, input);
    return label;
  }

  /** A value on a line from `min` to `max`, with the number next to it. */
  slider(title, line, min, max, step, value, format, onChange) {
    const row = el('label', 'keys-set');
    const text = el('span', 'toggle-text');
    text.append(el('b', '', title));
    if (line) text.append(el('small', '', line));
    const input = el('input');
    input.type = 'range';
    input.min = String(min);
    input.max = String(max);
    input.step = String(step);
    input.value = String(value);
    const shown = el('span', 'keys-value', format(Number(value)));
    input.addEventListener('input', () => {
      const v = Number(input.value);
      shown.textContent = format(v);
      onChange(v);
    });
    row.append(text, input, shown);
    return row;
  }

  /** One of a few, side by side. */
  choice(title, line, options, chosen, onChange) {
    const row = el('div', 'keys-set');
    const text = el('span', 'toggle-text');
    text.append(el('b', '', title));
    if (line) text.append(el('small', '', line));
    const group = el('span', 'keys-seg');
    group.setAttribute('role', 'radiogroup');
    group.setAttribute('aria-label', title);
    for (const [value, label] of options) {
      const b = button(value === chosen ? 'chosen' : '', label, () => {
        for (const other of group.children) {
          other.classList.toggle('chosen', other === b);
          other.setAttribute('aria-checked', other === b ? 'true' : 'false');
        }
        onChange(value);
      });
      b.setAttribute('role', 'radio');
      b.setAttribute('aria-checked', value === chosen ? 'true' : 'false');
      group.append(b);
    }
    row.append(text, group);
    return row;
  }
}
