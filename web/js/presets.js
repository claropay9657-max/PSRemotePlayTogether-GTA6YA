// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The settings of the display on top of the game picture, with presets: the
// same keys, defaults and built-in presets as in the app
// (gui/src/qmlbackend_display.cpp, gui/src/presetsettings.cpp). They are kept
// in the browser, so they are still there at the next visit.

/** One setting: its default, and whether it is a place or a size. */
function key(name, value, placement = false) {
  return { name, value, placement };
}

/** Every element has a switch, a place and a size. Places are fractions of the space left over. */
export const HUD_KEYS = (() => {
  const keys = [
    key('players_show', true),
    key('players_x', 0.0, true),
    key('players_y', 0.0, true),
    key('players_scale', 100, true),
    key('cams_show', true),
    key('cams_x', 1.0, true),
    key('cams_y', 0.0, true),
    key('cams_scale', 100, true),
    key('pads_show', false),
    key('pads_x', 0.0, true),
    key('pads_y', 1.0, true),
    key('pads_scale', 100, true),
    key('combined_show', false),
    key('combined_x', 1.0, true),
    key('combined_y', 1.0, true),
    key('combined_scale', 100, true),
    // What a player's entry shows
    key('avatars', true),
    key('names', true),
    key('talking', true),
    key('battery', true),
    key('vertical', true),
    // "auto": the colour of a white DualSense
    key('skin', 'auto'),
    // "transparent": nothing behind names and pictures; "glass": see-through boxes; "solid": dark boxes
    key('style', 'transparent'),
    key('opacity', 100),
    // Also when playing alone, without anybody else in the session
    key('solo', false),
    // "Pixel joined", "Pixel left"
    key('notices', true),
    // New chat messages for a few seconds, bottom left
    key('chat', true),
    // Everybody's controller as one row, or each one placed by itself
    key('pads_separate', false),
  ];
  // Where each player's controller goes when they are placed one by one: along the bottom
  for (let slot = 0; slot <= 8; slot++) {
    keys.push(key(`pad${slot}_x`, Math.min(1.0, slot * 0.2), true));
    keys.push(key(`pad${slot}_y`, 1.0, true));
    keys.push(key(`pad${slot}_scale`, 100, true));
  }
  return keys;
})();

export const HUD_PRESETS = [
  { name: 'Players and webcams' },
  { name: 'Everything', pads_show: true, combined_show: true },
  { name: 'Everything, in boxes', pads_show: true, combined_show: true, style: 'glass' },
  { name: 'Players in a row along the top', vertical: false, cams_show: false, players_x: 0.5 },
  { name: 'Each controller by itself', pads_show: true, pads_separate: true, combined_show: true },
  { name: 'Console controller only', players_show: false, cams_show: false, combined_show: true, solo: true },
  { name: 'Nothing', players_show: false, cams_show: false, notices: false },
];

/** The controller colours the app offers for this display, by the folder of their pictures. */
export const PAD_SKINS = [
  'PS5', 'PS5 (30th anniversary)', 'PS5 (black)', 'PS5 (chroma indigo)', 'PS5 (chroma pearl)', 'PS5 (chroma teal)',
  'PS5 (cobalt blue)', 'PS5 (cosmic red)', 'PS5 (galactic purple)', 'PS5 (gray camouflage)', 'PS5 (nova pink)',
  'PS5 (starlight blue)', 'PS5 (sterling silver)', 'PS5 (volcanic red)',
];

/**
 * A group of live settings with named presets. The selected preset follows
 * what is set: every change goes into it at once.
 */
export class PresetSettings {
  /**
   * @param {string} group the name the settings are stored under
   * @param {object[]} keys
   * @param {object[]} builtin
   */
  constructor(group, keys, builtin) {
    this.group = group;
    this.keys = keys;
    this.builtin = builtin;
    this.values = {};
    this.presetList = [];
    this.activeIndex = 0;
    /** @type {function(): void} something is set differently now */
    this.onChange = null;
    /** @type {function(): void} the list of presets, or which one is selected, has changed */
    this.onPresetsChange = null;
    const stored = this.load('values') || {};
    for (const k of keys) this.values[k.name] = this.coerce(k, k.name in stored ? stored[k.name] : k.value);
    this.loadPresets();
  }

  load(what) {
    try {
      const text = localStorage.getItem(`psrpt.${this.group}.${what}`);
      return text ? JSON.parse(text) : null;
    } catch (e) {
      return null;
    }
  }

  save(what, value) {
    try {
      localStorage.setItem(`psrpt.${this.group}.${what}`, JSON.stringify(value));
    } catch (e) {
      // then it lasts for this visit only
    }
  }

  find(name) {
    return this.keys.find((k) => k.name === name) || null;
  }

  /** Whatever arrives ends up with the type of the default. */
  coerce(k, value) {
    if (typeof k.value === 'boolean') {
      return typeof value === 'string' ? value === 'true' || value === '1' : !!value;
    }
    if (typeof k.value === 'number') {
      const v = Number(value);
      if (!Number.isFinite(v)) return k.value;
      if (Number.isInteger(k.value) && !k.name.endsWith('_x') && !k.name.endsWith('_y')) return Math.round(v);
      // Places are fractions of the free space
      if (!k.placement) return v;
      return v < 0 ? -1 : Math.max(0, Math.min(1, v));
    }
    return String(value);
  }

  snapshot() {
    const out = {};
    for (const k of this.keys) out[k.name] = this.values[k.name];
    return out;
  }

  /** The names of the presets. */
  get presets() {
    return this.presetList.map((p) => p.name);
  }

  get active() {
    return this.activeIndex;
  }

  loadPresets() {
    const list = this.load('presets');
    if (!Array.isArray(list) || !list.length) {
      // First visit: the built-in ones, the first of them with what is set right now
      this.presetList = this.builtin.map((p) => ({ ...p }));
      this.presetList[0] = { ...this.snapshot(), name: this.presetList[0].name };
      this.activeIndex = 0;
      this.savePresets();
      return;
    }
    this.presetList = list.filter((p) => p && typeof p.name === 'string');
    if (!this.presetList.length) this.presetList = this.builtin.map((p) => ({ ...p }));
    const active = Number(this.load('active'));
    this.activeIndex = Math.max(0, Math.min(this.presetList.length - 1, Number.isFinite(active) ? active : 0));
  }

  savePresets() {
    this.save('presets', this.presetList);
    this.save('active', this.activeIndex);
  }

  /** The selected preset follows what is set. */
  rememberActive() {
    if (this.activeIndex < 0 || this.activeIndex >= this.presetList.length) return;
    this.presetList[this.activeIndex] = { ...this.snapshot(), name: this.presetList[this.activeIndex].name };
    this.savePresets();
  }

  set(name, value) {
    const k = this.find(name);
    if (!k) return;
    const clean = this.coerce(k, value);
    if (this.values[name] === clean) return;
    this.values[name] = clean;
    this.save('values', this.values);
    this.rememberActive();
    if (this.onChange) this.onChange();
  }

  applyPreset(preset) {
    // A key the preset does not mention gets its default
    for (const k of this.keys) this.values[k.name] = k.name in preset ? this.coerce(k, preset[k.name]) : k.value;
    this.save('values', this.values);
    if (this.onChange) this.onChange();
  }

  setActive(index) {
    if (index < 0 || index >= this.presetList.length || index === this.activeIndex) return;
    this.activeIndex = index;
    this.save('active', index);
    this.applyPreset(this.presetList[index]);
    if (this.onPresetsChange) this.onPresetsChange();
  }

  addPreset(name) {
    let clean = name.trim().slice(0, 40);
    if (!clean) clean = `Preset ${this.presetList.length + 1}`;
    this.presetList.push({ ...this.snapshot(), name: clean });
    this.activeIndex = this.presetList.length - 1;
    this.savePresets();
    if (this.onPresetsChange) this.onPresetsChange();
  }

  renamePreset(name) {
    const clean = name.trim().slice(0, 40);
    if (!clean || this.activeIndex < 0 || this.activeIndex >= this.presetList.length) return;
    this.presetList[this.activeIndex].name = clean;
    this.savePresets();
    if (this.onPresetsChange) this.onPresetsChange();
  }

  removePreset() {
    if (this.presetList.length <= 1) return;
    this.presetList.splice(this.activeIndex, 1);
    this.activeIndex = Math.min(this.activeIndex, this.presetList.length - 1);
    this.savePresets();
    this.applyPreset(this.presetList[this.activeIndex]);
    if (this.onPresetsChange) this.onPresetsChange();
  }

  /** Every element back to where and how large it is by default. */
  resetPlacement() {
    for (const k of this.keys) if (k.placement) this.values[k.name] = k.value;
    this.save('values', this.values);
    this.rememberActive();
    if (this.onChange) this.onChange();
  }

  /** Throws all presets away and starts over with the built-in ones. */
  resetAll() {
    this.presetList = this.builtin.map((p) => ({ ...p }));
    this.activeIndex = 0;
    this.savePresets();
    this.applyPreset(this.presetList[0]);
    if (this.onPresetsChange) this.onPresetsChange();
  }
}
