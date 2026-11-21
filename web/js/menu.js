// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The stream menu, laid over the dimmed game picture: the connection and the two
// ways out along the top, Play Together in a panel at the side, and sound, picture,
// the controller and the overlays in a dock along the bottom. A port of the app's
// gui/src/qml/StreamMenuContent.qml and PlayTogetherBar.qml: the same pieces in the
// same places, with the same sizes and colours (css/game.css). What a browser cannot
// do is left out (the console's microphone, the renderer's quality, the host's own
// parts of the panel); what only a browser has sits in the dock where the app has
// those: the controller (controls on the screen, rumble, DualSense, the keyboard),
// full screen and the numbers of the connection.

import { appendWithLinks, button, el } from './dom.js';
import { BTN, CONTROLLER_XBOX } from './proto.js';

const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));

/** One colour per place in the session, the same as in the big chat. */
const SLOT_COLORS = ['#b18cff', '#00f2ff', '#34d399', '#f472b6', '#fbbf24', '#60a5fa', '#fb923c', '#a3e635', '#e879f9'];

/** How strongly the console's rumble and haptics are felt. */
const RUMBLE = [[0, 'Rumble off'], [0.5, 'Rumble weak'], [1, 'Rumble normal'], [2, 'Rumble strong']];

/** What this browser sends of its webcam, by the index the page keeps. */
const CAM_SIZES = ['Small (320 × 180)', 'Medium (480 × 270)', 'Large (640 × 360)'];

/**
 * The app's line icons (gui/res/ui), as stroke width and drawing; the last few
 * are drawn the same way for what only the page has.
 */
const ICONS = {
  mic: [2, '<rect x="9" y="3" width="6" height="11" rx="3"/><path d="M5 11a7 7 0 0 0 14 0M12 18v3"/>'],
  'mic-off': [2, '<rect x="9" y="3" width="6" height="11" rx="3"/><path d="M5 11a7 7 0 0 0 14 0M12 18v3M3 3l18 18"/>'],
  headphones: [2, '<path d="M3 18v-6a9 9 0 0 1 18 0v6"/><rect x="3" y="14" width="4" height="7" rx="1.5"/><rect x="17" y="14" width="4" height="7" rx="1.5"/>'],
  speaker: [2, '<path d="M4 9v6h4l5 4V5L8 9z"/><path d="M16.5 8.5a5 5 0 0 1 0 7M19 6a8.5 8.5 0 0 1 0 12"/>'],
  'speaker-off': [2, '<path d="M4 9v6h4l5 4V5L8 9z"/><path d="m17 9 4 6M21 9l-4 6"/>'],
  camera: [2, '<rect x="2" y="6" width="14" height="12" rx="2.5"/><path d="m16 10 6-3v10l-6-3"/>'],
  leave: [2.3, '<path d="M15 4h3a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2h-3M10 17l5-5-5-5M15 12H3"/>'],
  chat: [2, '<path d="M4 5h16a1 1 0 0 1 1 1v10a1 1 0 0 1-1 1H9l-5 4V6a1 1 0 0 1 1-1z"/>'],
  send: [2.2, '<path d="M4 12 20 4l-6 16-3-7z"/><path d="m11 13 9-9"/>'],
  minus: [3, '<path d="M5 12h14"/>'],
  plus: [3, '<path d="M12 5v14M5 12h14"/>'],
  layers: [2, '<path d="m12 3 9 5-9 5-9-5z"/><path d="m3 13 9 5 9-5"/>'],
  sliders: [2, '<path d="M4 6h9M17 6h3M4 12h3M11 12h9M4 18h11M19 18h1"/><circle cx="15" cy="6" r="2"/><circle cx="9" cy="12" r="2"/><circle cx="17" cy="18" r="2"/>'],
  'chevron-down': [2.4, '<path d="m6 9 6 6 6-6"/>'],
  check: [3.2, '<path d="M20 6 9 17l-5-5"/>'],
  gamepad: [1.9, '<path d="M7 7h10a5 5 0 0 1 4.9 6l-.8 4a2.6 2.6 0 0 1-4.6 1.1L14.8 16H9.2l-1.7 2.1A2.6 2.6 0 0 1 2.9 17l-.8-4A5 5 0 0 1 7 7z"/><path d="M8 10.5v3M6.5 12h3"/><path d="M15.5 11h.01M17.5 13h.01"/>'],
  eye: [2, '<path d="M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>'],
  fullscreen: [2, '<path d="M8 3H5a2 2 0 0 0-2 2v3M16 3h3a2 2 0 0 1 2 2v3M8 21H5a2 2 0 0 1-2-2v-3M16 21h3a2 2 0 0 0 2-2v-3"/>'],
  stats: [2, '<path d="M4 20h16M7 16v-5M12 16V6M17 16v-8"/>'],
  keyboard: [2, '<rect x="2" y="6" width="20" height="12" rx="2.5"/><path d="M6 10h.01M10 10h.01M14 10h.01M18 10h.01M7 14h10"/>'],
  touch: [2, '<rect x="6" y="2" width="12" height="20" rx="2.5"/><path d="M11 18h2"/>'],
  // Circle on a PlayStation controller: what goes back to the game
  circle: [2.4, '<circle cx="12" cy="12" r="7"/>'],
};

/** One of the line icons, in the colour of the text around it. */
function icon(name, size, cls) {
  const [width, drawing] = ICONS[name];
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('class', 'micon' + (cls ? ' ' + cls : ''));
  svg.setAttribute('width', String(size));
  svg.setAttribute('height', String(size));
  svg.setAttribute('viewBox', '0 0 24 24');
  svg.setAttribute('fill', 'none');
  svg.setAttribute('stroke', 'currentColor');
  svg.setAttribute('stroke-width', String(width));
  svg.setAttribute('stroke-linecap', 'round');
  svg.setAttribute('stroke-linejoin', 'round');
  svg.setAttribute('aria-hidden', 'true');
  svg.setAttribute('focusable', 'false');
  // A drawing of this file, never anything that came in
  svg.innerHTML = drawing;
  return svg;
}

/** A bar to drag, for the mouse: the buttons beside it are for the keys and the controller. */
function bar(min, max, step, onInput) {
  const b = el('input', 'mbar');
  b.type = 'range';
  b.min = String(min);
  b.max = String(max);
  b.step = String(step);
  b.tabIndex = -1;
  const paint = () => {
    const part = (Number(b.value) - min) / (max - min);
    b.style.setProperty('--fill', (clamp(part, 0, 1) * 100).toFixed(2) + '%');
  };
  b.addEventListener('input', () => {
    paint();
    onInput(Number(b.value));
  });
  b.set = (v) => {
    b.value = String(v);
    paint();
  };
  return b;
}

/** A player's picture as a circle, or their initial on the brand gradient. */
function face(cls, p) {
  const f = el('div', cls);
  if (p && p.avatarUrl) {
    f.style.backgroundImage = `url("${p.avatarUrl}")`;
    f.classList.add('picture');
  } else {
    f.textContent = ((p && p.name) || '?').trim().charAt(0).toUpperCase() || '?';
  }
  return f;
}

const nameOf = (p) => (p ? p.name || `Player ${p.slot + 1}` : '');

const shown = (n) => !n.hidden && n.getClientRects().length > 0;

export class StreamMenu {
  /**
   * @param {HTMLElement} root covers the game picture
   * @param {object} h what the controls do
   */
  constructor(root, h) {
    this.root = root;
    this.h = h;
    this.open = false;
    this.closing = false;
    this.players = [];
    this.talking = new Set();
    this.camUrls = new Map();
    /** slot -> {tile, cam, bars, quiet} */
    this.tiles = new Map();
    /** The last messages, newest last. */
    this.messages = [];
    /** The open list: {el, owner, where, fill, kind, slot}. */
    this.popup = null;
    this.voice = { voice: false, voiceOn: false, micMuted: false, deafened: false, cam: false, camOn: false, camSize: 1, pictureOff: false };
    this.controller = { touch: false, haptics: 1, dualsense: false, dualsenseOn: false, note: '', padKind: 0 };
    this.presets = { names: [], active: 0 };
    this.hasSound = true;
    this.hideTimer = 0;
    this.recoverTimer = 0;
    /** Where the focus was last, to find the closest control when that goes away. */
    this.lastFocus = null;
    this.layoutFrame = 0;
    /** The controller in the menu: what was held, which way it points, when that repeats. */
    this.padNav = { prev: 0, fresh: true, dir: '', next: 0 };
    /** @type {function(boolean): void} the menu has opened, or is gone again */
    this.onOpenChange = null;
    this.build();

    root.addEventListener('keydown', (e) => this.onKey(e));
    root.addEventListener('focusin', (e) => {
      if (this.popup && this.popup.el.contains(e.target)) return;
      const r = e.target.getBoundingClientRect();
      this.lastFocus = { x: r.left + r.width / 2, y: r.top + r.height / 2 };
    });
    // A click on the dimmed game or a bare part of the menu has taken the focus out of it:
    // the keys find their way back in
    window.addEventListener('keydown', (e) => {
      if (!this.open || e.defaultPrevented || (e.target !== document.body && e.target !== document.documentElement)) return;
      this.onKey(e);
    });
    // The mouse or a finger: the focus rings are for the keys and the controller
    root.addEventListener('pointerdown', () => root.classList.remove('keys'));
    this.dim.addEventListener('transitionend', (e) => {
      if (e.target === this.dim && !this.open) this.hidden();
    });
    document.addEventListener('pointerdown', (e) => {
      const p = this.popup;
      // Its own button closes it with its click
      if (p && !p.el.contains(e.target) && !p.owner.contains(e.target)) this.closePopup(false);
    }, true);
    const relayout = () => this.layoutSoon();
    window.addEventListener('resize', relayout);
    if (typeof ResizeObserver === 'function') {
      const ro = new ResizeObserver(relayout);
      for (const n of [root, this.top, this.dock]) ro.observe(n);
    }
  }

  // ------------------------------------------------------------ building

  build() {
    const h = this.h;
    const r = this.root;
    this.dim = el('div', 'menu-dim');

    // ---------------------------------------- top: the connection, and the ways out
    this.top = el('div', 'menu-top');
    this.conn = el('div', 'mconn');
    this.dot = el('span', 'mconn-dot');
    this.where = el('span', 'mconn-where');
    this.chipRate = el('span', 'mbadge play');
    this.chipLoss = el('span', 'mbadge');
    this.chipDropped = el('span', 'mbadge bad');
    this.conn.append(this.dot, this.where, this.chipRate, this.chipLoss, this.chipDropped);

    const exits = el('div', 'mexits');
    this.resumeButton = button('mresume', '', () => this.close());
    // What also goes back: Escape, or Circle on the controller
    this.resumeKey = el('span', 'mresume-key');
    this.resumeKey.setAttribute('aria-hidden', 'true');
    this.resumeKeyKind = '';
    this.resumeButton.append(el('span', '', 'Back to game'), this.resumeKey);
    this.resumeButton.setAttribute('aria-keyshortcuts', 'Escape');
    this.leaveButton = button('mleave', '', () => h.onLeave());
    this.leaveButton.append(icon('leave', 18), el('span', '', 'Leave'));
    exits.append(this.resumeButton, this.leaveButton);
    this.top.append(this.conn, exits);

    // ---------------------------------------- the side: Play Together
    this.panel = el('aside', 'mpanel');
    this.panel.setAttribute('aria-label', 'Play Together');
    this.scroller = el('div', 'mpanel-scroll');
    const column = el('div', 'mpanel-col');

    // The room
    const room = el('div', 'mroom');
    const roomHead = el('div', 'mroom-head');
    this.roomTitle = el('div', 'mroom-title', 'Play Together');
    this.roomLine = el('div', 'mroom-line');
    roomHead.append(this.roomTitle, this.roomLine);
    this.roomNote = el('div', 'mroom-note', 'Controller only: no picture from the console');
    this.roomNote.hidden = true;
    room.append(roomHead, this.roomNote);

    // Everybody who is here
    this.playersBox = el('div', 'mplayers');
    this.playersLabel = el('div', 'mlabel', 'Players');
    this.grid = el('div', 'mtiles');
    this.playersBox.append(this.playersLabel, this.grid);
    this.playersBox.hidden = true;

    // One's own microphone, voices and webcam
    this.own = el('div', 'mown');
    this.voiceButton = this.ownSwitch(() => h.onVoice(true));
    this.micButton = this.ownSwitch(() => h.onMic());
    this.soundButton = this.ownSwitch(() => h.onVoices());
    this.camButton = this.ownSwitch(() => h.onCam());
    this.leaveVoiceButton = this.ownSwitch(() => h.onVoice(false));
    this.leaveVoiceButton.setAttribute('aria-label', 'Leave voice chat');
    this.own.append(this.voiceButton, this.micButton, this.soundButton, this.camButton, this.leaveVoiceButton);

    // While the webcam runs: what this browser sends of it
    this.camRow = el('div', 'mcamsize');
    this.camSizeButton = this.dockButton({ text: CAM_SIZES[1], chevron: true });
    this.camSizeButton.addEventListener('click', () => this.openMenu(this.camSizeButton, 'below', (m) => this.camSizeMenu(m)));
    this.camSizeButton.setAttribute('aria-label', 'What my webcam sends');
    this.camRow.append(el('div', 'mlabel', 'My webcam sends'), this.camSizeButton);
    this.camRow.hidden = true;

    // The chat: the last messages, newest at the bottom, and a field to write in
    this.chat = el('div', 'mchat');
    const chatHead = el('div', 'mchat-head');
    this.unreadPill = el('span', 'munread');
    this.unreadPill.hidden = true;
    this.openBig = button('mpill', '', () => h.onChat());
    this.openBig.append(icon('chat', 15), el('span', '', 'Open big'));
    this.openBig.setAttribute('aria-label', 'Open the chat');
    chatHead.append(el('div', 'mlabel', 'Chat'), this.unreadPill, el('div', 'mspace'), this.openBig);
    this.chatList = el('div', 'mchat-list');
    this.chatEmpty = el('div', 'mchat-empty', 'No messages yet. Say hi!');
    this.chatLines = el('div', 'mchat-lines');
    this.chatList.append(this.chatEmpty, this.chatLines);
    const form = el('form', 'mchat-form');
    this.chatInput = el('input', 'mchat-input');
    this.chatInput.type = 'text';
    this.chatInput.maxLength = 400;
    this.chatInput.autocomplete = 'off';
    this.chatInput.spellcheck = false;
    this.chatInput.placeholder = 'Message everybody…';
    this.chatInput.setAttribute('aria-label', 'Message to everybody');
    this.sendButton = button('msend', '');
    this.sendButton.type = 'submit';
    this.sendButton.disabled = true;
    this.sendButton.setAttribute('aria-label', 'Send');
    this.sendButton.append(icon('send', 15));
    form.append(this.chatInput, this.sendButton);
    form.addEventListener('submit', (e) => {
      e.preventDefault();
      this.send();
    });
    this.chatInput.addEventListener('input', () => {
      this.sendButton.disabled = this.chatInput.value.trim().length === 0;
    });
    this.chat.append(el('div', 'mchat-rule'), chatHead, this.chatList, form);

    column.append(room, this.playersBox, this.own, this.camRow, this.chat);
    this.scroller.append(column);
    this.panel.append(this.scroller);
    // A player's menu stays with the player's tile
    this.scroller.addEventListener('scroll', () => {
      if (this.popup && this.popup.kind === 'player') this.placePopup();
    }, { passive: true });

    // ---------------------------------------- bottom: sound, picture, controller, overlays
    this.dock = el('div', 'mdock');

    // Sound: the game's volume
    this.soundGroup = el('div', 'mgroup tight');
    this.volume = bar(0, 100, 1, (v) => h.onVolume(v));
    this.volumeText = el('span', 'mvalue');
    this.soundGroup.append(
      this.dockButton({ icon: 'minus', tip: 'Quieter', onClick: () => h.onVolume(clamp(Number(this.volume.value) - 6, 0, 100)) }),
      this.volume,
      this.dockButton({ icon: 'plus', tip: 'Louder', onClick: () => h.onVolume(clamp(Number(this.volume.value) + 6, 0, 100)) }),
      this.volumeText);
    this.soundGroup.title = 'Game volume';

    // The voices of the voice chat, while one is in it
    this.voicesGroup = el('div', 'mgroup tight');
    this.voiceVolume = bar(0, 100, 1, (v) => h.onVoiceVolume(v));
    this.voiceVolumeText = el('span', 'mvalue');
    this.voicesGroup.append(icon('headphones', 17, 'mgroup-icon'), this.voiceVolume, this.voiceVolumeText);
    this.voicesGroup.title = 'Voices';
    this.voicesGroup.hidden = true;

    // Picture: fit, zoom or stretch
    this.pictureGroup = el('div', 'mgroup');
    this.segmented = el('div', 'mseg');
    this.segmented.setAttribute('role', 'radiogroup');
    this.segmented.setAttribute('aria-label', 'Picture');
    this.modeButtons = {};
    for (const [mode, label] of [['fit', 'Fit'], ['zoom', 'Zoom'], ['stretch', 'Stretch']]) {
      const b = button('', label, () => h.onPictureMode(mode));
      b.setAttribute('role', 'radio');
      this.modeButtons[mode] = b;
      this.segmented.append(b);
    }
    this.pictureGroup.append(this.segmented);

    // How far to zoom in, while zooming
    this.zoomGroup = el('div', 'mgroup');
    this.zoom = bar(-1, 4, 0.01, (v) => h.onZoom(v));
    this.zoomText = el('span', 'mzoom');
    this.zoomGroup.append(
      this.dockButton({ icon: 'minus', tip: 'Zoom out', onClick: () => h.onZoom(Number(this.zoom.value) - 0.1) }),
      this.zoom,
      this.dockButton({ icon: 'plus', tip: 'Zoom in', onClick: () => h.onZoom(Number(this.zoom.value) + 0.1) }),
      this.zoomText);
    this.zoomGroup.hidden = true;

    // The controller, and what is shown on the game picture
    this.padGroup = el('div', 'mgroup');
    this.controllerButton = this.dockButton({ icon: 'gamepad', text: 'Controller', chevron: true });
    this.controllerButton.addEventListener('click', () => this.openMenu(this.controllerButton, 'above', (m) => this.controllerMenu(m), { kind: 'controller' }));
    this.overlaysButton = this.dockButton({ icon: 'layers', text: 'Overlays', chevron: true });
    this.overlaysButton.addEventListener('click', () => this.openMenu(this.overlaysButton, 'above', (m) => this.overlaysMenu(m), { kind: 'overlays' }));
    this.padGroup.append(this.controllerButton, this.overlaysButton);

    // How the stream is shown on this screen
    this.moreGroup = el('div', 'mgroup');
    this.fullButton = this.dockButton({ icon: 'fullscreen', tip: 'Full screen', onClick: () => h.onFullscreen() });
    this.statsButton = this.dockButton({ icon: 'stats', tip: 'Stats on the picture', onClick: () => h.onStats() });
    this.moreGroup.append(this.fullButton, this.statsButton);

    // Thin upright lines between the parts, while the dock is on one line
    this.lines = [el('div', 'mline'), el('div', 'mline'), el('div', 'mline')];
    this.sections = [[this.soundGroup, this.voicesGroup], [this.pictureGroup, this.zoomGroup], [this.padGroup], [this.moreGroup]];
    this.dock.append(this.soundGroup, this.voicesGroup, this.lines[0], this.pictureGroup, this.zoomGroup, this.lines[1],
      this.padGroup, this.lines[2], this.moreGroup);

    r.append(this.dim, this.top, this.panel, this.dock);
    r.hidden = true;
  }

  /** A button of the dock: an icon, or a word with an icon, 36 px high. */
  dockButton({ icon: name, text, tip, chevron, onClick }) {
    const b = button('mdbtn' + (text !== undefined ? ' text' : ''), '', onClick);
    if (name) b.append(icon(name, text !== undefined ? 15 : 17));
    if (text !== undefined) {
      b.label = el('span', 'mdbtn-text', text);
      b.append(b.label);
    }
    if (chevron) {
      b.append(icon('chevron-down', 14, 'chev'));
      b.setAttribute('aria-haspopup', 'menu');
      b.setAttribute('aria-expanded', 'false');
    }
    if (tip) {
      b.title = tip;
      b.setAttribute('aria-label', tip);
    }
    return b;
  }

  /** One of one's own switches: an icon over a word. */
  ownSwitch(onClick) {
    const b = button('mown-btn', '', onClick);
    b.drawn = '';
    b.set = (name, text, tone) => {
      const key = [name, text, tone].join('|');
      if (key === b.drawn) return;
      b.drawn = key;
      b.replaceChildren(icon(name, 18), el('span', '', text));
      b.dataset.tone = tone || '';
    };
    return b;
  }

  // ------------------------------------------------------------ opening and closing

  /** @param {boolean} [byKeys] opened with the keys or the controller: the focus ring shows */
  toggle(byKeys) {
    if (this.open) this.close();
    else this.show(byKeys);
  }

  show(byKeys = false) {
    if (this.open) return;
    this.open = true;
    this.closing = false;
    clearTimeout(this.hideTimer);
    this.root.hidden = false;
    this.root.classList.toggle('keys', !!byKeys);
    this.padNav.fresh = true;
    this.lastFocus = null;
    if (this.onOpenChange) this.onOpenChange(true);
    this.layout();
    // The next frame, so that it slides instead of jumping
    void this.root.offsetHeight;
    this.root.classList.add('open');
    this.resumeButton.focus({ preventScroll: true });
  }

  close() {
    if (!this.open) return;
    this.open = false;
    this.closing = true;
    this.closePopup(false);
    this.root.classList.remove('open');
    if (document.activeElement && this.root.contains(document.activeElement)) document.activeElement.blur();
    // Without a transition (a hidden tab, reduced motion) nothing would ever say it is gone
    clearTimeout(this.hideTimer);
    this.hideTimer = setTimeout(() => this.hidden(), 260);
  }

  hidden() {
    if (this.open || !this.closing) return;
    clearTimeout(this.hideTimer);
    this.closing = false;
    this.root.hidden = true;
    if (this.onOpenChange) this.onOpenChange(false);
  }

  // ------------------------------------------------------------ where things go

  layoutSoon() {
    if (this.layoutFrame || this.root.hidden) return;
    this.layoutFrame = requestAnimationFrame(() => {
      this.layoutFrame = 0;
      this.layout();
    });
  }

  /**
   * Wide (800 px and more): the panel at the right, top bar and dock in the width left of it.
   * Narrower: the panel takes the whole width between the top bar and the dock. A window too
   * low for that (a phone on its side) has all three below each other, and the menu scrolls:
   * nothing is ever out of reach.
   */
  layout() {
    if (this.root.hidden) return;
    const r = this.root;
    const W = r.clientWidth;
    const H = r.clientHeight;
    r.classList.remove('stacked');
    r.classList.toggle('narrow', W < 800);
    this.fitConnection();
    this.panel.style.top = '';
    this.panel.style.height = '';
    this.dockLines(W < 800 ? W - 52 : W - 68 - this.panel.offsetWidth);

    let stacked = false;
    if (W < 800) {
      const top = this.top.offsetTop + this.top.offsetHeight + 12;
      const bottom = this.dock.offsetTop - 12;
      if (bottom - top < 200) stacked = true;
      else {
        this.panel.style.top = top + 'px';
        this.panel.style.height = (bottom - top) + 'px';
      }
    } else if (this.top.offsetHeight + this.dock.offsetHeight + 44 > H) {
      // The top bar and the dock would run into each other
      stacked = true;
    }
    if (stacked) {
      r.classList.add('narrow', 'stacked');
      this.fitConnection();
      this.dockLines(W - 52);
    } else {
      r.scrollTop = 0;
    }
    if (this.popup) this.placePopup();
  }

  /** The dock on one line has thin lines between its parts; wrapped, it has none. */
  dockLines(room) {
    const has = this.sections.map((s) => s.some((g) => !g.hidden));
    let width = 0;
    let items = 0;
    for (const g of this.sections.flat()) {
      if (g.hidden) continue;
      width += g.offsetWidth;
      items++;
    }
    // A line goes before a part when one is shown before it
    const wanted = this.lines.map((_, i) => has[i + 1] && has.slice(0, i + 1).some(Boolean));
    const lines = wanted.filter(Boolean).length;
    const wrapped = width + lines * 3 + (items + lines - 1) * 6 > room + 0.5;
    this.lines.forEach((l, i) => {
      l.hidden = wrapped || !wanted[i];
    });
  }

  // ------------------------------------------------------------ keys and controller
  //
  // The focus moves along one chain: Back to game, the panel from top to bottom, the dock
  // from left to right. Leave is to the right of Back to game only, so that the way out
  // is never one key away by accident. Up and Left go back along the chain, Down and Right
  // on; Return presses; Escape or Circle closes an open list, otherwise the menu.

  onKey(e) {
    if (e.key === 'Escape') {
      e.preventDefault();
      e.stopPropagation();
      this.root.classList.add('keys');
      this.back();
      return;
    }
    if (e.key === 'Tab' || e.key === 'Enter') {
      this.root.classList.add('keys');
      return;
    }
    const dir = { ArrowUp: 'up', ArrowDown: 'down', ArrowLeft: 'left', ArrowRight: 'right' }[e.key];
    if (!dir || e.altKey || e.ctrlKey || e.metaKey) return;
    if (this.move(dir)) e.preventDefault();
  }

  /** The chain the keys move along, as it is shown right now. */
  chain() {
    const pick = (box) => Array.from(box.querySelectorAll('button, input:not([type="range"])'))
      .filter((n) => !n.disabled && n.tabIndex >= 0 && shown(n));
    return [this.resumeButton, ...pick(this.panel), ...pick(this.dock)];
  }

  focus(n) {
    if (!n) return;
    n.focus({ preventScroll: true });
    this.ensureVisible(n);
  }

  /** Scrolls the panel, or the whole menu where it scrolls, so that what just got the focus can be seen. */
  ensureVisible(n) {
    const s = this.root.classList.contains('stacked') ? this.root : this.scroller;
    if (!s.contains(n) || s.scrollHeight <= s.clientHeight) return;
    const sr = s.getBoundingClientRect();
    const nr = n.getBoundingClientRect();
    const top = nr.top - sr.top + s.scrollTop - 12;
    const bottom = nr.bottom - sr.top + s.scrollTop + 12;
    if (top < s.scrollTop) s.scrollTop = Math.max(0, top);
    else if (bottom > s.scrollTop + s.clientHeight) s.scrollTop = bottom - s.clientHeight;
  }

  /** @returns {boolean} whether the key did something here */
  move(dir) {
    this.root.classList.add('keys');
    const from = document.activeElement;
    // A list is open: up and down go through it
    if (this.popup) {
      const entries = Array.from(this.popup.el.querySelectorAll('.mentry:not(:disabled)'));
      if (!entries.length || (dir !== 'up' && dir !== 'down')) return true;
      const i = entries.indexOf(from);
      const next = i < 0 ? 0 : clamp(i + (dir === 'down' ? 1 : -1), 0, entries.length - 1);
      entries[next].focus({ preventScroll: true });
      return true;
    }
    if (!from || from === document.body || !this.root.contains(from) || !shown(from)) {
      this.focus(this.closest() || this.resumeButton);
      return true;
    }
    // Writing: left and right move in the text
    if (from === this.chatInput && (dir === 'left' || dir === 'right')) return false;
    // Fit, Zoom, Stretch: left and right go from one to the next first
    if (from.parentElement === this.segmented && (dir === 'left' || dir === 'right')) {
      const beside = dir === 'left' ? from.previousElementSibling : from.nextElementSibling;
      if (beside) {
        this.focus(beside);
        return true;
      }
    }
    if (from === this.resumeButton || from === this.leaveButton) {
      if (dir === 'right' && from === this.resumeButton) this.focus(this.leaveButton);
      else if (dir === 'left' && from === this.leaveButton) this.focus(this.resumeButton);
      else if (dir === 'down') this.focus(this.chain()[1]);
      return true;
    }
    const chain = this.chain();
    const i = chain.indexOf(from);
    if (i < 0) {
      this.focus(this.resumeButton);
      return true;
    }
    const next = chain[i + (dir === 'down' || dir === 'right' ? 1 : -1)];
    if (next) this.focus(next);
    return true;
  }

  /** What is closest to where the focus was last: where it goes when that went away. */
  closest() {
    const at = this.lastFocus;
    if (!at) return null;
    let best = null;
    let bestDistance = Infinity;
    for (const n of this.chain()) {
      const r = n.getBoundingClientRect();
      const d = (r.left + r.width / 2 - at.x) ** 2 + (r.top + r.height / 2 - at.y) ** 2;
      if (d < bestDistance) {
        best = n;
        bestDistance = d;
      }
    }
    return best;
  }

  /**
   * The focus was on something that went away (a button that hid when it was pressed, a
   * player who left): it goes to whatever is closest, once the menu has caught up.
   */
  recoverSoon() {
    if (!this.open) return;
    clearTimeout(this.recoverTimer);
    this.recoverTimer = setTimeout(() => {
      if (!this.open || this.popup || !this.lastFocus) return;
      const a = document.activeElement;
      const lost = !a || a === document.body || (this.root.contains(a) && !shown(a));
      if (lost) this.focus(this.closest() || this.resumeButton);
    }, 50);
  }

  /** Escape or Circle: closes an open list, otherwise the menu. */
  back() {
    if (this.popup) this.closePopup(true, true);
    else this.close();
  }

  /** Cross: presses what has the focus. */
  activate() {
    this.root.classList.add('keys');
    const a = document.activeElement;
    if (!a || !this.root.contains(a)) this.focus(this.resumeButton);
    else if (a.tagName === 'BUTTON' && !a.disabled) a.click();
  }

  /**
   * The controller while the menu is open: the D-pad or the left stick move (and repeat
   * while held), Cross presses, Circle goes back. What is held when the menu opens does nothing.
   * @param {number} buttons @param {number} lx @param {number} ly
   */
  padInput(buttons, lx, ly) {
    const nav = this.padNav;
    const now = performance.now();
    const far = 16384;
    let dir = '';
    if ((buttons & BTN.DpadUp) || ly < -far) dir = 'up';
    else if ((buttons & BTN.DpadDown) || ly > far) dir = 'down';
    else if ((buttons & BTN.DpadLeft) || lx < -far) dir = 'left';
    else if ((buttons & BTN.DpadRight) || lx > far) dir = 'right';
    const pressed = buttons & ~nav.prev;
    nav.prev = buttons;
    if (nav.fresh) {
      nav.fresh = false;
      nav.dir = dir;
      nav.next = now + 400;
      return;
    }
    if (!dir) nav.dir = '';
    else if (dir !== nav.dir) {
      nav.dir = dir;
      nav.next = now + 400;
      this.move(dir);
    } else if (now >= nav.next) {
      nav.next = now + 110;
      this.move(dir);
    }
    if (pressed & BTN.Cross) this.activate();
    if (pressed & BTN.Circle) {
      this.root.classList.add('keys');
      this.back();
    }
  }

  // ------------------------------------------------------------ lists

  /**
   * Opens a list next to `owner` where there is room: "below" it (lined up with its
   * right edge, above where there is no room below), "above" it, or "beside" it (to the
   * right if that fits, else to the left, else below). It always stays inside.
   */
  openMenu(owner, where, fill, extra = {}) {
    const again = !!this.popup && this.popup.owner === owner;
    this.closePopup(again);
    if (again) return;
    const m = el('div', 'mmenu');
    m.setAttribute('role', 'menu');
    this.popup = { el: m, owner, where, fill, ...extra };
    fill(m);
    this.root.append(m);
    owner.classList.add('menu-open');
    if (owner.hasAttribute('aria-expanded')) owner.setAttribute('aria-expanded', 'true');
    this.placePopup();
    const first = m.querySelector('.mentry:not(:disabled)');
    if (first) first.focus({ preventScroll: true });
  }

  placePopup() {
    const p = this.popup;
    if (!p) return;
    const m = p.el;
    const r = this.root.getBoundingClientRect();
    const a = p.owner.getBoundingClientRect();
    const W = r.width;
    const H = r.height;
    const w = m.offsetWidth;
    const h = m.offsetHeight;
    const ax = a.left - r.left;
    const ay = a.top - r.top;
    let x;
    let y;
    if (p.where === 'beside') {
      y = ay;
      if (ax + a.width + 8 + w <= W - 8) x = ax + a.width + 8;
      else if (ax - 8 - w >= 8) x = ax - 8 - w;
      else {
        x = ax + (a.width - w) / 2;
        y = ay + a.height + 6;
      }
    } else {
      x = ax + a.width - w;
      y = ay + a.height + 6;
      if (p.where === 'above' || (y + h > H - 8 && ay - 6 - h >= 8)) y = ay - 6 - h;
    }
    // A menu that scrolls (stacked) carries the list along with what it belongs to
    const scrolled = this.root.scrollTop;
    m.style.left = Math.round(clamp(x, 8, W - w - 8)) + 'px';
    m.style.top = Math.round(clamp(y, 8, H - h - 8) + scrolled) + 'px';
  }

  /** Draws an open list again, with the focus where it was. */
  refreshPopup() {
    const p = this.popup;
    if (!p) return;
    const before = Array.from(p.el.querySelectorAll('.mentry')).indexOf(document.activeElement);
    p.el.replaceChildren();
    p.fill(p.el);
    this.placePopup();
    const entries = p.el.querySelectorAll('.mentry');
    if (before >= 0 && entries.length) entries[Math.min(before, entries.length - 1)].focus({ preventScroll: true });
  }

  /**
   * @param {boolean} refocus the focus goes back to the list's button
   * @param {boolean} [ring] with the focus ring (Escape, Circle)
   */
  closePopup(refocus, ring = false) {
    const p = this.popup;
    if (!p) return;
    this.popup = null;
    const hadFocus = p.el.contains(document.activeElement);
    p.el.remove();
    p.owner.classList.remove('menu-open');
    if (p.owner.hasAttribute('aria-expanded')) p.owner.setAttribute('aria-expanded', 'false');
    if (ring) this.root.classList.add('keys');
    if ((refocus || hadFocus) && this.open && p.owner.isConnected && !p.owner.disabled) {
      p.owner.focus({ preventScroll: true });
    }
  }

  /** A line of a list: an icon, the text, a tick for the one that is chosen. */
  entry(m, { icon: name, text, chosen, radio, danger, onClick }) {
    const b = button('mentry' + (chosen ? ' chosen' : '') + (danger ? ' danger' : ''), '', () => {
      this.closePopup(true);
      onClick();
    });
    b.setAttribute('role', radio ? 'menuitemradio' : 'menuitem');
    if (radio) b.setAttribute('aria-checked', chosen ? 'true' : 'false');
    if (name) b.append(icon(name, 17));
    b.append(el('span', 'mentry-text', text));
    if (chosen) b.append(icon('check', 15, 'mentry-tick'));
    // Like the app's lists: what the pointer is on is the current line, for the keys too
    b.addEventListener('pointermove', () => {
      if (document.activeElement !== b) b.focus({ preventScroll: true });
    });
    m.append(b);
    return b;
  }

  menuLine(m) {
    m.append(el('div', 'mmenu-line'));
  }

  /** What can be done with one player: for a guest, only hearing them or not. */
  playerMenu(m, slot) {
    const p = this.players.find((x) => x.slot === slot);
    if (!p) return;
    const name = nameOf(p);
    const head = el('div', 'mmenu-head');
    const text = el('div', 'mmenu-who');
    let state = p.canControl ? 'Plays along' : 'Watching';
    if (p.host) state = 'Host';
    else if (p.web) state = p.canControl ? 'From a web browser · plays along' : 'From a web browser · watching';
    text.append(el('div', 'mmenu-name', name), el('div', 'mmenu-state', state));
    head.append(face('mmenu-face', p), text);
    m.append(head);
    this.entry(m, {
      icon: p.mutedHere ? 'speaker' : 'speaker-off',
      text: p.mutedHere ? `Hear ${name} again` : `Mute ${name} for me`,
      onClick: () => this.h.onMutePlayer(slot, !p.mutedHere),
    });
  }

  openPlayerMenu(slot) {
    const t = this.tiles.get(slot);
    if (t) this.openMenu(t.tile, 'beside', (m) => this.playerMenu(m, slot), { kind: 'player', slot });
  }

  /** The controller: on the screen, how strongly it rumbles, a DualSense, the keyboard. */
  controllerMenu(m) {
    const c = this.controller;
    if (c.note) {
      m.append(el('div', 'mmenu-note', c.note));
      this.menuLine(m);
    }
    this.entry(m, { icon: 'touch', text: 'Controls on the screen', chosen: c.touch, onClick: () => this.h.onTouch() });
    this.menuLine(m);
    for (const [value, label] of RUMBLE) {
      this.entry(m, { text: label, radio: true, chosen: Number(c.haptics) === value, onClick: () => this.h.onHaptics(value) });
    }
    this.menuLine(m);
    if (c.dualsense) {
      this.entry(m, { icon: 'gamepad', text: 'DualSense…', chosen: c.dualsenseOn, onClick: () => this.h.onDualSense() });
    }
    this.entry(m, { icon: 'keyboard', text: 'Keyboard controls…', onClick: () => this.h.onKeys() });
  }

  /** What is shown on the game picture: the saved arrangements, arranging, the options. */
  overlaysMenu(m) {
    const { names, active } = this.presets;
    names.forEach((name, i) => {
      this.entry(m, { text: name, radio: true, chosen: i === active, onClick: () => this.h.onPreset(i) });
    });
    if (names.length) this.menuLine(m);
    this.entry(m, { icon: 'layers', text: 'Arrange', onClick: () => this.h.onArrange() });
    this.entry(m, { icon: 'sliders', text: 'Options…', onClick: () => this.h.onOptions() });
  }

  camSizeMenu(m) {
    CAM_SIZES.forEach((label, i) => {
      this.entry(m, { text: label, radio: true, chosen: i === this.voice.camSize, onClick: () => this.h.onCamSize(i) });
    });
  }

  // ------------------------------------------------------------ the chat

  send() {
    const text = this.chatInput.value;
    if (!text.trim()) return;
    if (this.h.onSend(text)) {
      this.chatInput.value = '';
      this.sendButton.disabled = true;
    }
  }

  chatLine(m) {
    const line = el('div', 'mchat-line');
    const name = el('b', '', m.name || `Player ${m.slot + 1}`);
    name.style.color = SLOT_COLORS[m.slot % SLOT_COLORS.length];
    line.append(name, '  ');
    if (m.text) appendWithLinks(line, m.text);
    else if (m.embed) {
      const a = el('a', '', m.embed.title || m.embed.url);
      a.href = m.embed.url;
      a.target = '_blank';
      a.rel = 'noopener noreferrer';
      line.append(a);
    }
    return line;
  }

  /** @param {{slot: number, name: string, text: string, embed: object|null}} m */
  addChat(m) {
    this.messages.push(m);
    this.chatLines.append(this.chatLine(m));
    while (this.messages.length > 30) {
      this.messages.shift();
      this.chatLines.firstElementChild.remove();
    }
    this.chatEmpty.hidden = true;
  }

  clearChat() {
    this.messages = [];
    this.chatLines.replaceChildren();
    this.chatEmpty.hidden = false;
  }

  setUnread(count) {
    this.unreadPill.hidden = count <= 0;
    this.unreadPill.textContent = count > 99 ? '99+' : String(count);
  }

  // ------------------------------------------------------------ what it shows

  /**
   * The connection line.
   * @param {{connected: boolean, reconnecting: string, where: string, mbps: string, loss: number, dropped: number}} c
   */
  setConnection(c) {
    this.dot.classList.toggle('waiting', !!c.reconnecting || !c.connected);
    this.where.textContent = c.reconnecting ? c.reconnecting
      : c.connected ? `Playing at ${c.where}` : `Joining ${c.where}…`;
    this.where.title = this.where.textContent;
    this.chipRate.hidden = !c.connected;
    this.chipLoss.hidden = !c.connected;
    this.chipDropped.hidden = !c.connected || !(c.dropped > 0);
    this.chipRate.textContent = `${c.mbps} Mbps`;
    this.chipLoss.textContent = `${c.loss.toFixed(1)} % loss`;
    this.chipLoss.classList.toggle('bad', c.loss >= 1);
    this.chipDropped.textContent = `${c.dropped} dropped`;
    if (this.open) this.fitConnection();
  }

  /**
   * A narrow window (a phone) has no room for the connection's numbers beside where one
   * plays: the numbers go, last first, rather than the words.
   */
  fitConnection() {
    const chips = [this.chipDropped, this.chipLoss, this.chipRate];
    for (const c of chips) c.classList.remove('squeezed');
    for (const c of chips) {
      if (this.where.scrollWidth <= this.where.clientWidth + 1) break;
      if (!c.hidden) c.classList.add('squeezed');
    }
  }

  /**
   * One's own switches.
   * @param {object} v {voice, voiceOn, micMuted, deafened, cam, camOn, camSize, pictureOff}
   */
  setVoice(v) {
    this.voice = v;
    this.voiceButton.hidden = !v.voice || v.voiceOn;
    this.micButton.hidden = !v.voiceOn;
    this.soundButton.hidden = !v.voiceOn;
    this.leaveVoiceButton.hidden = !v.voiceOn;
    this.camButton.hidden = !v.cam;
    this.voiceButton.set('headphones', 'Join voice chat', '');
    this.micButton.set(v.micMuted ? 'mic-off' : 'mic', v.micMuted ? 'Mic muted' : 'Mic on', v.micMuted ? 'bad' : '');
    this.soundButton.set(v.deafened ? 'speaker-off' : 'headphones', v.deafened ? 'Voices off' : 'Voices on', v.deafened ? 'bad' : '');
    this.camButton.set('camera', v.camOn ? 'Webcam on' : 'Webcam off', v.camOn ? 'good' : '');
    this.leaveVoiceButton.set('leave', 'Leave voice', 'dim');
    this.own.hidden = this.voiceButton.hidden && !v.voiceOn && this.camButton.hidden;
    this.camRow.hidden = !v.cam || !v.camOn;
    this.camSizeButton.label.textContent = CAM_SIZES[v.camSize] || CAM_SIZES[1];
    this.roomNote.hidden = !v.pictureOff;
    this.voicesGroup.hidden = !this.hasSound || !v.voiceOn;
    if (this.popup && this.popup.owner === this.camSizeButton) this.refreshPopup();
    this.layoutSoon();
    this.recoverSoon();
  }

  /**
   * Everybody, as tiles: by place, so that a tile stays (and keeps the focus) while its
   * player's state changes.
   * @param {object[]} players each {slot, name, you, web, host, canControl, inControl, muted, silenced, mutedHere, voice, camera, avatarUrl}
   */
  setPlayers(players) {
    this.players = players;
    const tiles = new Map();
    const nodes = [];
    for (const p of players) {
      const t = this.tiles.get(p.slot) || this.makeTile(p.slot);
      this.fillTile(t, p);
      tiles.set(p.slot, t);
      nodes.push(t.tile);
    }
    this.tiles = tiles;
    const now = Array.from(this.grid.children);
    if (nodes.length !== now.length || nodes.some((n, i) => n !== now[i])) {
      const had = this.grid.contains(document.activeElement) ? document.activeElement : null;
      this.grid.replaceChildren(...nodes);
      if (had && had.isConnected && !had.disabled) had.focus({ preventScroll: true });
    }
    this.playersBox.hidden = players.length === 0;
    this.playersLabel.textContent = players.length > 1 ? 'Players · click one for more' : 'Players';

    // The line under the room's name: who hosts, how many are here
    const host = players.find((p) => p.host);
    const parts = [];
    if (host && host.name) parts.push(`Hosted by ${host.name}`);
    parts.push(players.length === 1 ? '1 player' : `${players.length} players`);
    this.roomLine.textContent = players.length ? parts.join(' · ') : '';

    this.setTalking(this.talking);
    // A player's menu stays open while their state changes, and goes when they leave
    const p = this.popup;
    if (p && p.kind === 'player') {
      const t = this.tiles.get(p.slot);
      if (!t || t.you) this.closePopup(false);
      else {
        p.owner = t.tile;
        t.tile.classList.add('menu-open');
        this.refreshPopup();
      }
    }
    this.layoutSoon();
    this.recoverSoon();
  }

  makeTile(slot) {
    const tile = button('ptile');
    const t = { tile, cam: null, bars: null, quiet: null, you: false };
    tile.addEventListener('click', () => {
      if (!t.you) this.openPlayerMenu(slot);
    });
    return t;
  }

  /** A player: their webcam, picture or initial, their name and what they do. */
  fillTile(t, p) {
    const tile = t.tile;
    const muted = !!(p.muted || p.silenced);
    t.you = !!p.you;
    // One's own tile has nothing to choose
    tile.disabled = t.you;
    tile.classList.toggle('control', !!p.inControl);
    tile.setAttribute('aria-label', nameOf(p) + (t.you ? ' (you)' : ''));
    if (!t.you) tile.setAttribute('aria-haspopup', 'menu');
    else tile.removeAttribute('aria-haspopup');

    const parts = [face('ptile-face', p)];
    t.cam = null;
    if (p.camera) {
      t.cam = el('img', 'ptile-cam');
      t.cam.alt = '';
      const url = this.camUrls.get(p.slot);
      if (url) t.cam.src = url;
      parts.push(t.cam);
    }
    // Host, from a browser, only watching
    const tags = el('div', 'ptile-tags');
    if (p.host) tags.append(el('span', 'ptag host', 'HOST'));
    if (p.web) tags.append(el('span', 'ptag web', 'WEB'));
    if (!p.canControl) tags.append(el('span', 'ptag watching', 'WATCHING'));
    parts.push(tags);
    // Name and state along the bottom
    const foot = el('div', 'ptile-foot');
    foot.append(el('span', 'ptile-name', nameOf(p) + (t.you ? ' (you)' : '')));
    // Plays along, or only watches (the host always plays)
    if (!p.host) foot.append(icon(p.canControl ? 'gamepad' : 'eye', 14, p.canControl ? 'plays' : 'watches'));
    if (muted || p.mutedHere) foot.append(el('span', 'ptile-muted', p.mutedHere && !muted ? 'MUTED HERE' : 'MUTED'));
    t.bars = null;
    t.quiet = null;
    if (p.voice && !muted && !p.mutedHere) {
      t.bars = el('span', 'ptile-bars');
      t.bars.append(el('i'), el('i'), el('i'));
      // In voice chat, quiet
      t.quiet = el('span', 'ptile-quiet');
      foot.append(t.bars, t.quiet);
    }
    parts.push(foot);
    tile.replaceChildren(...parts);
  }

  /** @param {Set<number>} talking */
  setTalking(talking) {
    this.talking = talking;
    for (const [slot, t] of this.tiles) {
      const on = talking.has(slot);
      t.tile.classList.toggle('talking', on);
      if (t.bars) {
        t.bars.hidden = !on;
        t.quiet.hidden = on;
      }
    }
  }

  setCamFrame(slot, url) {
    if (url) this.camUrls.set(slot, url);
    else this.camUrls.delete(slot);
    const t = this.tiles.get(slot);
    if (t && t.cam && url) t.cam.src = url;
  }

  /** @param {number} volume 0..100 @param {number} voices 0..100 */
  setSound(volume, voices, hasSound) {
    this.hasSound = hasSound;
    this.soundGroup.hidden = !hasSound;
    this.voicesGroup.hidden = !hasSound || !this.voice.voiceOn;
    this.volume.set(volume);
    this.volumeText.textContent = Math.round(volume) + ' %';
    this.voiceVolume.set(voices);
    this.voiceVolumeText.textContent = Math.round(voices) + ' %';
    this.layoutSoon();
  }

  /** @param {'fit'|'zoom'|'stretch'} mode @param {number} zoom -1..4 */
  setPicture(mode, zoom, full, stats, hasPicture) {
    this.pictureGroup.hidden = !hasPicture;
    this.moreGroup.hidden = !hasPicture;
    for (const [m, b] of Object.entries(this.modeButtons)) {
      b.classList.toggle('chosen', m === mode);
      b.setAttribute('aria-checked', m === mode ? 'true' : 'false');
    }
    this.zoomGroup.hidden = !hasPicture || mode !== 'zoom';
    this.zoom.set(zoom);
    this.zoomText.textContent = zoom === -1 ? 'No black bars' : (zoom >= 0 ? zoom + 1 : zoom).toFixed(2) + ' ×';
    this.fullButton.classList.toggle('checked', !!full);
    this.fullButton.setAttribute('aria-pressed', full ? 'true' : 'false');
    this.fullButton.title = full ? 'Leave full screen' : 'Full screen';
    this.statsButton.classList.toggle('checked', !!stats);
    this.statsButton.setAttribute('aria-pressed', stats ? 'true' : 'false');
    this.layoutSoon();
    this.recoverSoon();
  }

  /** @param {object} c {touch, haptics, dualsense, dualsenseOn, note, padKind} */
  setController(c) {
    const before = this.controller;
    this.controller = c;
    // Back to game says what else goes back: Escape, or the controller's own button
    const kind = !c.padKind ? 'esc' : c.padKind === CONTROLLER_XBOX ? 'b' : 'circle';
    if (kind !== this.resumeKeyKind) {
      this.resumeKeyKind = kind;
      this.resumeKey.classList.toggle('glyph', kind !== 'esc');
      if (kind === 'circle') this.resumeKey.replaceChildren(icon('circle', 14));
      else this.resumeKey.textContent = kind === 'b' ? 'B' : 'Esc';
    }
    const p = this.popup;
    if (p && p.kind === 'controller' && JSON.stringify(before) !== JSON.stringify(c)) this.refreshPopup();
  }

  /** @param {string[]} names @param {number} active */
  setPresets(names, active) {
    this.presets = { names: names.slice(), active };
    if (this.popup && this.popup.kind === 'overlays') this.refreshPopup();
  }
}
