// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The display on top of the game picture: who is playing, their webcams, what
// everybody presses and what the console gets out of it. A port of the app's
// gui/src/qml/StreamHud.qml and controls/PadView.qml, piece by piece: the same
// four elements with a switch, a place and a size each, the same looks, the
// same notes, the same arrange mode. What it looks like is in css/game.css.

import { button, el } from './dom.js';

// ------------------------------------------------------------------ one controller

/** Skins that bring every picture themselves; the others borrow all but body and face buttons from the white one. */
const COMPLETE_SKINS = new Set(['PS5', 'PS5 (black)', 'PS5 (cosmic red)']);

function spriteUrl(skin, file) {
  const own = file === 'base.png' || file === 'button.png' || COMPLETE_SKINS.has(skin);
  return `url("pad/${encodeURIComponent(own ? skin : 'PS5')}/${file}")`;
}

const BIT = {
  cross: 1 << 0, circle: 1 << 1, square: 1 << 2, triangle: 1 << 3,
  left: 1 << 4, right: 1 << 5, up: 1 << 6, down: 1 << 7,
  l1: 1 << 8, r1: 1 << 9, l3: 1 << 10, r3: 1 << 11,
  options: 1 << 12, share: 1 << 13, touchpad: 1 << 14, ps: 1 << 15,
};

/**
 * A DualSense that shows what is being pressed, drawn with the sprites of
 * CryptoPad's PS5 skins at the places of its ps5-controller.css.
 */
export class PadView {
  /** @param {number} factor size relative to the sprites' own 807 x 651 */
  constructor(skin = 'PS5', factor = 0.2) {
    this.skin = '';
    this.el = el('div', 'pad');
    this.el.style.width = Math.round(807 * factor) + 'px';
    this.el.style.height = Math.round(651 * factor) + 'px';
    this.inner = el('div', 'pad-inner');
    this.inner.style.transform = `scale(${factor})`;
    this.el.append(this.inner);
    this.sprites = [];
    const sprite = (file, x, y, w, h, clipX = 0, clipY = 0, parent = this.inner) => {
      const s = el('div', 'pad-sprite');
      s.style.left = x + 'px';
      s.style.top = y + 'px';
      s.style.width = w + 'px';
      s.style.height = h + 'px';
      s.style.backgroundPosition = `${-clipX}px ${-clipY}px`;
      s.dataset.file = file;
      parent.append(s);
      this.sprites.push(s);
      return s;
    };
    const group = (x, y) => {
      const g = el('div', 'pad-group');
      g.style.left = x + 'px';
      g.style.top = y + 'px';
      this.inner.append(g);
      return g;
    };
    sprite('base.png', 0, 0, 807, 651);
    // Triggers fade in with how far they are pulled
    this.lt = sprite('trigger.png', 94, 0, 111, 108, 0, 0);
    this.rt = sprite('trigger.png', 94 + 619 - 111, 0, 111, 108, 113, 0);
    this.lb = sprite('bumper.png', 93, 114, 110, 35);
    this.rb = sprite('bumper.png', 93 + 620 - 110, 114, 110, 35);
    this.rb.style.transform = 'scaleX(-1)';
    this.touchpad = sprite('touchpad.png', 237, 118, 333, 177);
    this.meta = sprite('meta.png', 378, 356, 51, 40);
    // Create and Options
    this.share = sprite('back_start.png', 195, 140, 27, 57, 0, 0);
    this.options = sprite('back_start.png', 195 + 416 - 27, 140, 27, 57, 28, 0);
    // Cross, Circle, Square, Triangle
    const face = group(573, 178);
    this.face = [
      [sprite('button.png', 61, 123, 58, 58, 0, 0, face), BIT.cross, 0],
      [sprite('button.png', 123, 62, 58, 58, 59, 0, face), BIT.circle, 1],
      [sprite('button.png', 0, 61, 58, 58, 118, 0, face), BIT.square, 2],
      [sprite('button.png', 61, 0, 58, 58, 177, 0, face), BIT.triangle, 3],
    ];
    // Directional pad: only the pressed directions are drawn
    const dpad = group(71, 196);
    this.up = sprite('face.png', 46, 0, 52, 63, 0, 68, dpad);
    this.down = sprite('face.png', 46, 81, 52, 63, 54, 68, dpad);
    this.left = sprite('face.png', -1, 47, 64, 52, 108, 68, dpad);
    this.right = sprite('face.png', 81, 46, 63, 52, 175, 68, dpad);
    const sticks = group(220, 333);
    this.ls = sprite('stick.png', 0, 0, 100, 100, 0, 0, sticks);
    this.rs = sprite('stick.png', 267, 0, 100, 100, 0, 0, sticks);
    this.last = null;
    this.setSkin(skin);
    this.update(null);
  }

  /** @param {string} skin folder name of a PS5 skin, e.g. "PS5", "PS5 (black)" */
  setSkin(skin) {
    if (skin === this.skin) return;
    this.skin = skin;
    for (const s of this.sprites) s.style.backgroundImage = spriteUrl(skin, s.dataset.file);
  }

  /** @param {{buttons: number, l2: number, r2: number, lx: number, ly: number, rx: number, ry: number}|null} pad */
  update(pad) {
    const p = pad || { buttons: 0, l2: 0, r2: 0, lx: 0, ly: 0, rx: 0, ry: 0 };
    const l = this.last;
    if (l && l.buttons === p.buttons && l.l2 === p.l2 && l.r2 === p.r2 && l.lx === p.lx && l.ly === p.ly
      && l.rx === p.rx && l.ry === p.ry) return;
    this.last = { buttons: p.buttons, l2: p.l2, r2: p.r2, lx: p.lx, ly: p.ly, rx: p.rx, ry: p.ry };
    const down = (bit) => (p.buttons & bit) !== 0;
    const show = (node, on) => {
      node.style.visibility = on ? 'visible' : 'hidden';
    };
    this.lt.style.opacity = String((p.l2 || 0) / 255);
    this.rt.style.opacity = String((p.r2 || 0) / 255);
    show(this.lb, down(BIT.l1));
    show(this.rb, down(BIT.r1));
    show(this.touchpad, down(BIT.touchpad));
    show(this.meta, down(BIT.ps));
    show(this.share, down(BIT.share));
    show(this.options, down(BIT.options));
    for (const [node, bit, column] of this.face) {
      node.style.backgroundPosition = `${-column * 59}px ${down(bit) ? -59 : 0}px`;
    }
    show(this.up, down(BIT.up));
    show(this.down, down(BIT.down));
    show(this.left, down(BIT.left));
    show(this.right, down(BIT.right));
    const stick = (node, x, y, pressed) => {
      const vx = Math.max(-1, Math.min(1, (x || 0) / 32767));
      const vy = Math.max(-1, Math.min(1, (y || 0) / 32767));
      const moved = Math.sqrt(vx * vx + vy * vy) >= 0.12;
      node.style.backgroundPosition = `${pressed ? -102 : 0}px 0px`;
      node.style.transform = moved ? `translate(${vx * 22}px, ${vy * 22}px)` : '';
    };
    stick(this.ls, p.lx, p.ly, down(BIT.l3));
    stick(this.rs, p.rx, p.ry, down(BIT.r3));
  }
}

// ------------------------------------------------------------------ small pieces

/** A player's picture as a circle, or their initial on the gradient; a ring around it for "is talking". */
function avatar(size, ringWidth) {
  const a = el('div', 'hud-avatar');
  a.style.width = size + 'px';
  a.style.height = size + 'px';
  const face = el('div', 'hud-avatar-face');
  // The ring is the border; the face keeps one pixel of air to it
  const inset = ringWidth > 0 ? ringWidth + 1 : 0;
  face.style.inset = (inset - ringWidth) + 'px';
  face.style.fontSize = Math.round((size - inset * 2) * 0.46) + 'px';
  a.style.borderWidth = ringWidth + 'px';
  a.append(face);
  return {
    el: a,
    set(url, name) {
      face.style.backgroundImage = url ? `url("${url}")` : '';
      face.classList.toggle('picture', !!url);
      face.textContent = url ? '' : ((name || '?').trim().charAt(0).toUpperCase() || '?');
    },
    ring(color) {
      a.style.borderColor = color || 'transparent';
    },
  };
}

function battery() {
  const b = el('div', 'hud-battery');
  const cell = el('div', 'hud-battery-cell');
  const fill = el('div', 'hud-battery-fill');
  cell.append(fill, el('div', 'hud-battery-nub'));
  const text = el('span');
  b.append(cell, text);
  return {
    el: b,
    /** @param {number} level 0..100, 254 = on a cable, 255 = unknown */
    set(level, enabled) {
      b.hidden = !enabled || level === 255 || level === undefined;
      const cable = level > 100;
      fill.style.width = Math.max(2, 16 * (cable ? 1 : level / 100)) + 'px';
      fill.style.background = cable ? 'var(--hud-cyan)' : level <= 20 ? 'var(--hud-red)' : level <= 50 ? '#f5b942' : 'var(--hud-green)';
      text.textContent = cable ? 'cable' : level + ' %';
    },
  };
}

const playerName = (p) => p.name || `Player ${p.slot + 1}`;

// ------------------------------------------------------------------ the display

export class Hud {
  /**
   * @param {HTMLElement} root fills the game picture
   * @param {import('./presets.js').PresetSettings} settings
   */
  constructor(root, settings) {
    this.root = root;
    this.settings = settings;
    this.players = [];
    this.shared = false;
    this.exclusive = false;
    this.talking = new Set();
    this.padStates = {};
    this.camUrls = new Map();
    this.editing = false;
    this.chatOpen = false;
    this.known = null;
    this.knownReady = false;
    /** @type {function(boolean): void} */
    this.onEditingChange = null;
    /** @type {function(): void} what has to be asked of the host has changed */
    this.onWantChange = null;
    this.elements = new Map();
    this.padViews = new Map();
    this.wantedPads = false;

    root.classList.add('hud');
    this.veil = el('div', 'hud-veil');
    this.veil.hidden = true;
    this.layer = el('div', 'hud-layer');
    this.toasts = el('div', 'hud-toasts');
    this.notes = el('div', 'hud-notes');
    this.card = this.buildCard();
    root.append(this.veil, this.layer, this.toasts, this.notes, this.card);

    this.playersEl = this.element('players', 'Players');
    this.camsEl = this.element('cams', 'Webcams');
    this.padsEl = this.element('pads', "Everybody's controller");
    this.combinedEl = this.element('combined', 'What the console gets');
    this.ownPads = new Map(); // slot -> element, while every controller is placed by itself

    this.onKey = (e) => {
      if (this.editing && e.key === 'Escape') {
        e.preventDefault();
        this.setEditing(false);
      }
    };
    window.addEventListener('keydown', this.onKey);
    this.resizer = typeof ResizeObserver === 'function' ? new ResizeObserver(() => this.layout()) : null;
    if (this.resizer) this.resizer.observe(root);
    window.addEventListener('resize', () => this.layout());
    this.rebuild();
  }

  get cfg() {
    return this.settings.values;
  }

  get plain() {
    return this.cfg.style !== 'glass' && this.cfg.style !== 'solid';
  }

  get padSkin() {
    const skin = this.cfg.skin;
    return skin && skin !== 'auto' && String(skin).startsWith('PS5') ? skin : 'PS5';
  }

  get camPlayers() {
    return this.players.filter((p) => !!p.camera);
  }

  /** Whether everybody's inputs are needed right now: a guest only gets them when it asks. */
  get wantPads() {
    return this.editing || ((this.cfg.pads_show || this.cfg.combined_show) && this.players.length > 0);
  }

  // ------------------------------------------------------------ building blocks

  /** One of the things that can be shown, placed and sized. */
  element(key, title, showKey) {
    const e = {
      key,
      title,
      showKey: showKey || key + '_show',
      hasContent: false,
      available: true,
      dragX: -1,
      dragY: -1,
      outer: el('div', 'hud-el'),
      holder: el('div', 'hud-holder'),
      frame: el('div', 'hud-frame'),
      tools: el('div', 'hud-tools'),
    };
    e.titleEl = el('div', 'hud-tool-title', title);
    e.toggle = button('hud-tool', 'Hide');
    const smaller = button('hud-tool', '−');
    const larger = button('hud-tool', '+');
    e.tools.append(e.titleEl, e.toggle, smaller, larger);
    e.frame.append(e.tools);
    e.outer.append(e.holder, e.frame);
    this.layer.append(e.outer);
    this.elements.set(key, e);

    e.toggle.addEventListener('click', () => this.settings.set(e.showKey, !this.cfg[e.showKey]));
    smaller.addEventListener('click', () => this.resize(e, -10));
    larger.addEventListener('click', () => this.resize(e, 10));
    for (const b of [e.toggle, smaller, larger]) b.addEventListener('pointerdown', (ev) => ev.stopPropagation());

    // Arrange mode: a frame to grab
    let grabX = 0;
    let grabY = 0;
    let pointer = -1;
    e.frame.addEventListener('pointerdown', (ev) => {
      if (!this.editing || ev.button > 0) return;
      const r = this.root.getBoundingClientRect();
      grabX = ev.clientX - r.left - e.x;
      grabY = ev.clientY - r.top - e.y;
      pointer = ev.pointerId;
      e.frame.setPointerCapture(pointer);
      e.frame.classList.add('grabbed');
      ev.preventDefault();
    });
    e.frame.addEventListener('pointermove', (ev) => {
      if (ev.pointerId !== pointer) return;
      const r = this.root.getBoundingClientRect();
      e.dragX = Math.max(12, Math.min(r.width - e.w - 12, ev.clientX - r.left - grabX));
      e.dragY = Math.max(12, Math.min(r.height - e.h - 12, ev.clientY - r.top - grabY));
      this.place(e);
    });
    const release = (ev) => {
      if (ev.pointerId !== pointer) return;
      pointer = -1;
      e.frame.classList.remove('grabbed');
      if (e.dragX < 0) return;
      const freeX = Math.max(1, this.root.clientWidth - e.w - 24);
      const freeY = Math.max(1, this.root.clientHeight - e.h - 24);
      const x = (e.dragX - 12) / freeX;
      const y = (e.dragY - 12) / freeY;
      e.dragX = -1;
      e.dragY = -1;
      this.settings.set(e.key + '_x', x);
      this.settings.set(e.key + '_y', y);
      this.layout();
    };
    e.frame.addEventListener('pointerup', release);
    e.frame.addEventListener('pointercancel', release);
    e.frame.addEventListener('wheel', (ev) => {
      if (!this.editing) return;
      ev.preventDefault();
      this.resize(e, ev.deltaY < 0 ? 10 : -10);
    }, { passive: false });
    return e;
  }

  sizeOf(e) {
    return Math.max(0.4, Math.min(2.5, (this.cfg[e.key + '_scale'] || 100) / 100));
  }

  resize(e, step) {
    this.settings.set(e.key + '_scale', Math.max(40, Math.min(250, Math.round(this.sizeOf(e) * 100) + step)));
  }

  buildCard() {
    const card = el('div', 'hud-card');
    card.hidden = true;
    const title = el('div', 'hud-card-title', 'Arrange the display');
    this.cardText = el('div', 'hud-card-text');
    const buttons = el('div', 'hud-card-buttons');
    const reset = button('gbtn flat', 'Reset places and sizes', () => this.settings.resetPlacement());
    const done = button('gbtn highlighted', 'Done', () => this.setEditing(false));
    buttons.append(reset, done);
    card.append(title, this.cardText, buttons);
    return card;
  }

  // ------------------------------------------------------------ what is shown

  /**
   * Everybody in the session.
   * @param {object[]} players each {slot, name, host, canControl, inControl, muted, silenced, battery, avatarUrl, camera}
   * @param {boolean} exclusive not everybody controls at once, so "in control" says something
   */
  setPlayers(players, exclusive) {
    const before = this.wantPads;
    this.players = players;
    this.shared = players.length > 0;
    this.exclusive = this.shared && exclusive;
    this.notice();
    this.rebuild();
    if (before !== this.wantPads && this.onWantChange) this.onWantChange();
  }

  /** Who came and who went, as short notes; not for everybody who was already there. */
  notice() {
    if (!this.shared) {
      this.known = null;
      this.knownReady = false;
      return;
    }
    const now = new Map(this.players.map((p) => [p.slot, playerName(p)]));
    if (this.knownReady && this.cfg.notices) {
      for (const [slot, name] of now) if (!this.known.has(slot)) this.toast(`${name} joined`, true);
      for (const [slot, name] of this.known) if (!now.has(slot)) this.toast(`${name} left`, false);
    }
    this.known = now;
    if (this.players.length > 0) this.knownReady = true;
  }

  /** @param {Set<number>} talking the places of those who are heard right now */
  setTalking(talking) {
    this.talking = talking;
    this.refreshTalking();
  }

  /** @param {object} states place -> what that player presses, and "combined" for what the console gets */
  setPadStates(states) {
    this.padStates = states;
    for (const [key, view] of this.padViews) view.update(states[key] || null);
  }

  /** The newest picture of a player's webcam; an empty address takes it away. */
  setCamFrame(slot, url) {
    if (url) this.camUrls.set(slot, url);
    else this.camUrls.delete(slot);
    const img = this.camsEl.holder.querySelector(`img[data-slot="${slot}"]`);
    if (img && url) img.src = url;
  }

  /** The settings, or which preset is selected, have changed. */
  settingsChanged() {
    const before = this.wantedPads;
    this.rebuild();
    if (before !== this.wantPads && this.onWantChange) this.onWantChange();
  }

  setEditing(on) {
    if (on === this.editing) return;
    this.editing = on;
    this.root.classList.toggle('editing', on);
    this.veil.hidden = !on;
    this.card.hidden = !on;
    this.rebuild();
    if (this.onEditingChange) this.onEditingChange(on);
    if (this.onWantChange) this.onWantChange();
  }

  // ------------------------------------------------------------ the elements

  rebuild() {
    const cfg = this.cfg;
    this.wantedPads = this.wantPads;
    this.root.classList.toggle('plain', this.plain);
    this.root.classList.toggle('glass', cfg.style === 'glass');
    this.root.classList.toggle('solid', cfg.style === 'solid');
    this.padViews.clear();
    this.cardText.textContent = 'Drag an element to where you want it. The buttons above it show or hide it and '
      + `change its size (the mouse wheel does too). Everything is saved in the preset “${this.settings.presets[this.settings.active] || ''}”.`;

    this.buildPlayers();
    this.buildCams();
    this.buildPads();
    this.buildCombined();
    this.buildOwnPads();
    this.setPadStates(this.padStates);
    this.refreshTalking();
    this.layout();
  }

  buildPlayers() {
    const cfg = this.cfg;
    const e = this.playersEl;
    e.hasContent = this.players.length > 0 && (this.shared || cfg.solo);
    const grid = el('div', 'hud-players' + (cfg.vertical ? ' vertical' : ''));
    this.chips = [];
    for (const p of this.players) {
      const control = p.inControl && this.exclusive;
      const chip = el('div', 'hud-chip' + (control ? ' control' : ''));
      chip.style.opacity = this.exclusive && !p.inControl ? '0.7' : '';
      const a = avatar(34, 2);
      a.set(p.avatarUrl || '', p.name || '');
      a.el.hidden = !cfg.avatars;
      const column = el('div', 'hud-chip-text');
      const name = el('div', 'hud-chip-name', playerName(p));
      name.hidden = !cfg.names;
      const tags = el('div', 'hud-chip-tags');
      if (p.host) tags.append(el('span', 'hud-tag-host', 'HOST'));
      if (p.live) tags.append(el('span', 'hud-tag-live', 'LIVE'));
      if (this.shared && !p.canControl) tags.append(el('span', 'hud-tag-watching', 'WATCHING'));
      if (p.muted || p.silenced) tags.append(el('span', 'hud-tag-muted', 'MUTED'));
      const b = battery();
      b.set(p.battery, cfg.battery);
      tags.append(b.el);
      column.append(name, tags);
      chip.append(a.el, column, el('div', 'hud-chip-end'));
      grid.append(chip);
      this.chips.push({ slot: p.slot, avatar: a, name, control });
    }
    e.holder.replaceChildren(grid);
  }

  buildCams() {
    const cfg = this.cfg;
    const e = this.camsEl;
    const cams = this.camPlayers;
    e.hasContent = cams.length > 0;
    const row = el('div', 'hud-cams');
    this.camTiles = [];
    // In arrange mode without a webcam running: a stand-in to place
    const list = cams.length > 0 ? cams : (this.editing ? [{ slot: -1, name: 'Webcam' }] : []);
    for (const p of list) {
      const tile = el('div', 'hud-cam');
      const img = document.createElement('img');
      img.alt = '';
      img.dataset.slot = String(p.slot);
      const url = this.camUrls.get(p.slot);
      if (url) img.src = url;
      tile.append(img);
      const label = el('div', 'hud-cam-name', p.name || '');
      label.hidden = !cfg.names;
      tile.append(label);
      row.append(tile);
      this.camTiles.push({ slot: p.slot, tile });
    }
    e.holder.replaceChildren(row);
  }

  /** A controller with the player's name below it. */
  padColumn(key, name, labelClass, dim) {
    const column = el('div', 'hud-pad');
    const view = new PadView(this.padSkin, 0.2);
    view.el.style.opacity = dim ? '0.55' : '';
    this.padViews.set(key, view);
    const label = el('div', 'hud-pad-name ' + labelClass, name);
    label.hidden = !this.cfg.names;
    column.append(view.el, label);
    return column;
  }

  buildPads() {
    const e = this.padsEl;
    e.hasContent = this.players.length > 0;
    e.available = !this.cfg.pads_separate;
    const row = el('div', 'hud-pads');
    if (e.available) {
      for (const p of this.players) {
        const control = p.inControl && this.exclusive;
        row.append(this.padColumn(String(p.slot), playerName(p), control ? 'control' : '', this.exclusive && !p.inControl));
      }
    }
    e.holder.replaceChildren(row);
  }

  buildCombined() {
    const e = this.combinedEl;
    e.hasContent = this.players.length > 0;
    e.holder.replaceChildren(this.padColumn('combined', 'Console', 'console', false));
  }

  /** Each player's controller by itself, with a place and a size of its own. */
  buildOwnPads() {
    const wanted = this.cfg.pads_separate ? this.players : [];
    const slots = new Set(wanted.map((p) => p.slot));
    for (const [slot, e] of this.ownPads) {
      if (slots.has(slot)) continue;
      e.outer.remove();
      this.elements.delete(e.key);
      this.ownPads.delete(slot);
    }
    for (const p of wanted) {
      let e = this.ownPads.get(p.slot);
      if (!e) {
        e = this.element('pad' + p.slot, playerName(p), 'pads_show');
        this.ownPads.set(p.slot, e);
      }
      e.title = playerName(p);
      e.titleEl.textContent = e.title;
      e.hasContent = true;
      const control = p.inControl && this.exclusive;
      e.holder.replaceChildren(this.padColumn(String(p.slot), e.title, control ? 'control' : '', this.exclusive && !p.inControl));
    }
  }

  refreshTalking() {
    const lights = !!this.cfg.talking;
    for (const chip of this.chips || []) {
      const talking = lights && this.talking.has(chip.slot);
      chip.avatar.ring(talking ? 'var(--hud-green)' : '');
      chip.name.classList.toggle('talking', talking);
      chip.name.classList.toggle('control', !talking && this.plain && chip.control);
    }
    for (const cam of this.camTiles || []) cam.tile.classList.toggle('talking', lights && this.talking.has(cam.slot));
  }

  // ------------------------------------------------------------ places and sizes

  place(e) {
    const W = this.root.clientWidth;
    const H = this.root.clientHeight;
    const cfg = this.cfg;
    e.x = e.dragX >= 0 ? e.dragX : Math.round((cfg[e.key + '_x'] || 0) * Math.max(0, W - e.w - 24) + 12);
    e.y = e.dragY >= 0 ? e.dragY : Math.round((cfg[e.key + '_y'] || 0) * Math.max(0, H - e.h - 24) + 12);
    e.outer.style.left = e.x + 'px';
    e.outer.style.top = e.y + 'px';
    // Its switches: above the frame, or below it where there is no room above;
    // starting at the left edge, or ending at the right one near the right border
    if (this.editing) {
      const toolsWidth = e.tools.offsetWidth;
      e.tools.classList.toggle('below', e.y < 44);
      e.tools.classList.toggle('from-right', e.x + toolsWidth > W - 12);
    }
  }

  layout() {
    const cfg = this.cfg;
    for (const e of this.elements.values()) {
      const on = !!cfg[e.showKey];
      const wanted = e.available && on && e.hasContent;
      const visible = e.available && (this.editing || wanted);
      e.outer.hidden = !visible;
      if (!visible) continue;
      const size = this.sizeOf(e);
      e.holder.style.transform = `scale(${size})`;
      e.w = Math.max(this.editing ? 150 : 1, e.holder.offsetWidth * size);
      e.h = Math.max(this.editing ? 60 : 1, e.holder.offsetHeight * size);
      e.outer.style.width = e.w + 'px';
      e.outer.style.height = e.h + 'px';
      e.outer.style.opacity = String(this.editing ? (on ? 1 : 0.4) : (cfg.opacity || 100) / 100);
      e.toggle.textContent = on ? 'Hide' : 'Show';
      this.place(e);
    }
  }

  // ------------------------------------------------------------ notes

  /** "Pixel joined", "Pixel left": at the top, for a few seconds. */
  toast(text, joined) {
    const t = el('div', 'hud-toast' + (joined ? ' joined' : ''));
    t.append(el('span', 'hud-toast-dot'), el('span', '', text));
    this.toasts.append(t);
    setTimeout(() => t.remove(), 4400);
  }

  /**
   * A new chat message, bottom left, for a few seconds.
   * @param {{slot: number, name: string, text: string, embed: object|null}} message
   */
  chatMessage(message) {
    if (!this.cfg.chat || this.chatOpen || this.editing) return;
    const player = this.players.find((p) => p.slot === message.slot);
    let text = message.text || '';
    if (!text && message.embed) text = message.embed.title || message.embed.url || '';
    const name = message.name || `Player ${message.slot + 1}`;
    const note = el('div', 'hud-note');
    const a = avatar(24, 0);
    a.set(player ? (player.avatarUrl || '') : '', name);
    const body = el('div', 'hud-note-text');
    body.append(el('b', '', name), '  ' + text + (message.embed ? '  🔗' : ''));
    note.append(a.el, body);
    this.notes.append(note);
    while (this.notes.children.length > 4) this.notes.firstElementChild.remove();
    setTimeout(() => note.remove(), 7700);
  }

  /** The session is over: nothing is left standing. */
  clear() {
    this.players = [];
    this.shared = false;
    this.exclusive = false;
    this.known = null;
    this.knownReady = false;
    this.talking = new Set();
    this.padStates = {};
    this.camUrls.clear();
    this.toasts.replaceChildren();
    this.notes.replaceChildren();
    if (this.editing) this.setEditing(false);
    this.rebuild();
  }
}
