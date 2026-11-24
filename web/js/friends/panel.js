// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Friends in the page: who is online and what they play, friend requests, a friend link to add
// somebody, the chats with friends and groups, plans, and the friends settings. A panel on the
// right of the window, over the page and over the game, as the app's
// gui/src/qml/FriendsPanel.qml; the invite card in a chat is the app's InviteCard.qml, the plan
// card its PlanCard.qml, the settings its settings/FriendsPage.qml. Everything that is sealed,
// sent and kept is the client's (./client.js); this only shows it and passes on what the player does.
//
// Faces: friends switched off (what they are, and the switch), the lists (status, add a friend,
// requests, friends, chats), one chat, a sheet over the panel (a new group, a group's members,
// a plan), and the settings (the app has them on a settings page; the page has no settings
// pages, so here they are a face of the panel).
//
// What only a browser has: its keys live in this browser, which the page says the first time
// and offers the backup at once; one tab of the page at a time is this browser's device; and
// notifications while the page is closed come through ./push.js.
//
// Names, texts and everything else from other players goes in as text (textContent), never as
// HTML; the only HTML is a message's own, which ./chattext.js has escaped.

import { friendReactions, friendRunStarts, oneLine, placeFriendReaders } from '../chatmarks.js';
import { askDelete, chatInputRow, chatMessage, emojiPicker, flashMessage } from '../chatview.js';
import { button, el } from '../dom.js';
import { icon } from '../menu.js';
import { ask, group, menuUnder, pill, row, toggle } from '../setparts.js';
import { FriendsClient, MUTE_FOREVER } from './client.js';
import * as push from './push.js';
import { IndexedDbStore } from './store.js';
import * as V from './view.js';

/** Kept in localStorage while friends are on, so that the page starts them at once next time. */
const ON_KEY = 'psrpt.friends';

function remember(key, value) {
  try {
    if (value === null) localStorage.removeItem(key);
    else localStorage.setItem(key, value);
  } catch (e) {
    // not kept, then
  }
}

function recall(key) {
  try {
    return localStorage.getItem(key);
  } catch (e) {
    return null;
  }
}

/** A square button with only an icon, which says what it does (the app's C.IconButton). */
function iconButton(name, tip, onClick, small = false) {
  const b = button('icon-button' + (small ? ' small' : ''), '', onClick);
  b.append(icon(name, small ? 16 : 20));
  b.title = tip;
  b.setAttribute('aria-label', tip);
  return b;
}

/** A pill with an icon and words (the app's C.PillButton); kind "" is the ghost one. */
function pillButton(kind, text, iconName, onClick, small = true) {
  const b = button(`${kind}${small ? ' small' : ''}`.trim(), '', onClick);
  b.set = (words, name = iconName) => {
    b.replaceChildren(...(name ? [icon(name, small ? 15 : 18)] : []), el('span', '', words));
  };
  b.set(text);
  return b;
}

/** A link in a line of text (the app's C.TextLink). */
function textLink(text, onClick) {
  return button('fr-link', text, onClick);
}

/** A field with a line icon in front of it (the app's C.Field). */
function field(iconName, placeholder, maxLength) {
  const wrap = el('label', 'fr-field' + (iconName ? ' with-icon' : ''));
  if (iconName) wrap.append(icon(iconName, 18));
  const input = el('input');
  input.type = 'text';
  input.autocomplete = 'off';
  input.spellcheck = false;
  input.placeholder = placeholder;
  input.setAttribute('aria-label', placeholder);
  if (maxLength) input.maxLength = maxLength;
  wrap.append(input);
  wrap.input = input;
  return wrap;
}

/** A picture address the page itself made or a friend's checked one (client.js): nothing else goes into CSS. */
const SAFE_PICTURE = /^(data:image\/jpeg;base64,[A-Za-z0-9+/]+={0,2}|blob:[A-Za-z0-9:/._-]+)$/;

/** A round picture with the status as a dot on its edge (the app's FriendAvatar). */
function avatar(size) {
  const a = el('span', 'fr-avatar');
  a.style.setProperty('--size', `${size}px`);
  a.setAttribute('aria-hidden', 'true');
  const face = el('span', 'fr-pic');
  const dot = el('span', 'fr-dot');
  a.append(face, dot);
  a.set = (src, name, presence = '') => {
    const ok = typeof src === 'string' && SAFE_PICTURE.test(src);
    if (ok) {
      face.style.backgroundImage = `url("${src}")`;
      face.textContent = '';
    } else {
      face.style.backgroundImage = '';
      face.textContent = V.initialOf(name);
    }
    face.classList.toggle('picture', ok);
    dot.hidden = !presence;
    dot.className = `fr-dot ${V.stateTone(presence)}`;
  };
  return a;
}

/** A group's picture: the brand's colours with people on them (the app's GroupTile). */
function groupTile(size) {
  const t = el('span', 'fr-gtile');
  t.style.setProperty('--size', `${size}px`);
  t.setAttribute('aria-hidden', 'true');
  t.append(icon('people', Math.round(size * 0.5)));
  t.set = (dim) => t.classList.toggle('dim', !!dim);
  return t;
}

/** The small capital heading over a list. */
const heading = (text) => el('h3', 'fr-heading', text);

/** A window in front of everything with a title, words and buttons ([label, kind, value]); resolves to the value chosen. */
function card(title, text, buttons, extra = []) {
  return new Promise((resolve) => {
    const d = el('dialog', 'dialog ask-card fr-ask');
    const row_ = el('div', 'ask-card-buttons');
    for (const [label, kind, value] of buttons) row_.append(button(`pill ${kind}`, label, () => d.close(value)));
    d.append(el('h2', '', title), el('p', '', text), ...extra, row_);
    d.addEventListener('close', () => {
      d.remove();
      resolve(d.returnValue);
    });
    document.body.append(d);
    d.showModal();
    row_.lastElementChild.focus();
  });
}

/** Something to know, with OK (the app's info dialog). */
export const info = (title, text) => card(title, text, [['OK', 'primary', 'ok']]);

/** A picture made small for the card that friends see: a JPEG of at most 4 KB, as the app makes it. */
async function smallPicture(jpeg) {
  if (!jpeg || !jpeg.length || typeof createImageBitmap !== 'function') return null;
  let bitmap;
  try {
    bitmap = await createImageBitmap(new Blob([jpeg], { type: 'image/jpeg' }));
  } catch (e) {
    return null;
  }
  const canvas = document.createElement('canvas');
  canvas.width = 72;
  canvas.height = 72;
  const ctx = canvas.getContext('2d', { alpha: false });
  const side = Math.min(bitmap.width, bitmap.height);
  ctx.drawImage(bitmap, (bitmap.width - side) / 2, (bitmap.height - side) / 2, side, side, 0, 0, 72, 72);
  for (let quality = 0.85; quality >= 0.35; quality -= 0.1) {
    const blob = await new Promise((resolve) => canvas.toBlob(resolve, 'image/jpeg', quality));
    if (blob && blob.size <= 4096) return new Uint8Array(await blob.arrayBuffer());
  }
  return null;
}

async function copyText(text) {
  try {
    await navigator.clipboard.writeText(text);
    return true;
  } catch (e) {
    // An older way, where the clipboard is not offered
    const area = el('textarea', 'sr-only');
    area.value = text;
    document.body.append(area);
    area.select();
    let ok = false;
    try {
      ok = document.execCommand('copy');
    } catch (err) {
      ok = false;
    }
    area.remove();
    return ok;
  }
}

/**
 * An invite to a friend's session, as a chat shows it (the app's InviteCard.qml): the game's
 * key art as a soft band with the cover standing into it, the title and the game, the badges,
 * the places taken and free, whether it is open now, and Join.
 * @param {object} c what the client lets through (cleanRoom): invite, web, title, host, game, cover, console, policy, control, players, max, talk
 */
export function inviteCard(c, { mine, person, live, time, onJoin }) {
  const box = el('article', 'fr-invite' + (live === 'open' ? ' open' : ''));
  const cover = c.cover ? `${c.cover}?w=192` : '';

  // ---- the band: the key art blurred, or the brand's colours
  const band = el('div', 'fr-invite-band');
  if (cover) {
    const art = el('img', 'fr-invite-art');
    art.alt = '';
    art.decoding = 'async';
    art.src = cover;
    art.addEventListener('error', () => {
      art.remove();
      box.classList.remove('has-cover');
    });
    band.append(art);
    box.classList.add('has-cover');
  }
  band.append(icon(c.talk ? 'mic' : 'gamepad', 120, 'fr-invite-ghost'), el('div', 'fr-invite-shade'));
  const top = el('div', 'fr-invite-top');
  top.append(el('span', 'fr-invite-word', 'INVITE'), el('span', 'fr-invite-who', V.inviteWho(mine, person)));
  if (live) {
    const badge = el('span', `fr-live ${live}`);
    badge.append(el('i'), el('span', '', live === 'open' ? 'Open now' : 'Probably over'));
    top.append(badge);
  }
  band.append(top);

  // ---- the body: cover, title, game, badges, places, Join
  const body = el('div', 'fr-invite-body');
  const head = el('div', 'fr-invite-head');
  const pic = el('div', 'fr-invite-cover');
  if (c.talk) pic.append(icon('mic', 30));
  else pic.append(el('span', '', V.inviteInitial(c)));
  if (cover) {
    const img = el('img');
    img.alt = '';
    img.decoding = 'async';
    img.src = cover;
    img.addEventListener('error', () => img.remove());
    pic.append(img);
  }
  const words = el('div', 'fr-invite-words');
  words.append(el('h4', 'fr-invite-title', c.title || 'A session'));
  const line = V.inviteLine(c);
  if (line) words.append(el('p', 'fr-invite-line', line));
  head.append(pic, words);
  body.append(head);

  const badges = el('div', 'badges');
  const consoleName = V.consoleName(c.console);
  if (consoleName) badges.append(el('span', 'badge system', consoleName));
  const control = V.controlText(c);
  if (control) badges.append(el('span', 'badge play', control));
  const door = V.doorText(c);
  if (door) badges.append(el('span', 'badge door', door));
  if (c.web) badges.append(el('span', 'badge web', 'Browsers welcome'));
  if (badges.childElementCount) body.append(badges);

  // The places: taken ones filled, free ones open
  const { places, taken } = V.places(c);
  if (places > 0) {
    const meter = el('div', 'fr-places');
    const dots = el('span', 'fr-places-dots');
    dots.setAttribute('aria-hidden', 'true');
    for (let i = 0; i < places; i++) dots.append(el('i', i < taken ? 'taken' : ''));
    meter.append(dots, el('span', 'fr-places-text', V.placesText(taken, places)));
    body.append(meter);
  }

  const foot = el('div', 'fr-invite-foot' + (mine ? ' mine' : ''));
  if (!mine) {
    const join = button(live === 'old' ? 'fr-join old' : 'fr-join', '', onJoin);
    join.append(icon('enter', 18), el('span', '', live === 'old' ? 'Try to join' : 'Join'));
    if (c.title) join.setAttribute('aria-description', c.title);
    foot.append(join);
  }
  foot.append(el('span', 'fr-invite-time', time));
  body.append(foot);

  box.append(band, body);
  return box;
}

/**
 * A plan in a chat (the app's PlanCard.qml): "When shall we play?" with a few times. Each time
 * shows how many said yes and maybe, the best one so far stands out, and this player's own
 * answers can be changed at any time.
 * @param {{title: string, times: number[]}} plan
 * @param {Object<string, number[]>} votes fp -> answers (0 no, 1 maybe, 2 yes)
 */
export function planCard(plan, votes, { me, mine, person, time, people, now = Date.now(), onAnswer }) {
  const times = Array.isArray(plan.times) ? plan.times : [];
  const own = Array.isArray(votes[me]) ? votes[me] : [];
  const tally = V.planTally(votes, times.length);
  const best = V.planBest(tally);
  const box = el('article', 'fr-plan');
  const band = el('div', 'fr-plan-band');
  band.append(icon('calendar', 96, 'fr-plan-ghost'));
  const col = el('div', 'fr-plan-col');
  const top = el('div', 'fr-plan-top');
  top.append(el('span', 'fr-plan-word', 'PLAN'), el('span', 'fr-plan-who', mine ? 'You asked' : `${person} asks`),
    el('span', 'fr-plan-time', time));
  col.append(top, el('h4', 'fr-plan-title', V.planTitle(plan)));
  times.forEach((at, index) => {
    const [yes, maybe] = tally[index];
    const slot = el('div', 'fr-slot' + (best === index ? ' best' : ''));
    const head = el('div', 'fr-slot-head');
    const when = el('div', 'fr-slot-when');
    when.append(el('small', '', V.dayText(at, now)), el('b', '', V.hourText(at)));
    head.append(when);
    if (best === index) head.append(el('span', 'badge good dot', 'Best so far'));
    // How many can: yes filled, maybe lighter, of everybody
    const meter = el('div', 'fr-slot-meter');
    const bar = el('span', 'fr-slot-bar');
    const maybeBar = el('i', 'maybe');
    const yesBar = el('i', 'yes');
    maybeBar.style.width = `${Math.min(1, (yes + maybe) / Math.max(1, people)) * 100}%`;
    yesBar.style.width = `${Math.min(1, yes / Math.max(1, people)) * 100}%`;
    bar.append(maybeBar, yesBar);
    meter.append(bar, el('small', '', V.planCount(yes, maybe)));
    // The own answer
    const answers = el('div', 'fr-slot-answers');
    for (const [value, label, tone] of [[2, 'Yes', 'good'], [1, 'Maybe', 'wait'], [0, 'No', 'bad']]) {
      const chosen = own.length > index && (own[index] || 0) === value;
      const b = button(`fr-answer ${tone}${chosen ? ' chosen' : ''}`, label, () => {
        const a = [];
        for (let i = 0; i < times.length; i++) a.push(i === index ? value : own[i] || 0);
        onAnswer(a);
      });
      b.setAttribute('aria-pressed', chosen ? 'true' : 'false');
      b.dataset.key = `answer ${index} ${value}`;
      answers.append(b);
    }
    slot.append(head, meter, answers);
    col.append(slot);
  });
  box.append(band, col);
  return box;
}

export class FriendsPanel {
  /**
   * @param {object} o
   * @param {string} [o.server] a friends server of one's own, for trying things out on this machine
   * @param {(card: object, name: string) => void} o.onJoin joins a friend's session
   * @param {(news: number, online: number) => void} [o.onCount] what the Friends buttons show changed
   * @param {(kind: string, detail: object) => void} [o.onEvent] a request, a message or a friend hosting, while the panel is closed
   * @param {() => void} [o.onClose] the panel has closed
   */
  constructor(o) {
    this.o = o;
    this.fr = new FriendsClient();
    this.started = null;
    this.isOpen = false;
    /** "main" (off, lists or a chat) or "settings" */
    this.face = 'main';
    this.chat = '';
    this.tab = 0;
    /** A sheet over the panel: "" none, "newgroup", "members" (of the open group), "plan" (for the open chat). */
    this.sheet = '';
    /** Friends picked on a sheet, and the times of a plan being made. */
    this.picked = [];
    this.planTimes = [];
    this.looked = null;
    this.lookError = '';
    this.lookSeq = 0;
    this.lookTimer = 0;
    this.copiedTimer = 0;
    this.pendingLink = '';
    this.profile = { name: '', avatarUrl: '', small: null, ready: Promise.resolve() };
    this.activity = '';
    this.drawn = {};
    this.pushOn = false;
    this.pushProblem = '';
    this.pushBusy = false;
    this.serverEditing = false;
    this.returnFocus = null;
    /** The window for linking a device while it is open. */
    this.linkCard = null;
    this.build();

    this.fr.addEventListener('change', () => this.changed());
    this.fr.addEventListener('request', (e) => this.event('request', e.detail));
    this.fr.addEventListener('message', (e) => this.event('message', e.detail));
    this.fr.addEventListener('hosting', (e) => this.event('hosting', e.detail));
    document.addEventListener('visibilitychange', () => {
      if (this.isOpen) this.render();
    });
    // A click on a notification while the page is open
    push.listen(() => this.show());
    // Friends that were on last time start at once: the button shows what is new
    if (recall(ON_KEY) === '1') this.start();
  }

  // ------------------------------------------------------------ outside

  /** The client and its store, once: the store is opened only when friends are wanted. */
  start() {
    this.started ||= (async () => {
      await this.fr.setStorage(new IndexedDbStore());
      if (this.o.server) await this.fr.setServer(this.o.server);
      await this.profile.ready;
      await this.fr.setProfile(this.profile.name, this.profile.small);
      if (this.activity) await this.fr.setActivity(this.activity);
      this.pushOn = this.fr.enabled && (await push.isOn());
      if (this.pushOn) push.refresh(this.fr);
      this.changed();
    })();
    return this.started;
  }

  /** What friends see of this player: the profile's name and picture (JPEG bytes). */
  setProfile(name, jpeg) {
    const n = String(name || '').trim().slice(0, 32);
    if (this.profile.avatarUrl) URL.revokeObjectURL(this.profile.avatarUrl);
    this.profile.name = n;
    this.profile.avatarUrl = jpeg && jpeg.length ? URL.createObjectURL(new Blob([jpeg], { type: 'image/jpeg' })) : '';
    this.profile.ready = (async () => {
      this.profile.small = await smallPicture(jpeg);
      if (this.started) await this.fr.setProfile(this.profile.name, this.profile.small);
    })();
    if (this.isOpen) this.render();
  }

  /** What this browser is doing, for the status ("Playing … with friends"); empty when nothing. */
  setActivity(text) {
    this.activity = text || '';
    if (this.started) this.fr.setActivity(this.activity);
  }

  get news() {
    return V.newsCount(this.fr);
  }

  get online() {
    return V.onlineCount(this.fr);
  }

  /** Opens the panel at the lists, or at a chat (a friend's fingerprint or a group's id). */
  async show(chat = '') {
    this.start();
    this.face = 'main';
    this.sheet = '';
    this.chat = chat || '';
    if (this.chat) this.tab = 1;
    this.open();
    await this.started;
    if (this.fr.enabled) this.fr.refresh();
    this.render();
    // The friends were read meanwhile: another face than the one opened with
    if (this.isOpen && !this.currentFace().contains(document.activeElement)) this.focusFirst();
  }

  /** Opens the panel with a friend link to look up (a link opened in the page, or pasted). */
  async showLink(link) {
    this.pendingLink = link;
    await this.show();
    this.takePendingLink();
  }

  takePendingLink() {
    if (!this.pendingLink || !this.fr.enabled) return;
    this.tab = 0;
    this.chat = '';
    this.sheet = '';
    this.face = 'main';
    this.linkField.input.value = this.pendingLink;
    this.pendingLink = '';
    this.render();
    this.lookSoon(0);
    this.linkField.input.focus();
  }

  open() {
    if (this.isOpen) return;
    this.isOpen = true;
    this.returnFocus = document.activeElement;
    this.root.hidden = false;
    // Everything behind it is out of reach while it is open, as behind the app's popup
    for (const id of ['front', 'stage']) {
      const n = document.getElementById(id);
      if (n) n.inert = true;
    }
    requestAnimationFrame(() => this.root.classList.add('shown'));
    this.nowTimer = setInterval(() => this.render(), 60000);
    this.render();
    this.focusFirst();
  }

  close() {
    if (!this.isOpen) return;
    this.isOpen = false;
    clearInterval(this.nowTimer);
    this.root.classList.remove('shown');
    this.root.hidden = true;
    for (const id of ['front', 'stage']) {
      const n = document.getElementById(id);
      if (n) n.inert = false;
    }
    if (this.returnFocus && this.returnFocus.isConnected) this.returnFocus.focus({ preventScroll: true });
    this.returnFocus = null;
    if (this.o.onClose) this.o.onClose();
  }

  event(kind, detail) {
    // In front already: the panel shows it
    if (this.isOpen && document.visibilityState === 'visible') return;
    if (this.o.onEvent) this.o.onEvent(kind, detail);
  }

  changed() {
    remember(ON_KEY, this.fr.enabled ? '1' : null);
    if (this.fr.enabled && this.pendingLink && this.isOpen) this.takePendingLink();
    if (this.o.onCount) this.o.onCount(this.news, this.online);
    if (this.isOpen) this.render();
    if (this.linkCard) this.linkCard.update();
  }

  /** Where the keys start on a face: what one does there first. */
  focusFirst() {
    const face = this.currentFace();
    let target = face === this.offFace ? this.onButton
      : face === this.listsFace ? this.tabButtons[this.tab]
        : face === this.chatFace && !this.chatForm.hidden ? this.messageInput
          : face === this.sheetFace && this.sheet !== 'members'
            ? [...face.querySelectorAll('input[type="text"]')].find((n) => n.getClientRects().length > 0) || null : null;
    if (!target || target.disabled) target = face.querySelector('button:not(:disabled)');
    if (target) target.focus({ preventScroll: true });
  }

  // ------------------------------------------------------------ building

  build() {
    this.root = el('div', 'friends');
    this.root.hidden = true;
    const veil = el('div', 'fr-veil');
    veil.addEventListener('click', () => this.close());
    this.panel = el('aside', 'fr-panel');
    this.panel.setAttribute('role', 'dialog');
    this.panel.setAttribute('aria-modal', 'true');
    this.panel.setAttribute('aria-label', 'Friends');
    // On the document: a row that was focused may have gone with what it showed
    document.addEventListener('keydown', (e) => {
      if (this.isOpen) this.onKey(e);
    });
    this.offFace = this.buildOff();
    this.listsFace = this.buildLists();
    this.chatFace = this.buildChat();
    this.sheetFace = this.buildSheet();
    this.setFace = this.buildSettings();
    this.panel.append(this.offFace, this.listsFace, this.chatFace, this.sheetFace, this.setFace);
    this.root.append(veil, this.panel);
    document.body.append(this.root);
  }

  onKey(e) {
    // A question or a passphrase in front: it answers its own keys
    if (document.querySelector('dialog[open]')) return;
    if (e.key === 'Escape' && !e.defaultPrevented) {
      e.preventDefault();
      e.stopPropagation();
      // An emoji picker of the chat first
      if (this.closeChatPickers()) return;
      this.close();
      return;
    }
    // The keys stay in the panel while it is open
    if (e.key === 'Tab') {
      const items = Array.from(this.currentFace().querySelectorAll('button, input, select, a[href]'))
        .filter((n) => !n.disabled && n.getClientRects().length > 0 && !n.closest('[inert]'));
      if (!items.length) return;
      const first = items[0];
      const last = items[items.length - 1];
      if (!this.panel.contains(document.activeElement)) {
        e.preventDefault();
        (e.shiftKey ? last : first).focus();
      } else if (e.shiftKey && document.activeElement === first) {
        e.preventDefault();
        last.focus();
      } else if (!e.shiftKey && document.activeElement === last) {
        e.preventDefault();
        first.focus();
      }
    }
  }

  titleRow(back) {
    const r = el('div', 'fr-title-row');
    if (back) r.append(iconButton('back', 'Back to the lists', back));
    r.append(el('h2', 'fr-title', back ? 'Friends & chats' : 'Friends'));
    return r;
  }

  // ---------------------------------------------------- switched off

  buildOff() {
    const f = el('section', 'fr-face fr-off');
    const scroll = el('div', 'fr-scroll');
    const col = el('div', 'fr-off-col');
    const top = this.titleRow(null);
    top.append(iconButton('close', 'Close', () => this.close()));
    const words = el('div', 'fr-off-words');
    words.append(icon('people', 56, 'fr-off-icon'),
      el('h3', 'fr-off-title', 'See who plays, chat, agree on a time'),
      el('p', 'fr-off-text', 'Your friends see whether you are online and what you play, and join your sessions with one click. Chats work outside of sessions too.'));
    const privacy = el('div', 'fr-card fr-private');
    const ph = el('div', 'fr-private-head');
    ph.append(icon('lock', 16), el('b', '', 'Private by design'));
    privacy.append(ph,
      el('p', '', 'No account, no e-mail address. Chats and statuses are end-to-end encrypted: the friends server only passes on sealed notes it cannot read. Your keys and chats stay on your devices.'),
      // What a browser cannot promise (docs/friends.md, "Web only")
      el('p', 'fr-private-web', 'In a web browser, your keys are only as safe as the page that is loaded. The app is the safer place for players who want the most.'));
    this.offProblem = el('p', 'fr-problem');
    this.onButton = pillButton('brand', 'Switch on friends', 'people', () => this.switchOn(), false);
    this.onButton.classList.add('fr-on-button');
    const restore = textLink('From a backup or my other device…', () => this.openSettings());
    col.append(top, words, privacy, this.offProblem, this.onButton, restore,
      el('p', 'fr-off-foot', 'Friends see the name and picture of your profile.'));
    scroll.append(col);
    f.append(scroll);
    return f;
  }

  // ---------------------------------------------------- the lists

  buildLists() {
    const f = el('section', 'fr-face fr-lists');
    const head = el('div', 'fr-lists-head');
    const top = this.titleRow(null);
    top.append(iconButton('sliders', 'Friends settings', () => this.openSettings()),
      iconButton('close', 'Close', () => this.close()));

    // Who one is, and one's own status
    const own = el('div', 'fr-own');
    this.ownAvatar = avatar(36);
    this.ownName = el('div', 'fr-own-name');
    this.stateSelect = el('select', 'fr-state');
    this.stateSelect.setAttribute('aria-label', 'Your status');
    for (const [value, label] of [['online', 'Online'], ['away', 'Away'], ['busy', 'Busy'], ['invisible', 'Invisible']]) {
      const option = el('option', '', label);
      option.value = value;
      this.stateSelect.append(option);
    }
    this.stateSelect.addEventListener('change', () => {
      this.fr.myState = this.stateSelect.value;
    });
    own.append(this.ownAvatar, this.ownName, this.stateSelect);

    this.freeField = field('clock', 'When you\'re free, e.g. “tonight from 8”', 80);
    const commitFree = () => {
      this.fr.myFree = this.freeField.input.value.trim();
    };
    this.freeField.input.addEventListener('change', commitFree);
    this.freeField.input.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') commitFree();
    });

    // The server cannot be reached: what is shown is from the last time
    this.offline = el('p', 'fr-offline', 'The friends server cannot be reached right now. Messages are sent once it answers again.');

    this.tabs = el('div', 'fr-tabs');
    this.tabs.setAttribute('role', 'tablist');
    this.tabButtons = [0, 1].map((index) => {
      const b = button('', '', () => {
        this.tab = index;
        this.render();
      });
      b.setAttribute('role', 'tab');
      b.addEventListener('keydown', (e) => {
        const to = e.key === 'ArrowLeft' ? 0 : e.key === 'ArrowRight' ? 1 : -1;
        if (to < 0 || to === index) return;
        e.preventDefault();
        this.tab = to;
        this.render();
        this.tabButtons[to].focus();
      });
      return b;
    });
    this.tabs.append(...this.tabButtons);
    head.append(top, own, this.freeField, this.offline, this.tabs);

    // ---- friends: add, requests, the list
    this.listScroll = el('div', 'fr-scroll');
    this.friendsTab = el('div', 'fr-tab');
    this.friendsTab.append(this.buildAdd());
    this.requestsHead = heading('REQUESTS');
    this.requestsList = el('div', 'fr-list');
    this.requestsList.dataset.name = 'requests';
    this.friendsHead = heading('FRIENDS');
    this.noFriends = el('p', 'fr-empty', 'No friends yet. Send your friend link to somebody, or paste theirs above.');
    this.friendsList = el('div', 'fr-list');
    this.friendsList.dataset.name = 'friends';
    this.friendsTab.append(this.requestsHead, this.requestsList, this.friendsHead, this.noFriends, this.friendsList);

    // ---- chats, and a new group
    this.chatsTab = el('div', 'fr-tab');
    this.newGroupButton = pillButton('', 'New group', 'plus', () => this.openSheet('newgroup'));
    this.newGroupButton.classList.add('fr-new-group');
    this.noChats = el('p', 'fr-empty', 'No chats yet. Open a friend to write to them.');
    this.chatsList = el('div', 'fr-list tight');
    this.chatsList.dataset.name = 'chats';
    this.chatsTab.append(this.newGroupButton, this.noChats, this.chatsList);

    this.listScroll.append(this.friendsTab, this.chatsTab);
    f.append(head, this.listScroll);
    return f;
  }

  /** Add a friend with their link; one's own link to give out. */
  buildAdd() {
    const c = el('div', 'fr-card fr-add');
    this.linkField = field('link', 'Paste their friend link');
    this.linkField.input.addEventListener('input', () => this.lookSoon(350));
    this.lookErrorLine = el('p', 'fr-error');
    this.lookedRow = el('div', 'fr-looked');
    this.lookedAvatar = avatar(36);
    this.lookedName = el('b', '');
    this.lookedRow.append(this.lookedAvatar, this.lookedName);
    this.noteField = field(null, 'A line for them (optional): who you are', 200);
    this.alreadyLine = el('p', 'fr-already');
    this.requestButton = pillButton('primary', 'Send friend request', 'plus', () => {
      this.fr.sendRequest(this.linkField.input.value.trim(), this.noteField.input.value.trim());
      this.linkField.input.value = '';
      this.noteField.input.value = '';
      this.looked = null;
      this.lookError = '';
      this.render();
    });
    this.requestButton.classList.add('fr-request-button');

    const mine = el('div', 'fr-mylink');
    const words = el('div', 'fr-mylink-words');
    this.linkLabel = el('b', '', 'Your friend link');
    words.append(this.linkLabel, el('small', '', 'Give it to the people you want as friends. Whoever has it can send you a request.'));
    const anchor = el('span', 'fr-anchor');
    const copy = pillButton('', 'Copy', 'copy', () => menuUnder(copy, [
      ['For the app', () => this.copy(this.fr.link)],
      ['For a web browser', () => this.copy(this.fr.webLink)],
    ]));
    copy.setAttribute('aria-haspopup', 'menu');
    anchor.append(copy);
    mine.append(words, anchor);
    c.append(el('div', 'fr-card-title', 'Add a friend'), this.linkField, this.lookErrorLine, this.lookedRow, this.noteField,
      this.alreadyLine, this.requestButton, el('div', 'fr-rule'), mine);
    return c;
  }

  async copy(text) {
    if (!text || !(await copyText(text))) return;
    clearTimeout(this.copiedTimer);
    this.linkLabel.textContent = 'Copied';
    this.linkLabel.classList.add('copied');
    this.copiedTimer = setTimeout(() => {
      this.linkLabel.textContent = 'Your friend link';
      this.linkLabel.classList.remove('copied');
    }, 1800);
  }

  /** Looks a pasted link up a moment after typing stops. */
  lookSoon(ms) {
    clearTimeout(this.lookTimer);
    this.lookTimer = setTimeout(async () => {
      const text = this.linkField.input.value.trim();
      const seq = ++this.lookSeq;
      if (!text) {
        this.looked = null;
        this.lookError = '';
        this.render();
        return;
      }
      const r = await this.fr.lookUp(text);
      if (seq !== this.lookSeq || !r) return;
      this.looked = r.ok ? { fp: r.fp, name: r.name || 'Somebody without a name', avatar: r.avatar } : null;
      this.lookError = r.ok ? '' : r.error;
      this.render();
    }, ms);
  }

  // ---------------------------------------------------- one chat

  buildChat() {
    const f = el('section', 'fr-face fr-chat');
    const head = el('div', 'fr-chat-head');
    this.chatAvatar = avatar(38);
    this.chatTile = groupTile(38);
    const words = el('div', 'fr-chat-who');
    this.chatName = el('b', '');
    this.chatState = el('small', '');
    // Muted (this browser's own): a bell crossed out after the name
    const nameRow = el('span', 'fr-chat-name');
    this.chatMuted = icon('bellOff', 15, 'fr-muted-icon');
    this.chatMuted.setAttribute('aria-hidden', 'false');
    this.chatMuted.setAttribute('role', 'img');
    this.chatMuted.setAttribute('aria-label', 'Muted');
    nameRow.append(this.chatName, this.chatMuted);
    words.append(nameRow, this.chatState);
    this.chatJoin = pillButton('success', 'Join', '', () => {
      const f_ = this.chatFriend();
      if (f_ && f_.room) this.join(f_.room, f_.name);
    });
    const anchor = el('span', 'fr-anchor');
    this.chatMore = iconButton('more', 'More', () => menuUnder(this.chatMore, this.chatMenu()));
    this.chatMore.setAttribute('aria-haspopup', 'menu');
    anchor.append(this.chatMore);
    head.append(iconButton('back', 'Back to the lists', () => {
      this.chat = '';
      this.render();
      this.focusFirst();
    }), this.chatAvatar, this.chatTile, words, this.chatJoin, anchor, iconButton('close', 'Close', () => this.close()));

    this.messagesBox = el('div', 'fr-messages');
    this.chatEmpty = el('div', 'fr-chat-empty');
    this.chatEmptyText = el('p', '');
    this.chatEmpty.append(icon('lock', 30), this.chatEmptyText);
    this.messageList = el('div', 'fr-message-list cv-list');
    this.messagesBox.append(this.chatEmpty, this.messageList);

    // The same row to write in as in a session's chat (../chatview.js): emoji, field, send
    this.chatInput = chatInputRow({
      placeholder: 'Message',
      label: 'Message',
      maxLength: 4000,
      onSend: (text, reply) => {
        if (!this.chat) return false;
        this.fr.send(this.chat, text, reply ? reply.id : '');
        this.stickToEnd = true;
        return true;
      },
    });
    // The row with what goes above it (the message answered, the names after "@")
    this.chatForm = this.chatInput.el;
    this.messageInput = this.chatInput.input;
    // All emoji, to react to a message with
    this.reactPicker = emojiPicker({
      once: true,
      label: 'React',
      onPick: (emoji) => {
        if (this.chat && this.reactingTo) this.fr.react(this.chat, this.reactingTo, emoji);
      },
    });
    f.append(head, el('div', 'fr-rule'), this.messagesBox, el('div', 'fr-rule'), this.chatForm, this.chatInput.picker, this.reactPicker);
    return f;
  }

  /** Closes an open emoji picker of the chat; false if none was open. */
  closeChatPickers() {
    if (!this.chatInput.picker.isOpen && !this.reactPicker.isOpen) return this.chatInput.closeExtras();
    this.chatInput.picker.close();
    this.reactPicker.close();
    return true;
  }

  chatFriend() {
    return this.fr.friends.find((x) => x.fp === this.chat) || null;
  }

  /** The open chat's group (id, name, admin, add, left, members, banned), or null for a friend's chat. */
  chatGroup() {
    return this.chat && this.fr.enabled ? this.fr.group(this.chat) : null;
  }

  /** The chat's ⋯ menu, as the app's: what fits a friend's chat or a group's. */
  chatMenu() {
    const g = this.chatGroup();
    const f = this.chatFriend();
    const entries = [];
    if (g) entries.push(['Members…', () => this.openSheet('members')]);
    entries.push(['Plan a time…', () => this.openSheet('plan')]);
    if (!g && f) entries.push(['Safety code…', () => this.showSafetyCode(f)]);
    // Muting is this browser's own: nothing is sent
    const chat = this.chat;
    if (this.fr.mutedUntil(chat)) entries.push(['Unmute', () => this.fr.setMute(chat, 0)]);
    else {
      entries.push(['Mute for 1 hour', () => this.fr.setMute(chat, Date.now() + 3600000)]);
      entries.push(['Mute for 8 hours', () => this.fr.setMute(chat, Date.now() + 8 * 3600000)]);
      entries.push(['Mute until I turn it back on', () => this.fr.setMute(chat, MUTE_FOREVER)]);
    }
    entries.push(['Report…', () => this.askReport()]);
    if (g) entries.push(['Leave group…', () => this.askLeave(g)]);
    if (!g && f) {
      entries.push(['Remove as friend…', () => this.askUnfriend(f)]);
      entries.push(['Block…', () => this.askBlock(f.fp, f.name)]);
    }
    return entries;
  }

  // ---------------------------------------------------- sheets: a new group, a group's members, a plan

  buildSheet() {
    const f = el('section', 'fr-face fr-sheet');
    const head = el('div', 'fr-chat-head');
    this.sheetTitle = el('b', 'fr-sheet-title');
    head.append(iconButton('back', 'Back', () => {
      this.sheet = '';
      this.render();
      this.focusFirst();
    }), this.sheetTitle);
    const scroll = el('div', 'fr-scroll');
    const col = el('div', 'fr-sheet-col');

    // ---- a new group
    this.groupNameField = field(null, 'The group\'s name, e.g. “Friday racing”', 48);
    // ---- members
    this.membersHead = heading('MEMBERS');
    this.membersList = el('div', 'fr-list');
    this.membersList.dataset.name = 'members';
    this.bannedHead = heading('BANNED');
    this.bannedList = el('div', 'fr-list');
    this.bannedList.dataset.name = 'banned';
    // ---- friends to pick: for a new group, or to add
    this.pickHead = heading('');
    this.pickList = el('div', 'fr-list tight');
    this.pickList.dataset.name = 'pick';
    this.makeGroupButton = pillButton('brand', 'Pick friends first', 'people', () => this.makeGroup(), false);
    this.makeGroupButton.classList.add('fr-sheet-go');
    this.addButton = pillButton('primary', 'Add', 'plus', () => {
      this.fr.addToGroup(this.chat, this.picked);
      this.picked = [];
      this.render();
    });
    this.addButton.classList.add('fr-sheet-add');
    // ---- the group's settings (admins)
    this.settingsHead = heading('SETTINGS');
    this.renameField = field(null, 'The group\'s name', 48);
    this.renameField.input.addEventListener('change', () => {
      const g = this.chatGroup();
      const name = this.renameField.input.value.trim();
      if (g && name && name !== g.name) this.fr.renameGroup(this.chat, name);
    });
    this.addWhoLabel = el('b', 'fr-sheet-label', 'Who may add people');
    this.addWho = el('div', 'fr-tabs fr-add-who');
    this.addWho.setAttribute('role', 'radiogroup');
    this.addWho.setAttribute('aria-label', 'Who may add people');
    this.addWhoButtons = [['admins', 'Admins'], ['all', 'Everybody']].map(([value, label]) => {
      const b = button('', label, () => this.fr.setGroupAdd(this.chat, value));
      b.setAttribute('role', 'radio');
      b.dataset.value = value;
      return b;
    });
    this.addWho.append(...this.addWhoButtons);
    // ---- a plan
    this.planTitleField = field(null, 'What for (optional), e.g. “Racing”', 80);
    this.planHead = heading('TIMES TO CHOOSE FROM');
    this.planList = el('div', 'fr-list');
    this.planList.dataset.name = 'plan';
    this.planAddRow = el('div', 'fr-plan-add');
    this.daySelect = el('select', 'fr-day');
    this.daySelect.setAttribute('aria-label', 'Day');
    this.hourSelect = el('select', 'fr-hour');
    this.hourSelect.setAttribute('aria-label', 'Time');
    for (const hour of V.PLAN_HOURS) {
      const option = el('option', '', hour);
      option.value = hour;
      this.hourSelect.append(option);
    }
    this.hourSelect.value = '20:00';
    this.planAddRow.append(this.daySelect, this.hourSelect, pillButton('', 'Add', 'plus', () => {
      const at = V.planTimeAt(Number(this.daySelect.value), this.hourSelect.value);
      if (!this.planTimes.includes(at) && at > Date.now()) this.planTimes = [...this.planTimes, at].sort((x, y) => x - y);
      this.render();
    }));
    this.askButton = pillButton('brand', 'Add a time first', 'calendar', () => {
      this.fr.sendPlan(this.chat, this.planTitleField.input.value.trim(), this.planTimes);
      this.sheet = '';
      this.stickToEnd = true;
      this.render();
    }, false);
    this.askButton.classList.add('fr-sheet-go');

    col.append(this.groupNameField, this.membersHead, this.membersList, this.bannedHead, this.bannedList, this.pickHead, this.pickList,
      this.makeGroupButton, this.addButton, this.settingsHead, this.renameField, this.addWhoLabel, this.addWho,
      this.planTitleField, this.planHead, this.planList, this.planAddRow, this.askButton);
    scroll.append(col);
    f.append(head, el('div', 'fr-rule'), scroll);
    return f;
  }

  openSheet(sheet) {
    this.sheet = sheet;
    this.picked = [];
    this.planTimes = [];
    this.groupNameField.input.value = '';
    this.planTitleField.input.value = '';
    const g = this.chatGroup();
    this.renameField.input.value = g ? g.name : '';
    // The days from today on, fresh each time
    this.daySelect.replaceChildren(...V.planDays().map((d) => {
      const option = el('option', '', d.label);
      option.value = String(d.date);
      return option;
    }));
    this.hourSelect.value = '20:00';
    this.render();
    this.focusFirst();
  }

  async makeGroup() {
    if (!this.picked.length) return;
    const id = await this.fr.createGroup(this.groupNameField.input.value.trim(), this.picked);
    this.sheet = '';
    if (id) {
      this.tab = 1;
      this.chat = id;
    }
    this.render();
  }

  togglePick(fp) {
    this.picked = this.picked.includes(fp) ? this.picked.filter((x) => x !== fp) : [...this.picked, fp];
    this.render();
  }

  // ---------------------------------------------------- the settings

  buildSettings() {
    const f = el('section', 'fr-face fr-settings');
    const top = this.titleRow(() => {
      this.face = 'main';
      this.render();
      this.focusFirst();
    });
    top.append(iconButton('close', 'Close', () => this.close()));
    const about = el('p', 'fr-set-about', 'Friends see whether you are online and what you play, and chat with you outside of sessions. End-to-end encrypted, without an account.');
    const scroll = el('div', 'fr-scroll set-body');

    // ---- Friends: on or off; while off, from a backup or the other device
    this.onSwitch = toggle('Friends and chats in this browser', false, (on) => {
      this.onSwitch.checked = !on;
      if (on) this.switchOn();
      else this.switchOff();
    });
    this.onRow = row({ title: 'Friends and chats in this browser', subtitle: ' ', switch: this.onSwitch });
    this.restoreRow = row({ title: 'Restore from a backup', subtitle: 'Your friends and keys from a backup file, with its passphrase.',
      controls: [pill('ghost', 'Restore…', 'upload', () => this.askBackup('restore'))] });
    this.joinInput = el('input', 'fr-join-link');
    this.joinInput.type = 'text';
    this.joinInput.spellcheck = false;
    this.joinInput.autocomplete = 'off';
    this.joinInput.placeholder = 'psrpt://link/…';
    this.joinInput.setAttribute('aria-label', 'The link from the other device');
    this.joinButton = pill('primary', 'Link', '', () => {
      const text = this.joinInput.value.trim();
      if (!text) return;
      this.fr.joinLink(text);
      this.openLinkCard();
    });
    this.joinInput.addEventListener('input', () => {
      this.joinButton.disabled = !this.joinInput.value.trim();
    });
    this.joinButton.disabled = true;
    this.joinRow = row({ title: 'Link to my other device',
      subtitle: 'Your friends from a computer or browser that has them: there, choose Link another device, and paste its link here.',
      controls: [this.joinInput, this.joinButton] });
    const friendsGroup = group('Friends', [this.onRow, this.restoreRow, this.joinRow]);

    // ---- What friends see (a browser hosts no sessions: "Friends can join my sessions" is the app's)
    this.statusSwitch = toggle('Show my status', true, (on) => {
      this.fr.showStatus = on ? 'all' : 'nobody';
    });
    this.gameSwitch = toggle('Show what I play', true, (on) => {
      this.fr.showGame = on;
    });
    this.readSwitch = toggle('Read receipts', true, (on) => {
      this.fr.readReceipts = on;
    });
    this.seeGroup = group('What friends see', [
      row({ title: 'Show my status', subtitle: 'Online, away or busy, and when you are free. Off: you look offline to everybody and still see your friends.', switch: this.statusSwitch }),
      row({ title: 'Show what I play', subtitle: 'The game and the console, e.g. “Playing Astro Bot on PS5”.', switch: this.gameSwitch }),
      row({ title: 'Read receipts', subtitle: 'Friends see when you have read their messages, and you see when they have read yours. Off: neither.', switch: this.readSwitch }),
    ]);

    // ---- Notifications while the page is closed: a browser's own
    this.pushSwitch = toggle('Notifications while this page is closed', false, (on) => {
      this.pushSwitch.checked = !on;
      this.setPush(on);
    });
    this.pushRow = row({ title: 'Notifications while this page is closed', subtitle: ' ', switch: this.pushSwitch });
    this.pushGroup = group('Notifications', [this.pushRow]);

    // ---- The friend link
    this.linkGroup = group('Your friend link', [row({
      title: 'New friend link',
      subtitle: 'The link you gave out stops working, so nobody can send you requests with it any more. Your friends stay.',
      controls: [pill('ghost', 'New link…', 'refresh', async () => {
        if (await ask('Make a new friend link?', 'Whoever has your old link can no longer send you a request. Your friends stay your friends.')) this.fr.newLink();
      })],
    })]);

    // ---- The own devices, and linking another one
    this.devicesGroup = group('Your devices', []);
    this.devicesGroup.insertBefore(el('p', 'set-note', 'Every device with your friends on it gets your chats. Removing one stops new messages going there.'),
      this.devicesGroup.card);
    this.linkRow = row({ title: 'Link another device', subtitle: 'Your friends and keys on one more computer or browser, without a backup file.',
      controls: [pill('ghost', 'Link…', 'link', () => {
        this.fr.startLinking();
        this.openLinkCard();
      })] });

    // ---- Backup
    this.backupGroup = group('Backup', [row({
      title: 'Save a backup',
      subtitle: 'A file with your keys and friends, sealed with a passphrase. Restoring it makes another browser or computer one of your devices.',
      controls: [pill('ghost', 'Save…', 'download', () => this.askBackup('save'))],
    })]);
    this.backupGroup.insertBefore(el('p', 'set-note', 'Your friends are tied to keys on your devices, not to an account. Without a backup they are gone with this browser\'s data.'),
      this.backupGroup.card);

    // ---- Blocked
    this.blockedGroup = group('Blocked', []);

    // ---- The friends server
    this.serverInput = el('input', 'fr-server');
    this.serverInput.type = 'text';
    this.serverInput.spellcheck = false;
    this.serverInput.autocomplete = 'off';
    this.serverInput.placeholder = 'https://example.org/psrpt_friends.php';
    this.serverInput.setAttribute('aria-label', 'Friends server address');
    this.serverInput.addEventListener('change', () => {
      const address = this.serverInput.value.trim();
      if (address !== this.fr.server) this.fr.setServer(address);
    });
    this.serverOwn = textLink('Use my own…', () => {
      this.serverEditing = true;
      this.render();
      this.serverInput.focus();
    });
    this.serverDefault = textLink('Default', () => {
      this.serverEditing = false;
      this.serverInput.value = '';
      this.fr.setServer('');
      this.render();
    });
    this.serverRow = row({ title: 'The default server', subtitle: ' ', textMin: 150, controls: [this.serverOwn, this.serverInput, this.serverDefault] });
    this.serverGroup = group('Friends server', [this.serverRow]);

    scroll.append(friendsGroup, this.seeGroup, this.pushGroup, this.linkGroup, this.devicesGroup, this.backupGroup, this.blockedGroup,
      this.serverGroup);
    f.append(top, about, scroll);
    return f;
  }

  openSettings() {
    this.start();
    this.face = 'settings';
    this.render();
    this.focusFirst();
  }

  /**
   * Linking a device, on either side (the app's link card): the link to give, the six digits to
   * compare, and "Same digits: send" on the side that has the friends.
   */
  openLinkCard() {
    if (this.linkCard) return;
    const d = el('dialog', 'dialog ask-card fr-link-card');
    const title = el('h2', '');
    const words = el('p', '');
    const linkRow = el('div', 'fr-link-row');
    const linkInput = el('input');
    linkInput.type = 'text';
    linkInput.readOnly = true;
    linkInput.setAttribute('aria-label', 'The link for the other device');
    const copy = pill('ghost', 'Copy', 'copy', async () => {
      if (!(await copyText(this.fr.linkText))) return;
      copy.set('ghost', 'Copied', 'copy');
      setTimeout(() => copy.set('ghost', 'Copy', 'copy'), 1800);
    });
    linkRow.append(linkInput, copy);
    const code = el('div', 'fr-link-code');
    const spinner = el('span', 'spin fr-link-spin');
    spinner.setAttribute('aria-hidden', 'true');
    const buttons = el('div', 'ask-card-buttons');
    const closeButton = button('pill ghost', 'Cancel', () => d.close());
    const sendButton = button('pill primary', 'Same digits: send', () => this.fr.confirmLinking());
    buttons.append(closeButton, sendButton);
    d.append(title, words, linkRow, code, spinner, buttons);
    const update = () => {
      const state = this.fr.linkState;
      // This side has the friends: it shows the link and sends
      const giving = this.fr.linkText.length > 0 || state === 'sending';
      title.textContent = V.linkTitle(state);
      words.textContent = V.linkWords(state, giving, this.fr.problem);
      linkRow.hidden = !(state === 'waiting' && giving);
      if (linkInput.value !== this.fr.linkText) linkInput.value = this.fr.linkText;
      code.hidden = state !== 'code';
      code.textContent = this.fr.linkCode;
      spinner.hidden = !(state === 'sending' || state === 'receiving' || (state === 'waiting' && !giving));
      closeButton.textContent = state === 'done' || state === 'failed' ? 'Close' : 'Cancel';
      sendButton.hidden = !(state === 'code' && giving);
    };
    d.addEventListener('close', () => {
      const state = this.fr.linkState;
      if (state !== 'done' && state !== 'failed') this.fr.cancelLinking();
      this.linkCard = null;
      d.remove();
      if (this.isOpen) this.render();
    });
    this.linkCard = { update };
    document.body.append(d);
    update();
    d.showModal();
    closeButton.focus();
  }

  // ------------------------------------------------------------ what the player does

  async switchOn() {
    await this.started;
    await this.profile.ready;
    this.onButton.disabled = true;
    await this.fr.setProfile(this.profile.name, this.profile.small);
    await this.fr.enable();
    this.onButton.disabled = false;
    if (!this.fr.enabled) {
      this.render();
      return;
    }
    if (this.activity) this.fr.setActivity(this.activity);
    this.face = 'main';
    this.render();
    // The first time in this browser: where the keys are, and the backup at once
    const answer = await card('Your friends live in this browser',
      'Your keys and chats are kept in this browser only. Clearing its data for this page loses them, and with them your friends here. A backup file keeps them: with it and its passphrase, this or another browser becomes your device again.',
      [['Later', 'ghost', 'later'], ['Save a backup…', 'primary', 'save']]);
    if (answer === 'save') this.askBackup('save');
    else this.focusFirst();
  }

  async switchOff() {
    if (!(await ask('Switch off friends in this browser?',
      'Your friends, chats and keys in this browser are deleted. Your friends keep you in their list, but this browser no longer gets messages. Save a backup first to keep them.'))) return;
    if (this.pushOn) {
      await push.switchOff(this.fr);
      this.pushOn = false;
    }
    this.chat = '';
    this.sheet = '';
    await this.fr.disable();
    this.face = 'main';
    this.render();
  }

  async setPush(on) {
    if (this.pushBusy) return;
    this.pushBusy = true;
    this.pushProblem = '';
    this.render();
    if (on) {
      this.pushProblem = await push.switchOn(this.fr);
      this.pushOn = !this.pushProblem;
    } else {
      await push.switchOff(this.fr);
      this.pushOn = false;
    }
    this.pushBusy = false;
    this.render();
  }

  join(room, name) {
    if (!room || !room.invite) return;
    this.close();
    this.o.onJoin(room, name || '');
  }

  friendMenu(f) {
    return [
      [f.checked ? 'Safety code (checked)…' : 'Safety code…', () => this.showSafetyCode(f)],
      ['Remove as friend…', () => this.askUnfriend(f)],
      ['Block…', () => this.askBlock(f.fp, f.name)],
    ];
  }

  async showSafetyCode(f) {
    if (!f || !f.safety) return;
    if (await ask(`Safety code with ${f.name}`,
      `${f.safety}\n\nCompare these numbers with the ones ${f.name} sees, in person or in a session. If they are the same, your chats go straight to ${f.name} and nobody is in between.\n\nMark ${f.name} as checked?`)) {
      this.fr.setChecked(f.fp, true);
    }
  }

  async askUnfriend(f) {
    if (!(await ask(`Remove ${f.name} as a friend?`,
      `${f.name} no longer sees your status, and you no longer see theirs. Your chat stays in this browser. You can become friends again with a new request.`))) return;
    if (this.chat === f.fp) this.chat = '';
    this.fr.unfriend(f.fp);
  }

  async askBlock(fp, name) {
    if (!(await ask(`Block ${name || 'this player'}?`,
      `${name || 'This player'} can no longer send you messages or friend requests, and no longer sees your status. They are not told.`))) return;
    if (this.chat === fp) this.chat = '';
    this.fr.block(fp);
  }

  /** The last ten messages others wrote here, opened, to the people who run the friends server. */
  async askReport() {
    const g = this.chatGroup();
    const f = this.chatFriend();
    const who = g ? g.name : f ? f.name : 'this chat';
    if (!(await ask(`Report ${who}?`,
      'The last ten messages others wrote here go, opened, to the people who run the friends server. Nobody else gets to read them, and the server could not read them before. You can block somebody as well.'))) return;
    const messages = this.fr.messages(this.chat);
    const ids = [];
    for (let i = messages.length - 1; i >= 0 && ids.length < 10; i--) {
      if (!messages[i].mine && !messages[i].system) ids.push(messages[i].id);
    }
    this.fr.report(this.chat, ids, '');
  }

  async askLeave(g) {
    if (!(await ask(`Leave ${g.name}?`, 'You no longer get its messages. The chat so far stays in this browser.'))) return;
    this.fr.leaveGroup(this.chat);
  }

  async askRemoveMember(m, ban) {
    const yes = ban
      ? await ask(`Ban ${m.name}?`, `${m.name} no longer gets the group's messages, and nobody but an admin can add them again.`)
      : await ask(`Remove ${m.name}?`, `${m.name} no longer gets the group's messages. They can be added again.`);
    if (yes) this.fr.removeFromGroup(this.chat, m.fp, ban);
  }

  openChat(fp) {
    this.chat = fp;
    this.sheet = '';
    this.tab = 1;
    this.stickToEnd = true;
    this.render();
    if (!this.chatForm.hidden) this.messageInput.focus();
  }

  /** The passphrase for saving ("save") or opening ("restore") a backup, then the file. */
  askBackup(mode) {
    const save = mode === 'save';
    const d = el('dialog', 'dialog ask-card fr-pass');
    const one = el('input');
    one.type = 'password';
    one.placeholder = 'Passphrase';
    one.setAttribute('aria-label', 'Passphrase');
    one.autocomplete = save ? 'new-password' : 'current-password';
    const two = el('input');
    two.type = 'password';
    two.placeholder = 'The same once more';
    two.setAttribute('aria-label', 'The passphrase once more');
    two.autocomplete = 'new-password';
    two.hidden = !save;
    const problem = el('p', 'fr-error');
    const go = button('pill primary', save ? 'Save the backup file' : 'Choose the file…', () => {
      const wrong = V.passphraseProblem(mode, one.value, two.value);
      problem.textContent = wrong;
      if (wrong) return;
      const pass = one.value;
      d.close();
      if (save) this.saveBackup(pass);
      else this.chooseBackup(pass);
    });
    const buttons = el('div', 'ask-card-buttons');
    buttons.append(button('pill ghost', 'Cancel', () => d.close()), go);
    const form = el('form', 'fr-pass-form');
    form.addEventListener('submit', (e) => {
      e.preventDefault();
      go.click();
    });
    // Return in a field: what the button does
    const submit = el('button', 'sr-only');
    submit.type = 'submit';
    submit.tabIndex = -1;
    form.append(one, two, submit);
    d.append(el('h2', '', save ? 'Passphrase for the backup' : 'Passphrase of the backup'),
      el('p', '', save ? 'At least 8 characters. Nobody can open the backup without it, not even you: write it down somewhere safe.'
        : 'The passphrase the backup was saved with. Then choose the file.'),
      form, problem, buttons);
    d.addEventListener('close', () => d.remove());
    document.body.append(d);
    d.showModal();
    one.focus();
  }

  async saveBackup(pass) {
    const text = await this.fr.saveBackup(pass);
    if (!text) {
      await info('Backup not saved', 'The backup could not be made.');
      return;
    }
    const url = URL.createObjectURL(new Blob([text], { type: 'application/octet-stream' }));
    const a = el('a');
    a.href = url;
    a.download = V.backupFileName();
    document.body.append(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 60000);
    await info('Backup saved', 'Keep the file and the passphrase apart. With both, anybody can be you to your friends.');
  }

  chooseBackup(pass) {
    const input = el('input');
    input.type = 'file';
    input.accept = '.psrpt-backup,text/plain,application/octet-stream';
    input.addEventListener('change', async () => {
      const file = input.files && input.files[0];
      if (!file) return;
      const text = file.size <= 16 * 1024 * 1024 ? await file.text() : '';
      await this.started;
      const ok = text ? await this.fr.restoreBackup(text, pass) : false;
      if (!ok) {
        await info('Backup not restored', 'This backup does not open with that passphrase, or it is no friends backup.');
        return;
      }
      await this.fr.setProfile(this.profile.name, this.profile.small);
      if (this.activity) this.fr.setActivity(this.activity);
      this.face = 'main';
      this.render();
      await info('Friends restored', 'This browser is now one of your devices. Your friends get to know it the next time they write.');
    });
    input.click();
  }

  // ------------------------------------------------------------ showing it

  currentFace() {
    if (this.face === 'settings') return this.setFace;
    if (!this.fr.enabled) return this.offFace;
    if (this.sheet) return this.sheetFace;
    return this.chat ? this.chatFace : this.listsFace;
  }

  render() {
    if (!this.isOpen) return;
    const face = this.currentFace();
    for (const f of [this.offFace, this.listsFace, this.chatFace, this.sheetFace, this.setFace]) f.hidden = f !== face;
    if (face === this.offFace) this.renderOff();
    else if (face === this.listsFace) this.renderLists();
    else if (face === this.chatFace) this.renderChat();
    else if (face === this.sheetFace) this.renderSheet();
    else this.renderSettings();
  }

  renderOff() {
    const problem = this.fr.problem;
    this.offProblem.textContent = problem;
    this.offProblem.hidden = !problem;
    // Another tab of the page is this browser's device
    this.onButton.disabled = /another tab/.test(problem);
  }

  /** Replaces a list's rows only when what they show changed, keeping the focus on the same row. */
  fill(list, key, rows) {
    if (this.drawn[list.dataset.name] === key) return;
    this.drawn[list.dataset.name] = key;
    const had = list.contains(document.activeElement) ? document.activeElement.dataset.key : '';
    list.replaceChildren(...rows());
    if (had) {
      const again = list.querySelector(`[data-key="${CSS.escape(had)}"]`);
      if (again) again.focus({ preventScroll: true });
    }
  }

  renderLists() {
    const fr = this.fr;
    this.ownAvatar.set(this.profile.avatarUrl, this.profile.name || '?', V.ownPresence(fr.myState));
    this.ownName.textContent = this.profile.name || 'No name yet';
    if (this.stateSelect.value !== fr.myState) this.stateSelect.value = fr.myState;
    if (document.activeElement !== this.freeField.input && this.freeField.input.value !== fr.myFree) this.freeField.input.value = fr.myFree;
    this.offline.hidden = fr.online;
    const requests = fr.requests;
    const friends = fr.friends;
    const labels = V.tabLabels(V.incomingCount(requests), fr.unread);
    this.tabButtons.forEach((b, i) => {
      b.textContent = labels[i];
      b.classList.toggle('chosen', i === this.tab);
      b.setAttribute('aria-selected', i === this.tab ? 'true' : 'false');
    });
    this.friendsTab.hidden = this.tab !== 0;
    this.chatsTab.hidden = this.tab !== 1;

    // Add a friend: their name, and Send unless they are a friend already
    const typed = this.linkField.input.value.trim().length > 0;
    this.lookErrorLine.textContent = this.lookError;
    this.lookErrorLine.hidden = !(this.lookError && typed);
    const looked = !!this.looked && typed;
    const already = looked && friends.some((f) => f.fp === this.looked.fp);
    this.lookedRow.hidden = !looked;
    this.noteField.hidden = !looked || already;
    this.requestButton.hidden = !looked || already;
    this.alreadyLine.hidden = !already;
    if (looked) {
      this.lookedAvatar.set(this.looked.avatar, this.looked.name);
      this.lookedName.textContent = this.looked.name;
      this.alreadyLine.textContent = `${this.looked.name} is your friend already.`;
    }

    this.requestsHead.hidden = requests.length === 0;
    this.fill(this.requestsList, JSON.stringify(requests), () => requests.map((r) => this.requestRow(r)));
    this.friendsHead.hidden = friends.length === 0;
    this.noFriends.hidden = friends.length > 0 || requests.length > 0;
    this.fill(this.friendsList, JSON.stringify(friends), () => friends.map((f) => this.friendRow(f)));

    const chats = fr.chats;
    this.newGroupButton.hidden = friends.length === 0;
    this.noChats.hidden = chats.length > 0;
    const now = Date.now();
    const states = new Map(friends.map((f) => [f.fp, f.state]));
    this.fill(this.chatsList, JSON.stringify([chats, [...states], new Date(now).toDateString()]),
      () => chats.map((c) => this.chatRow(c, states.get(c.id) || '', now)));
  }

  requestRow(r) {
    const box = el('div', 'fr-request' + (r.incoming ? ' incoming' : ''));
    const pic = avatar(38);
    pic.set(r.avatar, r.name || '?');
    const col = el('div', 'fr-request-words');
    col.append(el('b', '', r.name || 'Somebody'), el('small', r.unsent ? 'bad' : '', V.requestLine(r)));
    box.append(pic, col);
    if (r.incoming) {
      const buttons = el('div', 'fr-request-buttons');
      const yes = pillButton('success', 'Accept', '', () => this.fr.accept(r.fp));
      yes.dataset.key = `accept ${r.fp}`;
      const no = pillButton('', 'Decline', '', () => this.fr.decline(r.fp));
      no.dataset.key = `decline ${r.fp}`;
      buttons.append(yes, no, iconButton('block', 'Block', () => this.askBlock(r.fp, r.name), true));
      col.append(buttons);
    } else {
      if (r.unsent) {
        const again = textLink('Send again', () => this.fr.resendRequest(r.fp));
        again.dataset.key = `again ${r.fp}`;
        box.append(again);
      }
      const back = iconButton('close', 'Take back the request', () => this.fr.decline(r.fp), true);
      back.dataset.key = `back ${r.fp}`;
      box.append(back);
    }
    return box;
  }

  friendRow(f) {
    const r = el('div', 'fr-friend');
    // The whole row opens the chat; the chat button is the way there for the keys
    r.addEventListener('click', (e) => {
      if (!e.target.closest('button, .set-menu')) this.openChat(f.fp);
    });
    const pic = avatar(40);
    pic.set(f.avatar, f.name, f.state);
    const col = el('div', 'fr-friend-words');
    const name = el('div', 'fr-friend-name');
    name.append(el('b', '', f.name || 'A friend'));
    if (f.checked) {
      const shield = icon('shield', 13, 'fr-checked');
      shield.removeAttribute('aria-hidden');
      shield.setAttribute('role', 'img');
      shield.setAttribute('aria-label', 'Safety code checked');
      name.append(shield);
    }
    col.append(name, el('small', f.state === 'offline' ? 'off' : '', V.stateText(f)));
    if (f.free) col.append(el('small', 'free', `Free: ${f.free}`));
    r.append(pic, col);
    if (f.room && f.room.invite) {
      const join = pillButton('success', 'Join', '', () => this.join(f.room, f.name));
      join.dataset.key = `join ${f.fp}`;
      r.append(join);
    }
    const chat = iconButton('chat', `Chat with ${f.name}`, () => this.openChat(f.fp), true);
    chat.dataset.key = `chat ${f.fp}`;
    const anchor = el('span', 'fr-anchor');
    const more = iconButton('more', 'More', () => menuUnder(more, this.friendMenu(f)), true);
    more.dataset.key = `more ${f.fp}`;
    more.setAttribute('aria-haspopup', 'menu');
    anchor.append(more);
    r.append(chat, anchor);
    return r;
  }

  chatRow(c, state, now) {
    const b = button('fr-chatrow', '', () => this.openChat(c.id));
    b.dataset.key = `chat ${c.id}`;
    let pic;
    if (c.kind === 'group') {
      pic = groupTile(40);
      pic.set(c.left);
    } else {
      pic = avatar(40);
      pic.set(c.avatar, c.title, state);
    }
    const col = el('span', 'fr-chatrow-words');
    const top = el('span', 'fr-chatrow-top');
    top.append(el('b', '', c.title || 'A former friend'), el('small', '', V.timeText(c.at, now)));
    const bottom = el('span', 'fr-chatrow-bottom');
    // Muted: a bell crossed out, and what is new counts only dimmed (a mention of this player still shows)
    const loud = c.muted ? c.mentions > 0 : c.unread > 0;
    bottom.append(el('small', loud ? 'unread' : '', c.last));
    if (c.muted) {
      const bell = icon('bellOff', 14, 'fr-muted-icon');
      bell.setAttribute('aria-hidden', 'false');
      bell.setAttribute('role', 'img');
      bell.setAttribute('aria-label', 'Muted');
      bottom.append(bell);
    }
    if (c.unread > 0) bottom.append(el('span', 'fr-count' + (c.muted && !c.mentions ? ' muted' : ''), c.muted && c.mentions ? '@' : V.badgeText(c.unread)));
    col.append(top, bottom);
    b.append(pic, col);
    return b;
  }

  renderChat() {
    const fr = this.fr;
    const f = this.chatFriend();
    const g = this.chatGroup();
    this.chatAvatar.hidden = !!g;
    this.chatTile.hidden = !g;
    if (g) this.chatTile.set(g.left);
    else this.chatAvatar.set(f ? f.avatar : '', f ? f.name : '?', f ? f.state : '');
    this.chatName.textContent = g ? g.name : f ? f.name : 'No longer a friend';
    this.chatMuted.toggleAttribute('hidden', !fr.mutedUntil(this.chat));
    this.chatState.textContent = g ? (g.left ? 'You are no longer in this group' : V.membersText(g.members.length))
      : f ? V.stateText(f) : 'Messages can no longer be sent';
    this.chatJoin.hidden = !(f && f.room && f.room.invite);
    const open = !!f || (!!g && !g.left);
    this.chatMore.parentElement.hidden = !open;
    this.chatForm.hidden = !open;
    this.messageInput.setAttribute('aria-label', `Message to ${g ? g.name : f ? f.name : ''}`);
    // Read now that it shows, if somebody looks at it
    if (document.visibilityState === 'visible') fr.markRead(this.chat);

    const messages = fr.messages(this.chat);
    const now = Date.now();
    const person = f ? f.name : 'A friend';
    const lives = messages.map((m) => (m.join ? V.liveFor(m, f, '', now) : ''));
    this.chatEmptyText.textContent = g ? `Messages in ${g.name} are end-to-end encrypted. They are kept on your devices, not on the server.`
      : `Messages with ${f ? f.name : 'this friend'} are end-to-end encrypted. They are kept on your devices, not on the server.`;
    this.chatEmpty.hidden = messages.length > 0;
    const box = this.messagesBox;
    const atEnd = box.scrollHeight - box.scrollTop - box.clientHeight < 40;
    const members = g ? g.members.length : 2;
    // Whose pictures go under which message (as Instagram shows who read how far)
    const readers = placeFriendReaders(messages, fr.readers(this.chat));
    const avatars = Object.fromEntries(fr.friends.map((x) => [x.fp, x.avatar || '']));
    // "@Name" is drawn for the members (the friend) and this player; the list after "@" offers the others in a group
    const names = g ? g.members.map((x) => x.name) : [f ? f.name : '', fr.profileName];
    const people = g ? g.members.filter((x) => !x.me).map((x) => ({ name: x.name, avatar: avatars[x.fp] || '' })) : [];
    this.chatInput.setPeople(people);
    // No answering a message that has gone meanwhile
    const reply = this.chatInput.reply;
    if (reply && !messages.some((m) => String(m.id) === reply.id && !m.deleted)) this.chatInput.setReply(null);
    const key = JSON.stringify([this.chat, messages.map((m) => [m.id, m.state, m.votes, m.name, m.reactions, m.deleted, m.quoted, m.mention]),
      [...readers], lives, person, members, avatars, names, new Date(now).toDateString()]);
    if (this.drawn.messages === key) return;
    const otherChat = this.drawn.messagesChat !== this.chat;
    if (otherChat) {
      this.barKey = '';
      this.closeChatPickers();
      // Answering is for the chat it was started in
      this.chatInput.setReply(null);
    }
    this.drawn.messages = key;
    this.drawn.messagesChat = this.chat;
    const had = this.messageList.contains(document.activeElement) ? document.activeElement.dataset.key : '';
    this.messageList.replaceChildren(...messages.map((m, i) => this.message(m, i, messages,
      { live: lives[i], person, now, group: g, readers: readers.get(i) || [], avatar: avatars[m.from] || '', names })));
    if (had) {
      const again = this.messageList.querySelector(`[data-key="${CSS.escape(had)}"]`);
      if (again) again.focus({ preventScroll: true });
    }
    if (atEnd || otherChat || this.stickToEnd) box.scrollTop = box.scrollHeight;
    this.stickToEnd = false;
  }

  message(m, index, messages, { live, person, now, group: g, readers, avatar: picture, names }) {
    // A note of the group: in the middle, small
    if (m.system) return el('div', 'fr-system', m.text || '');
    const mine = m.mine === true;
    const deleted = m.deleted === true;
    const time = V.messageTime(m, now);
    let card = null;
    if (deleted) card = null;
    else if (m.plan) {
      card = planCard(m.plan, m.votes || {}, { me: this.fr.fingerprint, mine, person: m.name || 'A friend', time: V.timeText(m.at, now),
        people: g ? g.members.length : 2, now, onAnswer: (answers) => this.fr.vote(this.chat, m.id, answers) });
    } else if (m.join) {
      card = inviteCard(m.join, { mine, person: g ? m.name || 'A member' : person, live, time,
        onJoin: () => this.join(m.join, g ? m.name : person) });
    }
    // What it answers: the writer's name (one's own in the accent colour) and a line of it
    const q = m.quoted;
    const quote = q ? { name: q.name, text: q.text, deleted: q.deleted, earlier: !q.found && !q.name,
      color: q.from && q.from === this.fr.fingerprint ? '#b18cff' : '#a9b0ff' } : null;
    // As a session's chat draws a message (../chatview.js): picture and name of others in a group
    const key = String(m.id);
    const chat = this.chat;
    return chatMessage({
      key,
      mine,
      first: friendRunStarts(messages, index),
      showAuthor: !!g,
      name: m.name || 'A member',
      avatar: picture,
      text: m.text || '',
      deleted,
      quote,
      onQuote: q && q.found ? () => flashMessage(this.messageList, q.id) : null,
      names,
      mentionsMe: m.mention === true,
      // The preview the writer's app made (an app: a browser cannot look at other sites)
      embed: m.embed || null,
      status: time,
      statusBad: m.state === 'failed',
      card,
      reactions: friendReactions(m.reactions, this.fr.fingerprint),
      readers,
      onReact: (emoji) => this.fr.react(chat, m.id, emoji),
      onMore: () => {
        this.reactingTo = m.id;
        this.reactPicker.open();
      },
      onReply: () => this.chatInput.setReply({ id: key, name: mine ? this.fr.profileName || 'You' : m.name || 'A member',
        text: oneLine(m.text || '') }),
      onDelete: mine ? async () => {
        if (await askDelete()) this.fr.deleteMessage(chat, m.id);
      } : null,
      barOpen: this.barKey === key,
      onBar: (open) => {
        this.barKey = open ? key : this.barKey === key ? '' : this.barKey;
      },
    });
  }

  renderSheet() {
    const fr = this.fr;
    const sheet = this.sheet;
    const g = this.chatGroup();
    // A group's sheet without the group (gone meanwhile): back to the chat
    if (sheet === 'members' && !g) {
      this.sheet = '';
      this.render();
      return;
    }
    this.sheetTitle.textContent = sheet === 'newgroup' ? 'New group' : sheet === 'members' ? g.name : 'Plan a time';
    const members = sheet === 'members';
    const admin = members && g.admin;
    const canAdd = sheet === 'newgroup' || (members && !g.left && (g.admin || g.add === 'all'));
    this.groupNameField.hidden = sheet !== 'newgroup';
    this.membersHead.hidden = !members;
    this.membersList.hidden = !members;
    this.bannedHead.hidden = !members || !g.banned.length;
    this.bannedList.hidden = !members || !g.banned.length;
    this.pickHead.hidden = !canAdd;
    this.pickHead.textContent = sheet === 'newgroup' ? 'WHO IS IN IT' : 'ADD FRIENDS';
    this.pickList.hidden = !canAdd;
    this.makeGroupButton.hidden = sheet !== 'newgroup';
    this.makeGroupButton.disabled = !this.picked.length;
    this.makeGroupButton.set(this.picked.length ? `Make the group (${this.picked.length} friends)` : 'Pick friends first');
    this.addButton.hidden = !(members && this.picked.length);
    this.addButton.set(`Add ${this.picked.length}`);
    this.settingsHead.hidden = !admin;
    this.renameField.hidden = !admin;
    this.addWhoLabel.hidden = !admin;
    this.addWho.hidden = !admin;
    const plan = sheet === 'plan';
    this.planTitleField.hidden = !plan;
    this.planHead.hidden = !plan;
    this.planList.hidden = !plan;
    this.planAddRow.hidden = !plan || this.planTimes.length >= 8;
    this.askButton.hidden = !plan;
    this.askButton.disabled = !this.planTimes.length;
    this.askButton.set(this.planTimes.length ? 'Ask: when?' : 'Add a time first');

    if (members) {
      this.fill(this.membersList, JSON.stringify([g.members, g.admin]), () => g.members.map((m) => this.memberRow(m, g)));
      this.fill(this.bannedList, JSON.stringify(g.banned), () => g.banned.map((b) => el('p', 'fr-banned', b.name)));
      if (document.activeElement !== this.renameField.input) this.renameField.input.value = g.name;
      for (const b of this.addWhoButtons) {
        const chosen = b.dataset.value === (g.add === 'all' ? 'all' : 'admins');
        b.classList.toggle('chosen', chosen);
        b.setAttribute('aria-checked', chosen ? 'true' : 'false');
      }
    }
    if (canAdd) {
      const inGroup = new Set(members ? g.members.map((m) => m.fp) : []);
      const friends = fr.friends.filter((f) => !inGroup.has(f.fp));
      this.fill(this.pickList, JSON.stringify([friends, this.picked]), () => friends.map((f) => this.pickRow(f)));
    }
    if (plan) this.fill(this.planList, JSON.stringify(this.planTimes), () => this.planTimes.map((at, i) => this.timeRow(at, i)));
  }

  memberRow(m, g) {
    const r = el('div', 'fr-member');
    const pic = avatar(34);
    pic.set('', m.name);
    const col = el('div', 'fr-member-words');
    col.append(el('b', '', m.me ? `${m.name} (you)` : m.name));
    if (!m.friend && !m.me) col.append(el('small', '', 'Not your friend: known from this group'));
    r.append(pic, col);
    if (m.admin) r.append(el('span', 'badge brand fr-admin', 'Admin'));
    if (g.admin && !m.me && !g.left) {
      const anchor = el('span', 'fr-anchor');
      const more = iconButton('more', 'More', () => menuUnder(more, [
        [m.admin ? 'No longer admin' : 'Make admin', () => this.fr.setGroupAdmin(this.chat, m.fp, !m.admin)],
        ['Remove from the group…', () => this.askRemoveMember(m, false)],
        ['Remove and ban…', () => this.askRemoveMember(m, true)],
      ]), true);
      more.dataset.key = `member ${m.fp}`;
      more.setAttribute('aria-haspopup', 'menu');
      anchor.append(more);
      r.append(anchor);
    }
    return r;
  }

  pickRow(f) {
    const on = this.picked.includes(f.fp);
    const r = el('label', 'fr-pick' + (on ? ' on' : ''));
    const pic = avatar(34);
    pic.set(f.avatar, f.name, f.state);
    const box = el('input');
    box.type = 'checkbox';
    box.checked = on;
    box.setAttribute('aria-label', f.name || 'A friend');
    box.dataset.key = `pick ${f.fp}`;
    box.addEventListener('change', () => this.togglePick(f.fp));
    r.append(pic, el('b', '', f.name || 'A friend'), box);
    return r;
  }

  timeRow(at, index) {
    const r = el('div', 'fr-time');
    r.append(icon('clock', 16, 'fr-time-icon'), el('b', '', V.planTimeText(at)), iconButton('close', 'Remove this time', () => {
      this.planTimes = this.planTimes.filter((_, i) => i !== index);
      this.render();
    }, true));
    return r;
  }

  renderSettings() {
    const fr = this.fr;
    const on = fr.enabled;
    this.onSwitch.checked = on;
    // Another tab of the page is this browser's device: nothing to switch on, restore or link here
    const elsewhere = !on && /another tab/.test(fr.problem);
    this.onSwitch.disabled = elsewhere;
    this.restoreRow.setEnabled(!elsewhere);
    this.joinRow.setEnabled(!elsewhere);
    this.onRow.setSubtitle(on ? 'Your keys and chats are kept in this browser.' : fr.problem || 'Switched off: no friends server is asked.');
    this.restoreRow.hidden = on;
    this.joinRow.hidden = on;
    for (const g of [this.seeGroup, this.pushGroup, this.linkGroup, this.backupGroup, this.serverGroup]) {
      g.classList.toggle('off', !on);
      g.inert = !on;
    }
    this.statusSwitch.checked = fr.showStatus !== 'nobody';
    this.gameSwitch.checked = fr.showGame;
    this.readSwitch.checked = fr.readReceipts;

    // Notifications: what this browser can, and whether they are on
    const can = push.support();
    this.pushSwitch.checked = this.pushOn;
    this.pushSwitch.disabled = this.pushBusy || can === 'none' || (can === 'home' && !this.pushOn);
    const denied = push.permission() === 'denied';
    this.pushRow.setSubtitle(this.pushProblem
      || (can === 'home' ? 'On an iPhone or iPad, add this page to your home screen first: Share, then Add to Home Screen (iOS 16.4 or later).'
        : can === 'none' ? 'This browser cannot show notifications while the page is closed.'
          : denied && !this.pushOn ? 'Notifications are blocked for this page. They can be allowed in the browser\'s settings for the site.'
            : 'Your browser tells you when something new comes from your friends. It does not say who or what: that you see here.'));

    // The own devices
    this.devicesGroup.hidden = !on;
    const devices = on ? fr.devices : [];
    const devicesKey = JSON.stringify(devices);
    if (this.drawn.devices !== devicesKey) {
      this.drawn.devices = devicesKey;
      this.devicesGroup.card.replaceChildren(...devices.map((d) => {
        const words = V.deviceText(d);
        const controls = d.me ? [] : [textLink('Remove…', async () => {
          if (await ask('Remove this device?',
            'Your friends no longer send to it. A device that still has your keys could put itself back: for a lost one, a backup on a new device and a new friend link are safer.')) {
            fr.removeDevice(d.id);
          }
        })];
        return row({ title: words.title, subtitle: words.line, textMin: 140, controls });
      }), this.linkRow);
    }

    // Blocked
    this.blockedGroup.hidden = !on;
    const blocked = on ? fr.blocked : [];
    const blockedKey = JSON.stringify(blocked);
    if (this.drawn.blocked !== blockedKey) {
      this.drawn.blocked = blockedKey;
      this.blockedGroup.card.replaceChildren(...(blocked.length ? blocked.map((b) => row({ title: b.name || 'A player without a name', textMin: 120,
        controls: [textLink('Unblock', () => fr.unblock(b.fp))] }))
        : [el('p', 'fr-set-empty', 'Nobody. Blocked players cannot send you messages or requests.')]));
    }

    // The server
    const own = fr.server.length > 0;
    const editing = own || this.serverEditing;
    this.serverRow.title_.textContent = editing ? 'Your own server' : 'The default server';
    this.serverRow.setSubtitle(editing ? 'You and your friends need the same one. Changing it starts anew there: send your friends your link again.'
      : fr.defaultServer);
    this.serverOwn.hidden = editing;
    this.serverInput.hidden = !editing;
    this.serverDefault.hidden = !editing;
    if (document.activeElement !== this.serverInput) this.serverInput.value = fr.server;
  }
}
