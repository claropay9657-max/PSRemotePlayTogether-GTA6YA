// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The stream menu: the connection, Play Together, sound, picture and what is
// shown on the game picture, on one sheet that slides in from the bottom. A
// port of the app's gui/src/qml/StreamMenuContent.qml and PlayTogetherBar.qml,
// with the same pieces in the same places. What a browser cannot do is left
// out (the console's microphone, the renderer's quality); what only a browser
// needs has a card of its own (the controller).

import { button, el, select, setOptions } from './dom.js';

const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));

/** A card of the bottom row: a heading in small capitals, the controls in a row below it. */
function card(title, ...controls) {
  const c = el('div', 'mcard');
  const body = el('div', 'mcard-body');
  body.append(...controls);
  c.append(el('div', 'mcard-title', title), body);
  return c;
}

/** The app's own slider: a thin track that fills with the gradient. */
function bar(min, max, step, onInput) {
  const b = el('input', 'mbar');
  b.type = 'range';
  b.min = String(min);
  b.max = String(max);
  b.step = String(step);
  // Mouse only: the buttons beside it are for the keys
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

export class StreamMenu {
  /**
   * @param {HTMLElement} root the bottom of the game picture
   * @param {object} h what the buttons do
   */
  constructor(root, h) {
    this.root = root;
    this.h = h;
    this.open = false;
    this.closing = false;
    this.players = [];
    this.talking = new Set();
    this.camUrls = new Map();
    this.popup = null;
    /** @type {function(boolean): void} the menu has opened, or is gone again */
    this.onOpenChange = null;
    this.build();
    root.addEventListener('keydown', (e) => this.onKey(e));
    root.addEventListener('transitionend', (e) => {
      if (e.target !== root || this.open) return;
      this.closing = false;
      root.hidden = true;
      if (this.onOpenChange) this.onOpenChange(false);
    });
    document.addEventListener('pointerdown', (e) => {
      if (this.popup && !this.popup.contains(e.target)) this.closePopup();
    }, true);
  }

  // ------------------------------------------------------------ building

  build() {
    const h = this.h;
    const sheet = el('div', 'menu-sheet');
    const column = el('div', 'menu-column');

    // ---------------------------------------- connection and leaving
    const top = el('div', 'menu-top');
    this.dot = el('div', 'menu-dot');
    this.where = el('div', 'menu-where');
    this.chipRate = el('div', 'mchip cyan');
    this.chipLoss = el('div', 'mchip');
    this.chipDropped = el('div', 'mchip red');
    this.chatButton = button('mbtn', 'Chat', () => h.onChat());
    this.chatBadge = el('span', 'mbadge');
    this.chatBadge.hidden = true;
    this.chatButton.append(this.chatBadge);
    this.resumeButton = button('mbtn', 'Back to game', () => this.close());
    this.leaveButton = button('mbtn tint-leave', 'Leave', () => h.onLeave());
    top.append(this.dot, this.where, this.chipRate, this.chipLoss, this.chipDropped, el('div', 'menu-space'),
      this.chatButton, this.resumeButton, this.leaveButton);

    // ---------------------------------------- Play Together
    const together = el('div', 'menu-together');
    // Says whose microphone this is
    together.append(el('div', 'mcard-title', 'PLAY TOGETHER  ·  VOICE AND WEBCAM WITH YOUR FRIENDS'));
    const strip = el('div', 'ptb');
    const own = el('div', 'ptb-own');
    const switches = el('div', 'ptb-row');
    this.voiceButton = button('bbtn', 'Join voice chat', () => h.onVoice(true));
    this.micButton = button('bbtn', 'Mic on', () => h.onMic());
    this.soundButton = button('bbtn', 'Voices on', () => h.onVoices());
    this.camButton = button('bbtn', 'Webcam off', () => h.onCam());
    this.leaveVoiceButton = button('bbtn', 'Leave voice chat', () => h.onVoice(false));
    switches.append(this.voiceButton, this.micButton, this.soundButton, this.camButton, this.leaveVoiceButton);
    // Where the host has "Players control": what this browser sends of the webcam
    this.camRow = el('div', 'ptb-row');
    this.camSize = select('mcombo', [['0', 'Small (320 × 180)'], ['1', 'Medium (480 × 270)'], ['2', 'Large (640 × 360)']],
      (value) => h.onCamSize(Number(value)));
    this.camRow.append(el('span', 'ptb-label', 'My webcam sends:'), this.camSize);
    this.note = el('div', 'ptb-note');
    own.append(switches, this.camRow, this.note);
    this.list = el('div', 'ptb-players');
    strip.append(own, this.list);
    together.append(strip);

    // ---------------------------------------- sound, picture, controller, overlays
    const controls = el('div', 'menu-controls');

    this.volume = bar(0, 100, 1, (v) => h.onVolume(v));
    this.volumeText = el('div', 'mvalue');
    this.voiceVolume = bar(0, 100, 1, (v) => h.onVoiceVolume(v));
    this.voiceVolumeText = el('div', 'mvalue');
    this.soundCard = card('SOUND',
      button('mbtn', '−', () => h.onVolume(clamp(Number(this.volume.value) - 6, 0, 100))),
      this.volume,
      button('mbtn', '+', () => h.onVolume(clamp(Number(this.volume.value) + 6, 0, 100))),
      this.volumeText,
      el('div', 'mdivider'),
      el('span', 'mlabel', 'Voices'),
      this.voiceVolume,
      this.voiceVolumeText);

    this.fitButton = button('mbtn', 'Fit', () => h.onPictureMode('fit'));
    this.zoomButton = button('mbtn', 'Zoom', () => h.onPictureMode('zoom'));
    this.stretchButton = button('mbtn', 'Stretch', () => h.onPictureMode('stretch'));
    this.zoomDivider = el('div', 'mdivider');
    this.zoomOut = button('mbtn', '−', () => h.onZoom(Number(this.zoom.value) - 0.1));
    this.zoom = bar(-1, 4, 0.01, (v) => h.onZoom(v));
    this.zoomIn = button('mbtn', '+', () => h.onZoom(Number(this.zoom.value) + 0.1));
    this.zoomText = el('div', 'mvalue wide');
    this.fullButton = button('mbtn', 'Full screen', () => h.onFullscreen());
    this.statsButton = button('mbtn', 'Stats', () => h.onStats());
    this.pictureCard = card('PICTURE', this.fitButton, this.zoomButton, this.stretchButton, this.zoomDivider,
      this.zoomOut, this.zoom, this.zoomIn, this.zoomText, el('div', 'mdivider'), this.fullButton, this.statsButton);

    this.touchButton = button('mbtn', 'On the screen', () => h.onTouch());
    this.haptics = select('mcombo', [['0', 'Rumble off'], ['0.5', 'Rumble weak'], ['1', 'Rumble normal'], ['2', 'Rumble strong']],
      (value) => h.onHaptics(Number(value)));
    this.dsButton = button('mbtn', 'DualSense…', () => h.onDualSense());
    this.keysButton = button('mbtn', 'Keyboard…', () => h.onKeys());
    this.controllerCard = card('CONTROLLER', this.touchButton, this.haptics, this.dsButton, this.keysButton);
    this.controllerNote = el('div', 'mcard-note');
    this.controllerCard.append(this.controllerNote);

    // What is shown on the game picture: switch between the saved arrangements
    this.presets = select('mcombo wide', [], (value, index) => h.onPreset(index));
    this.overlayCard = card('OVERLAYS', this.presets,
      button('mbtn', 'Arrange', () => h.onArrange()),
      button('mbtn', 'Options…', () => h.onOptions()));

    controls.append(this.soundCard, this.pictureCard, this.controllerCard, this.overlayCard);
    column.append(top, together, controls);
    sheet.append(el('div', 'menu-line'), column);
    this.root.append(sheet);
    this.root.hidden = true;
  }

  // ------------------------------------------------------------ opening and closing

  toggle() {
    if (this.open) this.close();
    else this.show();
  }

  show() {
    if (this.open) return;
    this.open = true;
    this.closing = false;
    this.root.hidden = false;
    // The next frame, so that it slides instead of jumping
    void this.root.offsetHeight;
    this.root.classList.add('open');
    this.resumeButton.focus({ preventScroll: true });
    if (this.onOpenChange) this.onOpenChange(true);
  }

  close() {
    if (!this.open) return;
    this.open = false;
    this.closing = true;
    this.closePopup();
    this.root.classList.remove('open');
    if (document.activeElement && this.root.contains(document.activeElement)) document.activeElement.blur();
    // Without a transition (a hidden tab, reduced motion) nothing would ever say it is gone
    setTimeout(() => {
      if (this.open || !this.closing) return;
      this.closing = false;
      this.root.hidden = true;
      if (this.onOpenChange) this.onOpenChange(false);
    }, 320);
  }

  /** Every control can be reached with the arrow keys; Escape goes back to the game. */
  onKey(e) {
    if (e.key === 'Escape') {
      e.preventDefault();
      e.stopPropagation();
      if (this.popup) this.closePopup();
      else this.close();
      return;
    }
    if (!['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'].includes(e.key)) return;
    const from = document.activeElement;
    if (!from || !this.root.contains(from) || from.tagName === 'INPUT') return;
    // A choice keeps up and down for itself
    if (from.tagName === 'SELECT' && (e.key === 'ArrowUp' || e.key === 'ArrowDown')) return;
    // As in the app, the players' tiles are for the mouse; the keys move between the controls
    const all = Array.from(this.root.querySelectorAll('button:not(.ptile), select')).filter((n) => n.offsetParent !== null && !n.disabled);
    const a = from.getBoundingClientRect();
    let best = null;
    let bestScore = Infinity;
    for (const n of all) {
      if (n === from) continue;
      const b = n.getBoundingClientRect();
      const dx = (b.left + b.width / 2) - (a.left + a.width / 2);
      const dy = (b.top + b.height / 2) - (a.top + a.height / 2);
      const sameRow = Math.abs(dy) < Math.max(a.height, b.height) * 0.7;
      let score = Infinity;
      if (e.key === 'ArrowRight' && sameRow && dx > 0) score = dx;
      if (e.key === 'ArrowLeft' && sameRow && dx < 0) score = -dx;
      if (e.key === 'ArrowDown' && !sameRow && dy > 0) score = dy * 4 + Math.abs(dx);
      if (e.key === 'ArrowUp' && !sameRow && dy < 0) score = -dy * 4 + Math.abs(dx);
      if (score < bestScore) {
        bestScore = score;
        best = n;
      }
    }
    if (best) {
      e.preventDefault();
      best.focus({ preventScroll: true });
    }
  }

  // ------------------------------------------------------------ what it shows

  /**
   * The connection line.
   * @param {{connected: boolean, reconnecting: string, where: string, mbps: string, loss: number, dropped: number}} c
   */
  setConnection(c) {
    const waiting = !!c.reconnecting || !c.connected;
    this.dot.classList.toggle('waiting', waiting);
    this.where.textContent = c.reconnecting ? c.reconnecting
      : c.connected ? `Playing at ${c.where}` : `Joining ${c.where}…`;
    this.chipRate.hidden = !c.connected;
    this.chipLoss.hidden = !c.connected;
    this.chipDropped.hidden = !c.connected || !(c.dropped > 0);
    this.chipRate.textContent = `${c.mbps} Mbps`;
    this.chipLoss.textContent = `${c.loss.toFixed(1)} % loss`;
    this.chipLoss.classList.toggle('red', c.loss >= 1);
    this.chipDropped.textContent = `${c.dropped} dropped`;
  }

  setUnread(count) {
    this.chatBadge.hidden = count <= 0;
    this.chatBadge.textContent = count > 99 ? '99+' : String(count);
  }

  /**
   * One's own switches.
   * @param {object} v {voice, voiceOn, micMuted, deafened, cam, camOn, camSize, pictureOff}
   */
  setVoice(v) {
    this.voiceButton.hidden = !v.voice || v.voiceOn;
    this.micButton.hidden = !v.voiceOn;
    this.soundButton.hidden = !v.voiceOn;
    this.leaveVoiceButton.hidden = !v.voiceOn;
    this.camButton.hidden = !v.cam;
    this.micButton.textContent = v.micMuted ? 'Mic muted' : 'Mic on';
    this.micButton.classList.toggle('tint-red', !!v.micMuted);
    this.soundButton.textContent = v.deafened ? 'Voices off' : 'Voices on';
    this.soundButton.classList.toggle('tint-red', !!v.deafened);
    this.camButton.textContent = v.camOn ? 'Webcam on' : 'Webcam off';
    this.camButton.classList.toggle('tint-green', !!v.camOn);
    this.camRow.hidden = !v.cam || !v.camOn;
    this.camSize.value = String(v.camSize);
    this.note.textContent = v.pictureOff ? 'Controller only: no picture from the console' : 'Click a player for more';
  }

  /**
   * Everybody, as tiles.
   * @param {object[]} players each {slot, name, you, host, web, canControl, inControl, muted, silenced, mutedHere, voice, camera, avatarUrl}
   */
  setPlayers(players) {
    this.players = players;
    this.closePopup();
    this.tiles = new Map();
    const nodes = [];
    for (const p of players) {
      const tile = button('ptile');
      tile.classList.toggle('control', !!p.inControl);
      tile.disabled = !!p.you;
      const muted = p.muted || p.silenced;
      let cam = null;
      if (p.camera) {
        cam = el('img', 'ptile-cam');
        cam.alt = '';
        const url = this.camUrls.get(p.slot);
        if (url) cam.src = url;
        tile.append(cam);
      } else {
        const face = el('div', 'ptile-face');
        if (p.avatarUrl) face.style.backgroundImage = `url("${p.avatarUrl}")`;
        else face.textContent = (p.name || '?').trim().charAt(0).toUpperCase();
        tile.append(face);
      }
      const tags = el('div', 'ptile-tags');
      if (p.host) tags.append(el('span', 'ptag host', 'HOST'));
      if (p.web) tags.append(el('span', 'ptag web', 'WEB'));
      if (!p.canControl) tags.append(el('span', 'ptag watching', 'WATCHING'));
      const foot = el('div', 'ptile-foot');
      foot.append(el('span', 'ptile-name', (p.name || `Player ${p.slot + 1}`) + (p.you ? ' (you)' : '')));
      if (muted || p.mutedHere) foot.append(el('span', 'ptag muted', p.mutedHere && !muted ? 'MUTED HERE' : 'MUTED'));
      let voice = null;
      if (p.voice && !muted && !p.mutedHere) {
        voice = el('span', 'ptile-voice');
        foot.append(voice);
      }
      tile.append(tags, foot);
      tile.addEventListener('click', (e) => {
        e.stopPropagation();
        this.playerMenu(p, tile);
      });
      nodes.push(tile);
      this.tiles.set(p.slot, { tile, cam, voice });
    }
    this.list.replaceChildren(...nodes);
    this.setTalking(this.talking);
  }

  /** @param {Set<number>} talking */
  setTalking(talking) {
    this.talking = talking;
    if (!this.tiles) return;
    for (const [slot, t] of this.tiles) {
      const on = talking.has(slot);
      t.tile.classList.toggle('talking', on);
      if (t.voice) t.voice.classList.toggle('on', on);
    }
  }

  setCamFrame(slot, url) {
    if (url) this.camUrls.set(slot, url);
    else this.camUrls.delete(slot);
    const t = this.tiles && this.tiles.get(slot);
    if (t && t.cam && url) t.cam.src = url;
  }

  /** "Mute Pixel for me": what a click on a player offers. */
  playerMenu(p, anchor) {
    this.closePopup();
    const menu = el('div', 'mpopup');
    menu.append(button('mpopup-item', p.mutedHere ? `Hear ${p.name} again` : `Mute ${p.name} for me`, () => {
      this.closePopup();
      this.h.onMutePlayer(p.slot, !p.mutedHere);
    }));
    const a = anchor.getBoundingClientRect();
    const r = this.root.getBoundingClientRect();
    this.root.append(menu);
    const left = clamp(a.left - r.left + 12, 8, r.width - menu.offsetWidth - 8);
    menu.style.left = left + 'px';
    menu.style.top = Math.max(8, a.top - r.top + a.height / 2 - menu.offsetHeight / 2) + 'px';
    this.popup = menu;
    menu.firstElementChild.focus({ preventScroll: true });
  }

  closePopup() {
    if (this.popup) this.popup.remove();
    this.popup = null;
  }

  /** @param {number} volume 0..100 @param {number} voices 0..100 */
  setSound(volume, voices, hasSound) {
    this.soundCard.hidden = !hasSound;
    this.volume.set(volume);
    this.volumeText.textContent = Math.round(volume) + ' %';
    this.voiceVolume.set(voices);
    this.voiceVolumeText.textContent = Math.round(voices) + ' %';
  }

  /** @param {'fit'|'zoom'|'stretch'} mode @param {number} zoom -1..4 */
  setPicture(mode, zoom, full, stats, hasPicture) {
    this.pictureCard.hidden = !hasPicture;
    this.fitButton.classList.toggle('selected', mode === 'fit');
    this.zoomButton.classList.toggle('selected', mode === 'zoom');
    this.stretchButton.classList.toggle('selected', mode === 'stretch');
    const zooming = mode === 'zoom';
    for (const n of [this.zoomDivider, this.zoomOut, this.zoom, this.zoomIn, this.zoomText]) n.hidden = !zooming;
    this.zoom.set(zoom);
    this.zoomText.textContent = zoom === -1 ? 'No black bars' : (zoom >= 0 ? zoom + 1 : zoom).toFixed(2) + ' ×';
    this.fullButton.classList.toggle('selected', !!full);
    this.statsButton.classList.toggle('selected', !!stats);
  }

  /** @param {object} c {touch, haptics, dualsense, dualsenseOn, note} */
  setController(c) {
    this.touchButton.classList.toggle('selected', !!c.touch);
    this.haptics.value = String(c.haptics);
    this.dsButton.hidden = !c.dualsense;
    this.dsButton.classList.toggle('selected', !!c.dualsenseOn);
    this.controllerNote.textContent = c.note || '';
    this.controllerNote.hidden = !c.note;
  }

  /** @param {string[]} names @param {number} active */
  setPresets(names, active) {
    setOptions(this.presets, names);
    this.presets.selectedIndex = active;
  }
}
