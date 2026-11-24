// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// One message as both chats of the page draw it, the session's chat (app.js) and the friends'
// chats (friends/panel.js), as the app's gui/src/qml/ChatBubble.qml does in both of its chats:
// own messages on the right in the brand's colour, the others' on the left with their picture
// and name at the start of a run, what it answers (a quote), "@Name" of a member in the accent
// colour, a link's preview, the reactions under it as chips, and the pictures of whoever has
// read up to it. On hover (or focus) beside the message: reply, react, and for one's own
// delete; the smiley opens a bar of quick reactions over it, a long press does too, and "+"
// there opens all emoji. A deleted message stays as "Message deleted". A card (an invite, a
// plan) can stand instead of the text bubble.
//
// Also the emoji to choose from (EmojiPicker.qml), for writing and for reacting, and the row
// to write in (the same in both chats: emoji, field, send), with the message it answers above
// it and the names to pick after "@".
//
// Names, texts, emoji and everything else from other players go in as text (textContent) or as
// the address of a picture that is checked first; nothing here is ever HTML.

import { button, el } from './dom.js';
import { EMOJI_GROUPS, MAX_READER_PICTURES, QUICK_REACTIONS, mentionChoices, mentionQuery, splitMentions } from './chatmarks.js';
import { icon } from './menu.js';

/** Pictures only as data the page made (a JPEG as base64) or a picture it holds (blob:). */
const SAFE_PICTURE = /^(data:image\/jpeg;base64,[A-Za-z0-9+/]+={0,2}|blob:[A-Za-z0-9:/._-]+)$/;

const LONG_PRESS_MS = 500;
/** The bar closes once the pointer has been away from the message and the bar for this long. */
const BAR_CLOSE_MS = 700;

function initialOf(name) {
  return (String(name || '?').trim().charAt(0) || '?').toUpperCase();
}

/** A round picture, or the first letter of the name on the brand's colours (the app's C.Avatar). */
export function face(url, name, size, cls = '') {
  const f = el('span', 'cv-face' + (cls ? ' ' + cls : ''));
  f.style.setProperty('--size', `${size}px`);
  f.setAttribute('aria-hidden', 'true');
  if (typeof url === 'string' && SAFE_PICTURE.test(url)) {
    f.style.backgroundImage = `url("${url}")`;
    f.classList.add('picture');
  } else f.textContent = initialOf(name);
  return f;
}

/** Plain text, with "@Name" of one of the names as a span in the accent colour. */
function appendWithMentions(parent, text, names) {
  for (const part of splitMentions(text, names)) {
    if (part.name) parent.append(el('span', 'cv-at', part.text));
    else parent.append(part.text);
  }
}

/**
 * The text of a message: as it is, with http(s) addresses as links (without what ends a
 * sentence), as ./friends/chattext.js and the app's chattext make it, but as text nodes; and
 * "@Name" of one of `names` (the chat's members and oneself) drawn as a mention.
 */
export function appendChatText(parent, text, names = []) {
  const s = String(text ?? '');
  const link = /https?:\/\/[^\s<>"']+/gi;
  let last = 0;
  for (let m = link.exec(s); m; m = link.exec(s)) {
    let url = m[0];
    while (url && '.,;:!?)]}'.includes(url[url.length - 1])) url = url.slice(0, -1);
    if (m.index > last) appendWithMentions(parent, s.slice(last, m.index), names);
    const a = el('a', '', url);
    a.href = url;
    a.target = '_blank';
    a.rel = 'noopener noreferrer';
    parent.append(a);
    last = m.index + url.length;
    link.lastIndex = last;
  }
  if (last < s.length) appendWithMentions(parent, s.slice(last), names);
}

/**
 * What a reply answers, over its text: the writer's name in their colour and one line of what
 * they wrote, with an accent on the left; a click goes to it.
 * @param {{name: string, color?: string, text: string, deleted?: boolean, earlier?: boolean}} q
 */
function quoteBlock(q, onQuote) {
  const b = button('cv-quote', '', () => onQuote && onQuote());
  if (q.color) b.style.setProperty('--quote', q.color);
  if (q.name) b.append(el('span', 'cv-quote-name', q.name));
  b.append(el('span', 'cv-quote-text' + (q.deleted || q.earlier ? ' gone' : ''), q.text || ''));
  b.setAttribute('aria-label', `Answers ${q.name || 'a message'}: ${q.text || ''}`);
  if (!onQuote) b.disabled = true;
  return b;
}

/** Goes to a message of a list (by its data-key) and lights it up for a moment. */
export function flashMessage(list, key) {
  const target = Array.from(list.querySelectorAll('[data-key]')).find((n) => n.dataset.key === key);
  if (!target) return false;
  target.scrollIntoView({ block: 'center', behavior: 'smooth' });
  target.classList.remove('cv-flash');
  // Again from the start, also when it was lit a moment ago
  void target.offsetWidth;
  target.classList.add('cv-flash');
  setTimeout(() => target.classList.remove('cv-flash'), 1600);
  return true;
}

/** "Delete for everybody?": resolves to true for Delete. */
export function askDelete() {
  return new Promise((resolve) => {
    const d = el('dialog', 'dialog ask-card cv-ask');
    const cancel = button('pill ghost', 'Cancel', () => d.close('no'));
    const yes = button('pill cv-danger', 'Delete', () => d.close('yes'));
    const buttons = el('div', 'ask-card-buttons');
    buttons.append(cancel, yes);
    d.append(el('h2', '', 'Delete for everybody?'),
      el('p', '', 'The message goes away for everybody in this chat. Whoever has read it already has seen it.'), buttons);
    d.addEventListener('close', () => {
      d.remove();
      resolve(d.returnValue === 'yes');
    });
    // Its own keys: Escape closes it, nothing behind it gets them
    d.addEventListener('keydown', (e) => e.stopPropagation());
    document.body.append(d);
    d.showModal();
    cancel.focus();
  });
}

function hostOf(url) {
  try {
    return new URL(url).hostname;
  } catch (e) {
    return '';
  }
}

/** The preview the writer's app made of the first link: site, title, description, picture. */
function preview(embed) {
  const a = el('a', 'cv-embed');
  a.href = embed.url;
  a.target = '_blank';
  a.rel = 'noopener noreferrer';
  const site = embed.site || hostOf(embed.url);
  if (site) a.append(el('span', 'cv-embed-site', site));
  if (embed.title) a.append(el('span', 'cv-embed-title', embed.title));
  if (embed.description) a.append(el('span', 'cv-embed-text', embed.description));
  if (typeof embed.image === 'string' && SAFE_PICTURE.test(embed.image)) {
    const img = el('img');
    img.alt = '';
    img.src = embed.image;
    a.append(img);
  }
  return a;
}

/** The reactions under a message: emoji and how many; one's own outlined; a click gives or takes back. */
function chips(reactions, mine, onReact) {
  const row = el('div', 'cv-chips' + (mine ? ' mine' : ''));
  for (const r of reactions) {
    const b = button('cv-chip' + (r.mine ? ' own' : ''), '', () => onReact && onReact(r.emoji));
    b.append(el('span', 'cv-chip-emoji', r.emoji));
    if (r.count > 1) b.append(el('span', 'cv-chip-count', String(r.count)));
    b.setAttribute('aria-label', `${r.emoji}, ${r.count}`);
    b.setAttribute('aria-pressed', r.mine ? 'true' : 'false');
    if (!onReact) b.disabled = true;
    row.append(b);
  }
  return row;
}

/** Who has read up to here: small pictures on the right, at most five, then "+N". */
function readerPictures(readers) {
  const row = el('div', 'cv-readers');
  for (const r of readers.slice(0, MAX_READER_PICTURES)) {
    const ring = el('span', 'cv-reader');
    ring.title = `Read by ${r.name || 'a player'}`;
    ring.append(face(r.avatar, r.name || '?', 15));
    row.append(ring);
  }
  if (readers.length > MAX_READER_PICTURES) {
    const more = el('span', 'cv-readers-more', `+${readers.length - MAX_READER_PICTURES}`);
    more.title = readers.slice(MAX_READER_PICTURES).map((r) => `Read by ${r.name || 'a player'}`).join('\n');
    row.append(more);
  }
  row.setAttribute('aria-label', readers.map((r) => `Read by ${r.name || 'a player'}`).join(', '));
  return row;
}

/**
 * One message.
 * @param {object} o
 * @param {string} o.key what the message is, kept on the element (data-key)
 * @param {boolean} o.mine
 * @param {boolean} [o.first] the first of a run by one writer: their picture and name show
 * @param {boolean} [o.showAuthor] pictures and names of others (a group, a session; not one friend)
 * @param {string} [o.name]
 * @param {string} [o.nameColor]
 * @param {string} [o.avatar] the writer's picture (data: or blob:)
 * @param {boolean} [o.host] the HOST tag after the name
 * @param {string} [o.text]
 * @param {boolean} [o.dim] on its way: the text a little faded
 * @param {boolean} [o.deleted] deleted by its writer: "Message deleted", nothing to do with it
 * @param {string} [o.status] under the text: the time, "Sending…", "Not sent"
 * @param {boolean} [o.statusBad]
 * @param {{name: string, color?: string, text: string, deleted?: boolean, earlier?: boolean}} [o.quote] what it answers
 * @param {() => void} [o.onQuote] a click on the quote
 * @param {string[]} [o.names] the names "@Name" is drawn for (the chat's members and oneself)
 * @param {boolean} [o.mentionsMe] it names this player: marked
 * @param {{url: string, site?: string, title?: string, description?: string, image?: string}} [o.embed]
 * @param {HTMLElement} [o.card] instead of the text bubble
 * @param {{emoji: string, count: number, mine: boolean}[]} [o.reactions]
 * @param {{name: string, avatar: string}[]} [o.readers]
 * @param {boolean} [o.canReact]
 * @param {(emoji: string) => void} [o.onReact]
 * @param {() => void} [o.onMore] "+": all emoji
 * @param {() => void} [o.onReply] answering it can be started
 * @param {() => void} [o.onDelete] it can be deleted for everybody (one's own)
 * @param {boolean} [o.barOpen] the quick reactions show (kept by whoever draws the list again)
 * @param {(open: boolean) => void} [o.onBar]
 */
export function chatMessage(o) {
  const mine = !!o.mine;
  const first = o.first !== false;
  const deleted = !!o.deleted;
  const sideAvatar = !!o.showAuthor && !mine;
  const card = deleted ? null : o.card;
  const m = el('div', 'cv-msg' + (mine ? ' mine' : '') + (first ? ' first' : '') + (sideAvatar ? ' side' : '') + (card ? ' with-card' : '')
    + (deleted ? ' deleted' : ''));
  if (o.key) m.dataset.key = o.key;

  if (sideAvatar && first) m.append(face(o.avatar, o.name, 30, 'cv-author'));
  const column = el('div', 'cv-col');
  m.append(column);

  if (sideAvatar && first) {
    const nameRow = el('div', 'cv-name-row');
    const label = el('span', 'cv-name', o.name || '');
    if (o.nameColor) label.style.color = o.nameColor;
    nameRow.append(label);
    if (o.host) nameRow.append(el('span', 'cv-host', 'HOST'));
    column.append(nameRow);
  }

  // The bubble or the card, with the actions beside it and the bar over it
  const body = el('div', 'cv-body');
  if (card) {
    body.classList.add('cv-card');
    body.append(card);
  } else {
    const bubble = el('div', 'cv-bubble' + (o.dim ? ' dim' : '') + (o.mentionsMe && !deleted ? ' mentions-me' : ''));
    if (deleted) bubble.append(el('div', 'cv-text cv-gone', 'Message deleted'));
    else {
      if (o.quote) bubble.append(quoteBlock(o.quote, o.onQuote));
      if (o.text) {
        const text = el('div', 'cv-text');
        appendChatText(text, o.text, o.names || []);
        bubble.append(text);
      }
      if (o.embed && /^https?:\/\//i.test(o.embed.url || '') && (o.embed.title || o.embed.description || o.embed.image)) {
        bubble.append(preview(o.embed));
      }
    }
    if (o.status) bubble.append(el('small', 'cv-status' + (o.statusBad ? ' bad' : ''), o.status));
    body.append(bubble);
  }
  column.append(body);

  const canReact = !deleted && o.canReact !== false && !!o.onReact;
  const onReply = deleted ? null : o.onReply || null;
  const onDelete = deleted || !mine ? null : o.onDelete || null;
  if (canReact || onReply || onDelete) {
    let open = false;
    let closeTimer = 0;
    const bar = el('div', 'cv-bar');
    bar.setAttribute('role', 'toolbar');
    bar.setAttribute('aria-label', 'Reactions');
    const setOpen = (on, tell = true) => {
      if (open === on) return;
      open = on;
      m.classList.toggle('bar-open', on);
      bar.hidden = !on;
      clearTimeout(closeTimer);
      if (on) {
        // Over the message; under it where the list would cut it off at its top
        bar.classList.remove('below');
        const list = m.closest('.cv-list');
        if (list && bar.getBoundingClientRect().top < list.getBoundingClientRect().top) bar.classList.add('below');
        document.addEventListener('pointerdown', outside, true);
      } else document.removeEventListener('pointerdown', outside, true);
      if (tell && o.onBar) o.onBar(on);
    };
    const outside = (e) => {
      if (!m.contains(e.target)) setOpen(false);
    };
    const action = (cls, label, content, run) => {
      const b = button(cls, typeof content === 'string' ? content : '', () => {
        setOpen(false);
        run();
      });
      if (typeof content !== 'string') b.append(content);
      b.setAttribute('aria-label', label);
      b.title = label;
      return b;
    };
    if (canReact) {
      for (const emoji of QUICK_REACTIONS) {
        const b = action('cv-quick', `React with ${emoji}`, emoji, () => o.onReact(emoji));
        b.title = '';
        bar.append(b);
      }
      bar.append(action('cv-quick more', 'More reactions', icon('plus', 16), () => o.onMore && o.onMore()));
    }
    // After "+": answering and, for one's own, deleting (where a long press is all there is)
    if (onReply) bar.append(action('cv-quick more', 'Reply', icon('reply', 16), onReply));
    if (onDelete) bar.append(action('cv-quick more bad', 'Delete', icon('trash', 16), onDelete));
    bar.hidden = true;

    // Beside the message while the pointer is on it: reply, react, delete
    const actions = el('div', 'cv-actions');
    if (onReply) {
      const b = button('cv-act', '', onReply);
      b.append(icon('reply', 15));
      b.setAttribute('aria-label', 'Reply');
      b.title = 'Reply';
      actions.append(b);
    }
    let smiley = null;
    if (canReact) {
      smiley = button('cv-act', '🙂', () => {
        setOpen(true);
        const firstQuick = bar.querySelector('button');
        if (firstQuick) firstQuick.focus({ preventScroll: true });
      });
      smiley.setAttribute('aria-label', 'React');
      smiley.title = 'React';
      actions.append(smiley);
    }
    if (onDelete) {
      const b = button('cv-act bad', '', onDelete);
      b.append(icon('trash', 15));
      b.setAttribute('aria-label', 'Delete');
      b.title = 'Delete';
      actions.append(b);
    }
    bar.addEventListener('keydown', (e) => {
      if (e.key !== 'Escape') return;
      e.stopPropagation();
      setOpen(false);
      (smiley || actions.firstElementChild).focus();
    });
    body.append(actions, bar);

    // Closes once the pointer has left the message and the bar for a moment
    m.addEventListener('pointerleave', (e) => {
      if (e.pointerType === 'mouse' && open) closeTimer = setTimeout(() => setOpen(false), BAR_CLOSE_MS);
    });
    m.addEventListener('pointerenter', () => clearTimeout(closeTimer));

    // A long press opens it too (on a phone there is no hovering)
    let pressTimer = 0;
    const cancelPress = () => clearTimeout(pressTimer);
    body.addEventListener('pointerdown', (e) => {
      if (e.pointerType === 'mouse' || e.target.closest('a, button, input, select')) return;
      cancelPress();
      pressTimer = setTimeout(() => setOpen(!open), LONG_PRESS_MS);
    });
    for (const type of ['pointerup', 'pointercancel', 'pointermove']) body.addEventListener(type, cancelPress);

    if (o.barOpen) queueMicrotask(() => setOpen(true, false));
  }

  if (!deleted && o.reactions && o.reactions.length) column.append(chips(o.reactions, mine, canReact ? o.onReact : null));
  if (o.readers && o.readers.length) column.append(readerPictures(o.readers));
  return m;
}

/**
 * The emoji to choose from, by groups (EmojiPicker.qml), for writing or, with `once`, for a
 * reaction (one pick closes it). Put it into the chat's panel; it shows over it.
 * @param {{once?: boolean, onPick: (emoji: string) => void, label?: string, onClose?: () => void}} o
 */
export function emojiPicker(o) {
  const p = el('div', 'cv-picker' + (o.once ? ' react' : ' write'));
  p.setAttribute('role', 'dialog');
  p.setAttribute('aria-label', o.label || 'Emoji');
  p.hidden = true;
  const tabs = el('div', 'cv-picker-tabs');
  const grid = el('div', 'cv-picker-grid');
  let group = 0;
  const showGroup = (index) => {
    group = index;
    for (const [i, tab] of Array.from(tabs.children).entries()) {
      tab.classList.toggle('chosen', i === index);
      tab.setAttribute('aria-pressed', i === index ? 'true' : 'false');
    }
    grid.replaceChildren(...EMOJI_GROUPS[index][1].split(' ').map((emoji) => {
      const b = button('', emoji, () => {
        o.onPick(emoji);
        if (o.once) p.close();
      });
      b.setAttribute('aria-label', emoji);
      return b;
    }));
    grid.scrollTop = 0;
  };
  EMOJI_GROUPS.forEach(([tabIcon], index) => tabs.append(button('', tabIcon, () => showGroup(index))));
  p.append(tabs, grid);
  showGroup(group);

  /** What opened it: a press there does not count as outside. */
  let opener = null;
  const outside = (e) => {
    if (!p.contains(e.target) && !(opener && opener.contains(e.target))) p.close();
  };
  p.open = (from = null) => {
    opener = from;
    p.hidden = false;
    document.addEventListener('pointerdown', outside, true);
    const chosen = tabs.children[group];
    if (chosen) chosen.focus({ preventScroll: true });
  };
  p.close = () => {
    if (p.hidden) return;
    p.hidden = true;
    document.removeEventListener('pointerdown', outside, true);
    if (o.onClose) o.onClose();
  };
  p.toggle = (from = null) => (p.hidden ? p.open(from) : p.close());
  Object.defineProperty(p, 'isOpen', { get: () => !p.hidden });
  p.addEventListener('keydown', (e) => {
    if (e.key !== 'Escape') return;
    e.stopPropagation();
    p.close();
  });
  return p;
}

/**
 * The row to write in, the same in both chats (the app's RoomChat and FriendsPanel): the emoji
 * button with its picker, the field, and the send button; Return sends. Above it while there is
 * one: the message being answered ("Replying to …", ✕), and after "@" the names to pick from.
 * Put `el` into the chat (and `picker` over it).
 * @param {object} o
 * @param {string} o.placeholder
 * @param {string} o.label
 * @param {number} o.maxLength
 * @param {(text: string, reply: object|null) => boolean|void} o.onSend gets the trimmed text and
 *   what it answers (as given to setReply); false keeps both
 */
export function chatInputRow(o) {
  const wrap = el('div', 'cv-writer');
  const form = el('form', 'cv-input');
  const emojiButton = button('cv-emoji-button', '😊');
  emojiButton.setAttribute('aria-label', 'Emoji');
  emojiButton.title = 'Emoji';
  const input = el('input', 'cv-field');
  input.type = 'text';
  input.maxLength = o.maxLength;
  input.autocomplete = 'off';
  input.placeholder = o.placeholder;
  input.setAttribute('aria-label', o.label);
  const send = button('cv-send', '');
  send.type = 'submit';
  send.append(icon('send', 20));
  send.setAttribute('aria-label', 'Send');
  send.title = 'Send';
  send.disabled = true;
  const sync = () => {
    send.disabled = input.disabled || input.value.trim().length === 0;
  };

  // ---- the message being answered
  let reply = null;
  const replyBar = el('div', 'cv-reply-bar');
  replyBar.hidden = true;
  const replyWords = el('div', 'cv-reply-words');
  const replyTitle = el('span', 'cv-reply-title');
  const replyText = el('span', 'cv-reply-text');
  replyWords.append(replyTitle, replyText);
  const replyClose = button('cv-reply-close', '', () => {
    setReply(null);
    input.focus();
  });
  replyClose.append(icon('close', 14));
  replyClose.setAttribute('aria-label', 'Do not reply');
  replyClose.title = 'Do not reply';
  replyBar.append(icon('reply', 16, 'cv-reply-icon'), replyWords, replyClose);
  /** @param {{name: string, text: string}|null} target and whatever the owner keeps with it */
  function setReply(target) {
    reply = target || null;
    replyBar.hidden = !reply;
    replyTitle.textContent = reply ? `Replying to ${reply.name || 'a message'}` : '';
    replyText.textContent = reply ? reply.text || '' : '';
  }

  // ---- the names to pick after "@"
  let people = [];
  let shown = [];
  let chosen = 0;
  let query = null;
  const list = el('div', 'cv-mentions');
  list.setAttribute('role', 'listbox');
  list.setAttribute('aria-label', 'Mention');
  list.hidden = true;
  const closeList = () => {
    list.hidden = true;
    shown = [];
    query = null;
    input.removeAttribute('aria-activedescendant');
  };
  const drawList = () => {
    list.replaceChildren(...shown.map((p, i) => {
      const b = button('cv-mention' + (i === chosen ? ' chosen' : ''), '', () => pick(i));
      b.id = `cv-mention-${i}`;
      b.setAttribute('role', 'option');
      b.setAttribute('aria-selected', i === chosen ? 'true' : 'false');
      // Taken before the field loses its focus
      b.addEventListener('pointerdown', (e) => e.preventDefault());
      b.append(face(p.avatar, p.name, 24), el('span', 'cv-mention-name', p.name));
      return b;
    }));
    input.setAttribute('aria-activedescendant', `cv-mention-${chosen}`);
  };
  const updateList = () => {
    query = people.length && input.selectionStart !== null ? mentionQuery(input.value, input.selectionStart) : null;
    shown = query ? mentionChoices(people, query.query).slice(0, 8) : [];
    if (!shown.length) {
      closeList();
      return;
    }
    chosen = Math.min(chosen, shown.length - 1);
    list.hidden = false;
    drawList();
  };
  function pick(i) {
    const p = shown[i];
    if (!p || !query) return;
    const caret = input.selectionStart === null ? input.value.length : input.selectionStart;
    const before = input.value.slice(0, query.at);
    const after = input.value.slice(caret);
    const insert = `@${p.name} `;
    if ((before + insert + after).length > input.maxLength) return;
    input.value = before + insert + after;
    input.selectionStart = input.selectionEnd = before.length + insert.length;
    closeList();
    sync();
    input.focus();
  }

  input.addEventListener('input', () => {
    sync();
    chosen = 0;
    updateList();
  });
  input.addEventListener('click', updateList);
  input.addEventListener('blur', () => setTimeout(() => {
    if (document.activeElement !== input) closeList();
  }, 0));
  input.addEventListener('keydown', (e) => {
    if (!list.hidden && shown.length) {
      if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
        e.preventDefault();
        chosen = (chosen + (e.key === 'ArrowDown' ? 1 : shown.length - 1)) % shown.length;
        drawList();
        return;
      }
      if (e.key === 'Enter' || e.key === 'Tab') {
        e.preventDefault();
        e.stopPropagation();
        pick(chosen);
        return;
      }
      if (e.key === 'Escape') {
        e.preventDefault();
        e.stopPropagation();
        closeList();
        return;
      }
    }
    // Escape: no reply after all (and nothing else behind the field takes it)
    if (e.key === 'Escape' && reply) {
      e.preventDefault();
      e.stopPropagation();
      setReply(null);
    }
  });
  form.addEventListener('submit', (e) => {
    e.preventDefault();
    const text = input.value.trim();
    if (!text || input.disabled) return;
    if (o.onSend(text, reply) === false) return;
    input.value = '';
    setReply(null);
    closeList();
    sync();
  });
  const picker = emojiPicker({
    label: 'Emoji',
    onPick: (emoji) => {
      const at = input.selectionStart === null ? input.value.length : input.selectionStart;
      const end = input.selectionEnd === null ? at : input.selectionEnd;
      const next = input.value.slice(0, at) + emoji + input.value.slice(end);
      if (next.length > input.maxLength) return;
      input.value = next;
      input.selectionStart = input.selectionEnd = at + emoji.length;
      sync();
    },
    onClose: () => input.focus(),
  });
  emojiButton.addEventListener('click', () => picker.toggle(emojiButton));
  form.append(emojiButton, input, send);
  wrap.append(list, replyBar, form);
  return {
    el: wrap,
    form,
    input,
    sendButton: send,
    emojiButton,
    picker,
    /** Writing works (or not, with another hint). */
    setEnabled(on, placeholder) {
      input.disabled = !on;
      emojiButton.disabled = !on;
      if (placeholder) input.placeholder = placeholder;
      if (!on) closeList();
      sync();
    },
    clear() {
      input.value = '';
      setReply(null);
      closeList();
      sync();
    },
    /** Answering a message: {name, text, …} (shown "Replying to <name>", one line of text), or null. */
    setReply(target) {
      setReply(target);
      if (target) input.focus();
    },
    get reply() {
      return reply;
    },
    /** Who "@" offers: [{name, avatar}], not oneself; [] (a chat with one friend) offers nobody. */
    setPeople(list_) {
      people = Array.isArray(list_) ? list_.filter((p) => p && p.name) : [];
      if (!list.hidden) updateList();
    },
    /** The list after "@" or the message being answered: closed; false if neither was there. */
    closeExtras() {
      if (!list.hidden) {
        closeList();
        return true;
      }
      if (reply) {
        setReply(null);
        return true;
      }
      return false;
    },
  };
}
