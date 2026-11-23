// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Keyboard & mouse: the app's settings page of that name (gui/src/qml/settings/
// KeyboardPage.qml), rebuilt for this page row by row with the same words, ranges and steps:
// the keyboard as a controller, the mouse as its touchpad or, as in a game on a computer, for
// aiming; which key, mouse button or wheel direction is which button of the controller; and a
// place to try it all without a game.
//
// An input is changed right where it is shown, without a pop-up: click it (or press Return on
// it) and it waits; the next key, mouse button or turn of the wheel is the new one. Escape
// keeps the old one.
//
// Where a browser cannot do what the app does, the page says so in the same place: Esc is the
// browser's own (it always gives the mouse back), and the mouse can only be caught right after
// a click or a key press.
//
// It only shows and changes what keypad.js holds and the two switches beside it; whoever
// opens it keeps the result (onChange).

import { button, el } from './dom.js';
import { PadView } from './hud.js';
import { BROWSERS_OWN, CURVES, LAYOUTS, SECTIONS, SLOTS, buttonInput, curveName, inputLabel, keyInput } from './keypad.js';
import { MouseAim } from './mouseaim.js';
import { ask, group, label, menuUnder, pill, row, segmented, slider, toggle } from './setparts.js';

const SVG = 'http://www.w3.org/2000/svg';
const percent = (v) => `${Math.round(v)} %`;
const CURVE_NAMES = ['even', 'fine', 'quick', 'custom'];
const AIM_BUTTONS = ['l2', 'l1', ''];

function svg(tag, attributes) {
  const node = document.createElementNS(SVG, tag);
  for (const [name, value] of Object.entries(attributes || {})) node.setAttribute(name, String(value));
  return node;
}

/**
 * Acceleration, drawn: how far the stick goes (up) for how fast the mouse moves (to the
 * right). The four points in between can be dragged, each no lower than the one before it.
 */
class CurveGraph {
  /** @param {() => number[]} points @param {(points: number[]) => void} onChange */
  constructor(points, onChange) {
    this.points = points;
    this.onChange = onChange;
    // Where the drawing is: room for the words on the left and below
    this.left = 22;
    this.top = 8;
    this.width = 320 - this.left - 12;
    this.height = 190 - this.top - 24;
    const g = svg('svg', { class: 'curve', width: 320, height: 190, viewBox: '0 0 320 190' });
    this.el = g;
    const defs = svg('defs');
    const under = svg('linearGradient', { id: 'curve-under', x1: 0, y1: 0, x2: 1, y2: 0 });
    under.append(svg('stop', { offset: 0, 'stop-color': 'rgba(140, 82, 255, 0.20)' }), svg('stop', { offset: 1, 'stop-color': 'rgba(240, 70, 155, 0.20)' }));
    defs.append(under);
    g.append(defs, svg('rect', { class: 'curve-plot', x: this.left + 0.5, y: this.top + 0.5, width: this.width - 1, height: this.height - 1, rx: 10 }));
    // A line for each fifth, both ways
    for (let i = 1; i <= 4; i++) {
      g.append(svg('line', { class: 'curve-grid', x1: this.atX(i) + 0.5, y1: this.top + 1, x2: this.atX(i) + 0.5, y2: this.top + this.height - 1 }));
      g.append(svg('line', { class: 'curve-grid', x1: this.left + 1, y1: this.atY(i / 5) + 0.5, x2: this.left + this.width - 1, y2: this.atY(i / 5) + 0.5 }));
    }
    // Even, for comparison: twice as fast, twice as far
    g.append(svg('line', { class: 'curve-even', x1: this.atX(0), y1: this.atY(0), x2: this.atX(5), y2: this.atY(1) }));
    this.area = svg('path', { class: 'curve-area', fill: 'url(#curve-under)' });
    this.line = svg('path', { class: 'curve-line' });
    g.append(this.area, this.line);
    const words = (x, y, text, more) => {
      const t = svg('text', { class: 'curve-word', x, y, ...(more || {}) });
      t.textContent = text;
      g.append(t);
    };
    words(this.left, this.top + this.height + 17, 'mouse slow');
    words(this.left + this.width, this.top + this.height + 17, 'mouse fast', { 'text-anchor': 'end' });
    words(0, 0, 'stick far', { 'text-anchor': 'end', transform: `translate(${this.left - 11} ${this.top}) rotate(-90)` });
    // The points to drag
    this.handles = [0, 1, 2, 3].map((i) => {
      const h = svg('g', { class: 'curve-point', tabindex: 0, role: 'slider', 'aria-valuemin': 0, 'aria-valuemax': 100 });
      h.append(svg('circle', { class: 'curve-ring', r: 12 }), svg('circle', { class: 'curve-dot', r: 5.5 }), svg('circle', { class: 'curve-hit', r: 15 }));
      h.addEventListener('pointerdown', (e) => {
        e.preventDefault();
        h.focus();
        h.setPointerCapture(e.pointerId);
        h.classList.add('held');
      });
      h.addEventListener('pointermove', (e) => {
        if (!h.hasPointerCapture(e.pointerId)) return;
        const box = g.getBoundingClientRect();
        // The drawing may be shown smaller than it is drawn
        const y = (e.clientY - box.top) * (190 / box.height);
        this.setPoint(i, (this.top + this.height - y) / this.height);
      });
      const let_go = () => h.classList.remove('held');
      h.addEventListener('pointerup', let_go);
      h.addEventListener('pointercancel', let_go);
      h.addEventListener('keydown', (e) => {
        const step = e.key === 'ArrowRight' || e.key === 'ArrowUp' || e.key === '+' ? 0.02
          : e.key === 'ArrowLeft' || e.key === 'ArrowDown' || e.key === '-' ? -0.02 : 0;
        if (!step) return;
        e.preventDefault();
        this.setPoint(i, this.value(i + 1) + step);
      });
      g.append(h);
      return h;
    });
    this.draw();
  }

  atX(i) {
    return this.left + (this.width * i) / 5;
  }

  atY(value) {
    return this.top + this.height * (1 - value);
  }

  value(i) {
    return i <= 0 ? 0 : i >= 5 ? 1 : this.points()[i - 1] || 0;
  }

  /** Point `i` (0 to 3) to `to`, kept between its neighbours. */
  setPoint(i, to) {
    const next = this.points().slice(0, 4);
    const low = i > 0 ? next[i - 1] : 0;
    const high = i < 3 ? next[i + 1] : 1;
    next[i] = Math.max(low, Math.min(high, Math.round(to * 100) / 100));
    this.onChange(next);
    this.draw();
  }

  draw() {
    let path = `M${this.atX(0)} ${this.atY(0)}`;
    for (let i = 1; i <= 5; i++) path += ` L${this.atX(i)} ${this.atY(this.value(i))}`;
    this.line.setAttribute('d', path);
    this.area.setAttribute('d', `${path} L${this.atX(5)} ${this.atY(0)} Z`);
    this.handles.forEach((h, i) => {
      const at = this.value(i + 1);
      h.setAttribute('transform', `translate(${this.atX(i + 1)} ${this.atY(at)})`);
      h.setAttribute('aria-valuenow', String(Math.round(at * 100)));
      h.setAttribute('aria-label', `At ${(i + 1) * 20} % of full speed the stick goes ${Math.round(at * 100)} % of the way`);
    });
  }
}

export class KeysDialog {
  /**
   * @param {HTMLDialogElement} dialog
   * @param {HTMLElement} body where everything but the title and "Close" goes
   * @param {import('./keypad.js').Converter} keypad
   * @param {object} h
   * @param {boolean} h.canAim this browser can catch the mouse
   * @param {{keyboard: boolean, mouseTouch: boolean}} h.switches the two switches beside the converter; changed in place
   * @param {() => string} h.skin the controller the "Try it" picture shows
   * @param {() => void} h.onChange something was set: keep it
   */
  constructor(dialog, body, keypad, h) {
    this.dialog = dialog;
    this.body = body;
    this.keypad = keypad;
    this.h = h;
    /** What a press is being waited for: {id, slot}, or null. */
    this.listening = null;
    this.trying = false;
    this.wheelTurned = 0;
    this.onListenKey = (e) => this.heardKey(e);
    this.onListenDown = (e) => this.heardButton(e);
    this.onListenWheel = (e) => this.heardWheel(e);
    this.onTryKey = (e) => this.tryKey(e);
    // Escape while a press is waited for or while trying ends that, and the window stays
    dialog.addEventListener('cancel', (e) => {
      if (!this.listening && !this.trying) return;
      e.preventDefault();
      this.stopListening();
      this.setTrying(false);
    });
    dialog.addEventListener('close', () => {
      this.stopListening();
      this.setTrying(false);
    });
    this.build();
  }

  show() {
    this.build();
    if (this.dialog.showModal) this.dialog.showModal();
    else this.dialog.setAttribute('open', '');
  }

  /** Something outside changed what is set (the stream menu's "Mouse aims", the key for it). */
  sync() {
    if (this.dialog.open) this.refresh();
  }

  // ------------------------------------------------------------------ what is set

  setOption(patch) {
    this.keypad.setOptions({ ...this.keypad.options, ...patch });
    this.h.onChange();
    this.refresh();
  }

  setBindings(bindings) {
    this.keypad.setBindings(bindings);
    this.h.onChange();
    this.buildKeys();
    this.refresh();
  }

  /** Every key and mouse button at once: "shooter" (the mouse aims from then on) or "classic" (it no longer does). */
  applyLayout(name) {
    this.stopListening();
    this.keypad.setBindings(LAYOUTS[name]);
    this.keypad.setOptions({ ...this.keypad.options, aim: name === 'shooter' && this.h.canAim });
    this.h.onChange();
    this.buildKeys();
    this.refresh();
  }

  /** `input` becomes the `slot`th input of `id`. One input, one function: it is taken away wherever else it was. */
  assign(id, slot, input) {
    const bindings = this.keypad.getBindings();
    const list = bindings[id] || (bindings[id] = []);
    // The place it had, before the lists change
    const replaces = slot >= 0 && slot < list.length;
    const replaced = replaces ? list[slot] : '';
    for (const inputs of Object.values(bindings)) {
      const at = inputs.indexOf(input);
      if (at >= 0) inputs.splice(at, 1);
    }
    if (replaces) {
      const at = list.indexOf(replaced);
      if (at >= 0) list[at] = input;
      else list.splice(Math.min(slot, list.length), 0, input);
    } else if (list.length < SLOTS) list.push(input);
    else list[list.length - 1] = input;
    this.setBindings(bindings);
    this.focusCap(id, Math.max(0, bindings[id].indexOf(input)));
  }

  clear(id, slot) {
    const bindings = this.keypad.getBindings();
    if (!bindings[id] || slot < 0 || slot >= bindings[id].length) return;
    this.stopListening();
    bindings[id].splice(slot, 1);
    this.setBindings(bindings);
    this.focusCap(id, Math.min(slot, bindings[id].length));
  }

  focusCap(id, slot) {
    const cap = this.keys.querySelector(`.cap[data-id="${id}"][data-slot="${slot}"]`);
    if (cap) cap.focus();
  }

  /** What catches the mouse and lets it go, as it is written on its key; "" if nothing does. */
  catchKey() {
    const inputs = this.keypad.getBindings().mouse || [];
    return inputs.length ? inputLabel(inputs[0]) : '';
  }

  // ------------------------------------------------------------------ waiting for a press

  listen(id, slot) {
    if (this.listening && this.listening.id === id && this.listening.slot === slot) return;
    this.stopListening();
    this.setTrying(false);
    this.keypad.releaseAll();
    this.listening = { id, slot };
    this.wheelTurned = 0;
    // Before anything else on the page sees the key or the click
    window.addEventListener('keydown', this.onListenKey, true);
    window.addEventListener('mousedown', this.onListenDown, true);
    window.addEventListener('wheel', this.onListenWheel, { capture: true, passive: false });
    this.buildKeys();
    this.focusCap(id, slot);
  }

  stopListening() {
    if (!this.listening) return;
    const { id, slot } = this.listening;
    this.listening = null;
    window.removeEventListener('keydown', this.onListenKey, true);
    window.removeEventListener('mousedown', this.onListenDown, true);
    window.removeEventListener('wheel', this.onListenWheel, { capture: true });
    this.buildKeys();
    this.focusCap(id, slot);
  }

  take(input) {
    const { id, slot } = this.listening;
    this.stopListening();
    this.assign(id, slot, input);
  }

  heardKey(e) {
    e.preventDefault();
    e.stopImmediatePropagation();
    if (e.repeat) return;
    if (e.code === 'Escape') {
      this.stopListening();
      return;
    }
    const input = keyInput(e);
    if (input && !BROWSERS_OWN.has(input)) this.take(input);
  }

  heardButton(e) {
    const input = buttonInput(e);
    if (input === 'm:0') return;
    e.preventDefault();
    e.stopImmediatePropagation();
    // The button that chose an input comes up again: that is not a click on anything
    const swallow = (after) => {
      after.preventDefault();
      after.stopImmediatePropagation();
    };
    const clicks = ['click', 'auxclick', 'contextmenu'];
    for (const type of clicks) window.addEventListener(type, swallow, true);
    window.addEventListener('mouseup', (up) => {
      swallow(up);
      // The click that a button coming up makes follows at once
      setTimeout(() => {
        for (const type of clicks) window.removeEventListener(type, swallow, true);
      }, 60);
    }, { capture: true, once: true });
    this.take(input);
  }

  heardWheel(e) {
    e.preventDefault();
    e.stopImmediatePropagation();
    if (!e.deltaY) return;
    // A whole notch, or a clear push on a trackpad. Browsers count pixels, lines or pages, down positive
    const notch = e.deltaMode === 1 ? 3 : e.deltaMode === 2 ? 1 : 100;
    const turned = -e.deltaY / notch;
    this.wheelTurned = (this.wheelTurned > 0) === (turned > 0) ? this.wheelTurned + turned : turned;
    if (Math.abs(this.wheelTurned) < 1) return;
    const input = this.wheelTurned > 0 ? 'w:up' : 'w:down';
    this.wheelTurned = 0;
    this.take(input);
  }

  // ------------------------------------------------------------------ trying it

  setTrying(on) {
    if (on === this.trying) return;
    this.stopListening();
    this.keypad.releaseAll();
    this.trying = on;
    const act = on ? 'addEventListener' : 'removeEventListener';
    window[act]('keydown', this.onTryKey, true);
    window[act]('keyup', this.onTryKey, true);
    if (on) {
      this.syncTryMouse(true);
      const tick = () => {
        if (!this.trying) return;
        this.tryPad.update(this.keypad.state(performance.now()));
        this.tryFrame = requestAnimationFrame(tick);
      };
      tick();
    } else {
      cancelAnimationFrame(this.tryFrame);
      this.tryMouse.setOn(false);
      this.tryPad.update(null);
    }
    this.refresh();
  }

  /** While trying, the mouse is caught if it aims. `now`: this follows a click or a key press, so the browser lets it. */
  syncTryMouse(now) {
    const aims = this.trying && this.keypad.options.aim && this.h.canAim;
    this.tryMouse.setOn(aims);
    if (aims && now) this.tryMouse.catchNow();
  }

  tryKey(e) {
    const input = keyInput(e);
    const down = e.type === 'keydown';
    e.preventDefault();
    e.stopImmediatePropagation();
    if (e.code === 'Escape') {
      if (down) this.setTrying(false);
      return;
    }
    if (e.repeat) return;
    if (this.keypad.press(input, down) === 'mouse' && down) {
      // Catches the mouse, or lets it go: remembered, like the switch above
      this.setOption({ aim: !this.keypad.options.aim && this.h.canAim });
      this.syncTryMouse(true);
    }
  }

  // ------------------------------------------------------------------ building it

  build() {
    this.stopListening();
    const o = this.keypad.options;
    const s = this.h.switches;
    const parts = [];

    // ---- the two switches and the layouts
    const bar = el('div', 'set-bar');
    const barSwitch = (name, tip, on, onChange) => {
      const wrap = el('label', 'set-bar-switch');
      wrap.title = tip;
      wrap.append(toggle(name, on, onChange), el('b', '', name));
      return wrap;
    };
    this.keyboardSwitch = barSwitch('Keyboard as controller', 'Play with the keys below while streaming.', s.keyboard, (on) => {
      s.keyboard = on;
      if (!on) this.setTrying(false);
      this.h.onChange();
      this.refresh();
    });
    this.touchSwitch = barSwitch('Mouse as touchpad',
      'Drag on the picture to swipe on the touchpad; another mouse button presses it. Not while the mouse aims.', s.mouseTouch, (on) => {
        s.mouseTouch = on;
        this.h.onChange();
      });
    const layouts = el('span', 'set-bar-layouts');
    const layoutsButton = pill('ghost', 'Layouts', 'chevron-down', () => menuUnder(layoutsButton, [
      ['Mouse and WASD, as in games on a computer', async () => {
        if (await ask('Mouse and WASD', 'Every key and mouse button gets what games on a computer give it: WASD walks, the mouse aims, the left button fires, the right one aims, Space jumps. What you set yourself is replaced, and the mouse aims from now on.')) this.applyLayout('shooter');
      }],
      ['Keys only, as this page came', async () => {
        if (await ask('Keys only', 'Every button gets the key this page came with: WASD for the left stick, the arrows for the D-pad, K for Cross, Q and E for the shoulders. What you set yourself is replaced, and the mouse no longer aims.')) this.applyLayout('classic');
      }],
    ]), { iconAfter: true });
    layoutsButton.title = 'Every key and mouse button at once: as in games on a computer, or the keys this page came with.';
    layoutsButton.setAttribute('aria-haspopup', 'menu');
    layouts.append(layoutsButton);
    bar.append(this.keyboardSwitch, el('span', 'set-bar-line'), this.touchSwitch, el('span', 'set-bar-gap'), layouts);
    parts.push(bar);

    // ---- the mouse aims
    this.aimSwitch = toggle('The mouse aims', o.aim && this.h.canAim, (on) => this.setOption({ aim: on }));
    this.aimSwitch.disabled = !this.h.canAim;
    this.aimRow = row({ title: 'The mouse aims', subtitle: ' ', switch: this.aimSwitch });

    this.stickSeg = segmented('The mouse moves', ['Right stick', 'Left stick'], o.rightStick ? 0 : 1,
      (index) => this.setOption({ rightStick: index === 0 }));
    this.sensX = slider('Speed, left and right', { min: 10, max: 400, step: 5, value: Math.round(o.sensX * 100), format: percent },
      (v) => this.setOption({ sensX: v / 100 }));
    this.sensY = slider('Speed, up and down', { min: 10, max: 400, step: 5, value: Math.round(o.sensY * 100), format: percent },
      (v) => this.setOption({ sensY: v / 100 }));
    this.curveSeg = segmented('Acceleration', ['Even', 'Fine', 'Quick', 'Own'], 0, (index) => {
      if (index < 3) this.setOption({ curve: CURVES[CURVE_NAMES[index]].slice() });
    });
    this.graph = new CurveGraph(() => this.keypad.options.curve, (points) => this.setOption({ curve: points }));
    const curve = el('span', 'set-curve');
    curve.append(this.curveSeg, this.graph.el);
    this.deadZone = slider('Past the game\'s dead zone', { min: 0, max: 40, step: 1, value: Math.round(o.deadZone * 100), format: percent },
      (v) => this.setOption({ deadZone: v / 100 }));
    this.smoothing = slider('Smoothing', { min: 8, max: 80, step: 4, value: o.smoothingMs, format: (v) => `${Math.round(v)} ms` },
      (v) => this.setOption({ smoothingMs: Math.round(v) }));
    this.aimSeg = segmented('Slower while aiming', ['L2', 'L1', 'Never'], 0, (index) => this.setOption({ aimButton: AIM_BUTTONS[index] }));
    this.aimFactor = slider('Speed while aiming', { min: 20, max: 100, step: 5, value: Math.round(o.aimFactor * 100), width: 170, valueWidth: 46, format: percent },
      (v) => this.setOption({ aimFactor: v / 100 }));
    this.invertSwitch = toggle('Up and down the other way round', o.invertY, (on) => this.setOption({ invertY: on }));
    this.walk = slider('Walking', { min: 20, max: 90, step: 5, value: Math.round(o.walk * 100), format: percent },
      (v) => this.setOption({ walk: v / 100 }));

    this.aimRows = [
      row({ sub: true, title: 'The mouse moves', subtitle: 'The right stick looks around in most games.', textMin: 200, controls: [this.stickSeg] }),
      row({ sub: true, title: 'Speed, left and right', subtitle: 'How far the stick goes for a movement of the mouse. Set the game\'s own stick speed high, then find what fits here.', textMin: 200, controls: [this.sensX] }),
      row({ sub: true, title: 'Speed, up and down', textMin: 200, controls: [this.sensY] }),
      row({ sub: true, title: 'Acceleration', subtitle: 'How much further the stick goes as the mouse gets faster. Even: twice as fast, twice as far. Fine: small movements aim more finely. Quick: the stick answers strongly at once. Or drag the points to a curve of your own.', textMin: 220, controls: [curve] }),
      row({ sub: true, title: 'Past the game\'s dead zone', subtitle: 'Games ignore a stick that is only a little off its middle. The smallest movement of the mouse pushes the stick this far at once. Too low and slow aiming does nothing; too high and it jumps.', textMin: 200, controls: [this.deadZone] }),
      row({ sub: true, title: 'Smoothing', subtitle: 'The mouse\'s speed is taken over this long. Longer is calmer, and a little later.', textMin: 200, controls: [this.smoothing] }),
      row({ sub: true, title: 'Slower while aiming', subtitle: 'While the button for aiming is held, the mouse counts this much: a steadier hand down the sights.', textMin: 200, controls: [this.aimSeg, this.aimFactor] }),
      row({ sub: true, title: 'Up and down the other way round', subtitle: 'Pushing the mouse away looks down, as in a plane.', switch: this.invertSwitch }),
    ];
    this.aimGroup = group('Aiming with the mouse', [
      this.aimRow, ...this.aimRows,
      row({ title: 'Walking', subtitle: 'How far the left stick goes while the key for walking is held. Without it, a key pushes the stick all the way.', textMin: 200, controls: [this.walk] }),
    ]);
    parts.push(this.aimGroup);

    parts.push(el('p', 'set-note', 'Good to know: a console game reads the stick as how fast to turn, with a dead zone and a top speed of its own. So aiming with the mouse plays well once the speeds fit the game, but not like a game made for a mouse, and turning faster than the game\'s top speed is not possible. Some online games do not allow a mouse through a controller in their rules.'));

    // ---- trying it
    this.tryCard = el('section', 'set-try');
    this.tryPad = new PadView(this.h.skin(), 0.3);
    const tryPicture = el('div', 'set-try-pad');
    tryPicture.append(this.tryPad.el);
    this.tryText = el('p', 'set-try-text');
    this.tryButton = pill('primary', 'Try it', 'play', () => this.setTrying(!this.trying));
    const tryWords = el('div', 'set-try-words');
    tryWords.append(label('Try it here'), this.tryText, this.tryButton);
    this.tryCard.append(tryPicture, tryWords);
    // While trying, the mouse is caught on this card
    if (this.tryMouse) this.tryMouse.setOn(false);
    this.tryMouse = new MouseAim(this.tryCard, this.keypad, () => this.trying, (caught, own) => {
      // Escape gave the mouse back: that ends it, as the words on the card say
      if (!caught && !own && this.trying) this.setTrying(false);
      else this.refresh();
    });
    parts.push(this.tryCard);

    // ---- the inputs, one card per part of the controller
    this.keys = el('div', 'set-keys');
    parts.push(this.keys);
    parts.push(el('p', 'set-note', 'Changing happens right here: click a key and press the new one, or a mouse button, or turn the wheel. Up to three for each button of the controller; one key or mouse button does one thing. Escape keeps the old one. Escape itself cannot be a button here: a browser uses it to give the mouse back. Mouse buttons and the wheel count while the mouse aims.'));

    this.body.replaceChildren(...parts);
    this.buildKeys();
    this.refresh();
  }

  /** What depends on what is set, brought up to date without building anything again. */
  refresh() {
    const o = this.keypad.options;
    const s = this.h.switches;
    const aim = o.aim && this.h.canAim;
    this.keyboardSwitch.querySelector('input').checked = s.keyboard;
    this.touchSwitch.querySelector('input').checked = s.mouseTouch;
    // Off while the keyboard does not play
    for (const part of [this.aimGroup, this.tryCard, this.keys]) {
      part.classList.toggle('off', !s.keyboard);
      part.inert = !s.keyboard;
    }
    this.aimSwitch.checked = aim;
    const key = this.catchKey();
    this.aimRow.setSubtitle(!this.h.canAim ? 'This browser cannot catch the mouse, so here only the keys play.'
      : 'Moving the mouse moves a stick, and its buttons and wheel are buttons of the controller. While you play, the mouse is caught: '
        + (key ? `${key} lets it go and catches it again, and the stream menu lets it go too.` : 'the stream menu lets it go.')
        + ' In a browser, click the picture once to catch it; Esc always gives it back.');
    for (const r of this.aimRows) r.setEnabled(aim);
    this.stickSeg.set(o.rightStick ? 0 : 1);
    this.sensX.set(Math.round(o.sensX * 100));
    this.sensY.set(Math.round(o.sensY * 100));
    this.curveSeg.set(Math.max(0, CURVE_NAMES.indexOf(curveName(o.curve))));
    this.graph.draw();
    this.deadZone.set(Math.round(o.deadZone * 100));
    this.smoothing.set(o.smoothingMs);
    this.aimSeg.set(Math.max(0, AIM_BUTTONS.indexOf(o.aimButton)));
    this.aimFactor.set(Math.round(o.aimFactor * 100));
    this.aimFactor.setEnabled(o.aimButton.length > 0);
    this.invertSwitch.checked = o.invertY;
    this.walk.set(Math.round(o.walk * 100));

    this.tryCard.classList.toggle('on', this.trying);
    this.tryText.textContent = !this.trying ? 'See what your keys and your mouse do to the controller, without a game.'
      : this.tryMouse.caught ? 'Your keys and your mouse are the controller now, and the mouse is caught. Escape ends it.'
        : aim ? 'Your keys are the controller now. Click here and the mouse is caught too. Escape ends it.'
          : 'Your keys are the controller now. Escape ends it.';
    this.tryButton.set(this.trying ? 'ghost' : 'primary', this.trying ? 'End' : 'Try it', this.trying ? 'close' : 'play');
  }

  buildKeys() {
    if (!this.keys) return;
    const bindings = this.keypad.getBindings();
    this.keys.replaceChildren(...SECTIONS.map((section) => {
      const card = el('section', 'set-keycard');
      card.append(label(section.title));
      for (const [id, name] of section.keys) {
        const full = section.title === 'Left stick' || section.title === 'Right stick' || section.title === 'D-pad'
          ? `${section.title} ${name.toLowerCase()}` : name;
        const line = el('div', 'set-keyline');
        line.append(el('span', 'set-keyname', name));
        const caps = el('span', 'set-caps');
        const inputs = bindings[id] || [];
        // What is bound, and one more to add while there is room
        const count = Math.min(SLOTS, inputs.length + 1);
        for (let slot = 0; slot < count; slot++) caps.append(this.cap(id, slot, inputs[slot] || '', full));
        line.append(caps);
        card.append(line);
      }
      return card;
    }));
  }

  /** One input of a function, as a key: click it and the next key or mouse button takes its place. The last one of a row, with a plus, adds one. */
  cap(id, slot, input, what) {
    const waiting = !!this.listening && this.listening.id === id && this.listening.slot === slot;
    const cap = button('cap' + (waiting ? ' waiting' : input ? '' : ' add'), waiting ? 'Press…' : input ? inputLabel(input) : '+',
      () => this.listen(id, slot));
    cap.dataset.id = id;
    cap.dataset.slot = String(slot);
    cap.setAttribute('aria-label', input ? `${what}: ${inputLabel(input)}` : `${what}: add a key or mouse button`);
    cap.title = waiting ? 'Press the new key or mouse button, or Escape to keep this one.'
      : input ? 'Press to change it. Delete takes it away.' : 'Add another key or mouse button';
    cap.addEventListener('keydown', (e) => {
      if (this.listening) return;
      if (input && (e.key === 'Delete' || e.key === 'Backspace')) {
        e.preventDefault();
        this.clear(id, slot);
      }
    });
    if (!input || waiting) return cap;
    // Takes the input away: a small cross on the key's corner, while the pointer is on the key
    const wrap = el('span', 'cap-wrap');
    const cross = button('cap-x', '', (e) => {
      e.stopPropagation();
      this.clear(id, slot);
    });
    cross.tabIndex = -1;
    cross.setAttribute('aria-label', `Take ${inputLabel(input)} away from ${what}`);
    wrap.append(cap, cross);
    return wrap;
  }
}
