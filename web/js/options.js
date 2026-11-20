// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The options of the display on the game picture, opened from the stream menu:
// a port of the app's StreamDisplayPanel.qml with the "On the game picture"
// card of DisplayOptions.qml. The same switches with the same names; whatever
// is changed here shows on the game picture at once.

import { button, el, select, setOptions } from './dom.js';
import { PAD_SKINS } from './presets.js';

const STYLES = ['transparent', 'glass', 'solid'];

export class OverlayOptions {
  /**
   * @param {HTMLElement} root in the middle of the game picture
   * @param {import('./presets.js').PresetSettings} settings
   * @param {{onArrange: function(): void, onClose: function(): void}} h
   */
  constructor(root, settings, h) {
    this.root = root;
    this.settings = settings;
    this.h = h;
    this.toggles = [];
    /** "", "new" or "rename" */
    this.mode = '';
    this.sureTimer = 0;
    this.build();
    root.hidden = true;
    root.addEventListener('keydown', (e) => {
      if (e.key !== 'Escape') return;
      e.preventDefault();
      e.stopPropagation();
      if (this.mode) this.setMode('');
      else this.close();
    });
  }

  get isOpen() {
    return !this.root.hidden;
  }

  show() {
    this.sync();
    this.root.hidden = false;
    this.closeButton.focus({ preventScroll: true });
  }

  close() {
    if (this.root.hidden) return;
    this.setMode('');
    this.root.hidden = true;
    this.h.onClose();
  }

  // ------------------------------------------------------------ building

  toggle(key, text, parent) {
    const label = el('label', 'gswitch');
    const input = el('input');
    input.type = 'checkbox';
    input.addEventListener('change', () => this.settings.set(key, input.checked));
    const track = el('span', 'gswitch-track');
    track.append(el('span', 'gswitch-knob'));
    label.append(input, track, el('span', 'gswitch-text', text));
    parent.append(label);
    this.toggles.push({ key, input, label });
    return label;
  }

  build() {
    const settings = this.settings;
    const head = el('div', 'gpanel-head');
    this.closeButton = button('gpanel-close', '×', () => this.close());
    this.closeButton.setAttribute('aria-label', 'Close');
    head.append(el('div', 'gpanel-title', 'Overlays'), el('div', 'gpanel-note', 'Changes apply at once'), this.closeButton);

    const body = el('div', 'gpanel-body');
    const card = el('div', 'ocard');
    card.append(el('div', 'ocard-title', 'On the game picture'),
      el('div', 'ocard-sub', 'What you see on top of the game while playing. Only you see this.'));

    // Choose, add, rename and remove presets
    card.append(el('div', 'ocaption', 'Preset'));
    const presetRow = el('div', 'orow');
    this.presets = select('gcombo grow', [], (value, index) => settings.setActive(index));
    this.deleteButton = button('gbtn flat', 'Delete', () => settings.removePreset());
    this.defaultsButton = button('gbtn flat', 'Defaults', () => {
      if (this.sureTimer) {
        this.unsure();
        settings.resetAll();
      } else {
        this.defaultsButton.textContent = 'Sure?';
        this.sureTimer = setTimeout(() => this.unsure(), 3000);
      }
    });
    this.defaultsButton.title = 'Throw all presets away and start over with the built-in ones';
    presetRow.append(this.presets,
      button('gbtn flat', 'New', () => this.setMode('new')),
      button('gbtn flat', 'Rename', () => this.setMode('rename')),
      this.deleteButton, this.defaultsButton);
    this.nameRow = el('div', 'orow');
    this.nameField = el('input', 'gfield grow');
    this.nameField.type = 'text';
    this.nameField.maxLength = 40;
    this.saveButton = button('gbtn highlighted', 'Save as new', () => this.saveName());
    this.nameField.addEventListener('input', () => {
      this.saveButton.disabled = this.nameField.value.trim().length === 0;
    });
    this.nameField.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') {
        e.preventDefault();
        this.saveName();
      }
    });
    this.nameRow.append(this.nameField, this.saveButton, button('gbtn flat', 'Cancel', () => this.setMode('')));
    this.nameRow.hidden = true;
    card.append(presetRow, this.nameRow);

    card.append(el('div', 'ocaption', 'Show'));
    this.toggle('players_show', 'Players', card);
    this.toggle('cams_show', 'Webcams', card);
    this.toggle('pads_show', "Everybody's controller", card);
    this.separate = this.toggle('pads_separate', 'Place each controller by itself', card);
    this.toggle('combined_show', 'The controller the console gets', card);
    this.toggle('notices', 'A note when somebody joins or leaves', card);
    this.toggle('chat', 'New chat messages, for a few seconds', card);

    card.append(el('div', 'ocaption', 'For each player'));
    const grid = el('div', 'ogrid');
    this.toggle('avatars', 'Picture', grid);
    this.toggle('names', 'Name', grid);
    this.toggle('talking', 'Lights up when talking', grid);
    this.toggle('battery', 'Controller battery', grid);
    card.append(grid);
    this.toggle('vertical', 'List the players below each other', card);
    this.toggle('solo', 'Also when I play alone', card);

    card.append(el('div', 'ocaption', 'Look'));
    const style = el('div', 'orow');
    this.style = select('gcombo grow', ['Transparent, no boxes', 'Glass boxes', 'Dark boxes'],
      (value, index) => settings.set('style', STYLES[index]));
    style.append(el('span', 'olabel', 'Style'), this.style);
    const through = el('div', 'orow');
    this.opacity = el('input', 'gslider grow');
    this.opacity.type = 'range';
    this.opacity.min = '30';
    this.opacity.max = '100';
    this.opacity.step = '5';
    this.opacity.addEventListener('input', () => settings.set('opacity', Number(this.opacity.value)));
    this.opacityText = el('span', 'ovalue');
    through.append(el('span', 'olabel', 'See-through'), this.opacity, this.opacityText);
    const colour = el('div', 'orow');
    this.skin = select('gcombo grow', PAD_SKINS.map((s) => (s === 'PS5' ? 'White' : s.replace('PS5 (', '').replace(')', ''))),
      (value, index) => settings.set('skin', PAD_SKINS[index]));
    colour.append(el('span', 'olabel', 'Controller colour'), this.skin);
    card.append(style, through, colour);

    const buttons = el('div', 'orow buttons');
    buttons.append(button('gbtn highlighted', 'Arrange on the game picture', () => {
      this.close();
      this.h.onArrange();
    }), button('gbtn flat', 'Reset places and sizes', () => settings.resetPlacement()));
    card.append(buttons);

    body.append(card);
    this.root.append(head, body);
  }

  unsure() {
    clearTimeout(this.sureTimer);
    this.sureTimer = 0;
    this.defaultsButton.textContent = 'Defaults';
  }

  setMode(mode) {
    this.mode = mode;
    this.nameRow.hidden = mode === '';
    if (!mode) return;
    const settings = this.settings;
    this.nameField.value = mode === 'rename' ? (settings.presets[settings.active] || '') : '';
    this.nameField.placeholder = mode === 'new' ? 'Name of the new preset' : 'New name';
    this.saveButton.textContent = mode === 'new' ? 'Save as new' : 'Rename';
    this.saveButton.disabled = this.nameField.value.trim().length === 0;
    this.nameField.focus();
    if (mode === 'rename') this.nameField.select();
  }

  saveName() {
    if (this.nameField.value.trim().length === 0) return;
    if (this.mode === 'new') this.settings.addPreset(this.nameField.value);
    else this.settings.renamePreset(this.nameField.value);
    this.setMode('');
  }

  // ------------------------------------------------------------ what is set

  /** The settings, or the presets, have changed: every control says what holds now. */
  sync() {
    const settings = this.settings;
    const cfg = settings.values;
    setOptions(this.presets, settings.presets);
    this.presets.selectedIndex = settings.active;
    this.deleteButton.disabled = settings.presets.length <= 1;
    for (const t of this.toggles) t.input.checked = !!cfg[t.key];
    const separate = this.toggles.find((t) => t.key === 'pads_separate');
    separate.input.disabled = !cfg.pads_show;
    separate.label.classList.toggle('disabled', !cfg.pads_show);
    this.style.selectedIndex = Math.max(0, STYLES.indexOf(cfg.style));
    this.opacity.value = String(cfg.opacity);
    this.opacity.style.setProperty('--fill', ((cfg.opacity - 30) / 70 * 100).toFixed(1) + '%');
    this.opacityText.textContent = `${cfg.opacity} %`;
    this.skin.selectedIndex = Math.max(0, PAD_SKINS.indexOf(cfg.skin === 'auto' ? 'PS5' : cfg.skin));
  }
}
