// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// What both chats of the page show beside the text, worked out the app's way: the reactions
// under a message as chips (emoji, count, whether one's own is among them) and whose pictures
// go under which message (as Instagram shows who has read how far). A session's chat
// (gui/src/qml/RoomChat.qml) has it from the host's marks, by slot; a friends' chat
// (gui/src/qml/FriendsPanel.qml) from the friends client, by fingerprint. No DOM here: the
// message itself is drawn by ./chatview.js.

/** The quick reactions over a message (ChatBubble.qml), then "+" for all emoji. */
export const QUICK_REACTIONS = ['❤️', '😂', '👍', '😮', '😢', '🔥'];

/** The emoji to choose from, by groups (EmojiPicker.qml): for writing and for reacting. */
export const EMOJI_GROUPS = [
  ['😀', '😀 😃 😄 😁 😆 😅 😂 🤣 🙂 🙃 😉 😊 😇 🥰 😍 🤩 😘 😋 😛 😜 🤪 🤗 🤭 🤫 🤔 🤐 🤨 😐 😑 😶 😏 😒 🙄 😬 😌 😔 😪 😴 😷 🤒 🥵 🥶 🥴 😵 🤯 🤠 🥳 😎 🤓 🧐 😕 😟 🙁 😮 😯 😲 😳 🥺 😦 😨 😰 😥 😢 😭 😱 😖 😣 😞 😓 😩 😫 🥱 😤 😡 😠 🤬 😈 💀 ☠️ 💩 🤡 👻 👽 🤖'],
  ['👍', '👍 👎 👌 🤌 🤏 ✌️ 🤞 🤟 🤘 🤙 👈 👉 👆 👇 ☝️ 👋 🤚 🖐️ ✋ 👏 🙌 👐 🤲 🙏 ✍️ 💪 🦾 🫡 🤝 👀 🧠 🫶'],
  ['❤️', '❤️ 🧡 💛 💚 💙 💜 🖤 🤍 🤎 💔 ❤️‍🔥 💕 💞 💓 💗 💖 💘 💝 💯 💢 💥 💫 💦 💨 💤 ✨ ⭐ 🌟 🔥 ⚡ 🌈'],
  ['🎮', '🎮 🕹️ 👾 🏆 🥇 🥈 🥉 🏅 🎯 🎲 ♟️ 🧩 🎉 🎊 🎁 🎈 🍕 🍔 🍟 🌮 🍿 🍩 🍪 🍺 🍻 🥤 ☕ 🚗 🏎️ 🚀 ✈️ 🛸 ⚔️ 🛡️ 🏹 🔫 💣 🗡️ 🪓 💰 💎 🔑 🗺️'],
  ['✅', '✅ ❌ ❓ ❗ ‼️ ⁉️ ⚠️ 🚫 ⛔ 🔴 🟠 🟡 🟢 🔵 🟣 ⚫ ⚪ ⬆️ ⬇️ ⬅️ ➡️ 🔄 ⏸️ ▶️ ⏩ ⏪ 🆗 🆒 🆕 🆙 🔝 💬 💭 🗯️ 📢 🔔 🎵 🎶 ⏰ ⌛'],
];

/** At most this many pictures under a message, then "+N". */
export const MAX_READER_PICTURES = 5;

/** Code point order: how the app's std::map sorts the emoji of a session's message (UTF-8 bytes). */
export function byCodePoint(a, b) {
  const x = Array.from(a, (c) => c.codePointAt(0));
  const y = Array.from(b, (c) => c.codePointAt(0));
  for (let i = 0; i < Math.min(x.length, y.length); i++) {
    if (x[i] !== y[i]) return x[i] - y[i];
  }
  return x.length - y.length;
}

/** UTF-16 order: how the app's QJsonObject sorts the emoji of a friends' chat message. */
export function byUtf16(a, b) {
  return a < b ? -1 : a > b ? 1 : 0;
}

/**
 * A session's message's reactions as chips: {emoji, count, mine}.
 * @param {{reactions: Map<number, Map<string, number[]>>}} marks the host's marks (proto.js)
 * @param {number} seq the message, by the host's numbering (0: none yet)
 * @param {number} ownSlot
 */
export function roomReactions(marks, seq, ownSlot) {
  const kinds = seq && marks ? marks.reactions.get(seq) : null;
  if (!kinds) return [];
  return [...kinds.entries()].filter(([, slots]) => slots.length > 0).sort(([a], [b]) => byCodePoint(a, b))
    .map(([emoji, slots]) => ({ emoji, count: slots.length, mine: slots.includes(ownSlot) }));
}

/**
 * A friends' chat message's reactions ({emoji: [fingerprints]}) as chips: {emoji, count, mine}.
 * @param {object} reactions
 * @param {string} me the own fingerprint
 */
export function friendReactions(reactions, me) {
  if (!reactions || typeof reactions !== 'object') return [];
  return Object.keys(reactions).sort(byUtf16).map((emoji) => {
    const who = Array.isArray(reactions[emoji]) ? reactions[emoji] : [];
    return { emoji, count: who.length, mine: who.includes(me) };
  }).filter((r) => r.count > 0);
}

/**
 * Who has read how far in a session's chat, as RoomChat.qml places it: each other player's
 * picture under the newest message they have read up to (or written themselves), but not under
 * their own message; nothing for oneself, nothing for players no longer there.
 * @param {{slot: number, seq: number, pending?: boolean}[]} messages oldest first
 * @param {Map<number, number>} read slot -> read up to this message
 * @param {{slot: number, name: string, avatar: string, you: boolean}[]} players
 * @returns {Map<number, {name: string, avatar: string}[]>} by the message's place in the list
 */
export function placeRoomReaders(messages, read, players) {
  const out = new Map();
  const slots = [...read.keys()].sort((a, b) => a - b);
  for (const slot of slots) {
    const player = players.find((p) => p.slot === slot);
    if (!player || player.you) continue;
    const upTo = read.get(slot);
    for (let i = messages.length - 1; i >= 0; i--) {
      const m = messages[i];
      if (m.pending || !m.seq) continue;
      if (m.slot === slot || m.seq <= upTo) {
        if (m.slot !== slot) {
          if (!out.has(i)) out.set(i, []);
          out.get(i).push({ name: player.name, avatar: player.avatar || '' });
        }
        break;
      }
    }
  }
  return out;
}

/**
 * The same in a friends' chat (FriendsPanel.qml placeReaders): each reader's picture under the
 * newest message they have seen (by its time) or written; left out under their own message.
 * @param {{from: string, at: number, system?: boolean}[]} messages oldest first
 * @param {{fp: string, name: string, avatar: string, at: number}[]} readers the client's readers(chat)
 * @returns {Map<number, {name: string, avatar: string}[]>}
 */
export function placeFriendReaders(messages, readers) {
  const out = new Map();
  for (const reader of readers) {
    for (let i = messages.length - 1; i >= 0; i--) {
      const m = messages[i];
      if (m.system) continue;
      if (m.from === reader.fp || m.at <= reader.at) {
        if (m.from !== reader.fp) {
          if (!out.has(i)) out.set(i, []);
          out.get(i).push({ name: reader.name, avatar: reader.avatar || '' });
        }
        break;
      }
    }
  }
  return out;
}

/** A session's message starts a run (its writer's picture and name show), as RoomChat.qml says. */
export function roomRunStarts(messages, index) {
  const m = messages[index];
  const previous = index > 0 ? messages[index - 1] : null;
  return !(previous && previous.slot === m.slot && previous.name === m.name && m.timeMs - previous.timeMs < 120000);
}

/** A friends' chat message starts a run, as FriendsPanel.qml says. */
export function friendRunStarts(messages, index) {
  const m = messages[index];
  const previous = index > 0 ? messages[index - 1] : null;
  return !!m.system || !(previous && previous.from === m.from && !previous.system);
}

// ------------------------------------------------------------------ mentions

function escapeRegExp(text) {
  return text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

/** Before "@": the start, a space or punctuation (docs/friends-protocol.md, "Mentions"). */
const MENTION_BEFORE = '(^|[\\s\\p{P}])';
/** After the name: anything but a letter or a digit. */
const MENTION_AFTER = '(?![\\p{L}\\p{N}])';

/**
 * Whether a message mentions a player: "@" and their own name, in any case, at the start or
 * after a space or punctuation, and not followed by a letter or digit.
 * @param {string} text
 * @param {string} name the player's own name (in a friends' chat the profile's, in a session the player's)
 */
export function mentions(text, name) {
  const n = String(name ?? '').trim();
  if (!n) return false;
  return new RegExp(`${MENTION_BEFORE}@${escapeRegExp(n)}${MENTION_AFTER}`, 'iu').test(String(text ?? ''));
}

/**
 * Text cut where "@" and one of the names stands, as mentions() finds them: [{text, name}],
 * name being the name as listed for a mention and '' for plain text. Longer names first, so
 * that "@Ann Marie" is not taken for "@Ann".
 * @param {string} text
 * @param {string[]} names
 */
export function splitMentions(text, names) {
  const s = String(text ?? '');
  const list = [...new Set((names || []).map((n) => String(n ?? '').trim()).filter(Boolean))].sort((a, b) => b.length - a.length);
  if (!list.length || !s.includes('@')) return s ? [{ text: s, name: '' }] : [];
  const re = new RegExp(`${MENTION_BEFORE}@(${list.map(escapeRegExp).join('|')})${MENTION_AFTER}`, 'giu');
  const out = [];
  let last = 0;
  for (let m = re.exec(s); m; m = re.exec(s)) {
    const start = m.index + m[1].length;
    if (start > last) out.push({ text: s.slice(last, start), name: '' });
    const typed = m[2];
    const name = list.find((n) => n.toLowerCase() === typed.toLowerCase()) || typed;
    out.push({ text: s.slice(start, start + 1 + typed.length), name });
    last = start + 1 + typed.length;
    // The character before the next "@" may be the one this match ended on
    re.lastIndex = last;
  }
  if (last < s.length) out.push({ text: s.slice(last), name: '' });
  return out;
}

/**
 * What is being typed after "@" just before the caret, for the list of names to pick from:
 * {at, query} (where the "@" is, and what follows it), or null.
 * @param {string} value the field's text
 * @param {number} caret
 */
export function mentionQuery(value, caret) {
  const before = String(value ?? '').slice(0, caret);
  const at = before.lastIndexOf('@');
  if (at < 0) return null;
  if (at > 0 && !/[\s\p{P}]/u.test(before[at - 1])) return null;
  const query = before.slice(at + 1);
  // A name may have a space in it, but not a line, and not more than a name has
  if (/[\n\r]/.test(query) || query.length > 32 || /\s{2}/.test(query)) return null;
  return { at, query };
}

/** Who the list offers for what is typed: names that start with it first, then those that have it. */
export function mentionChoices(people, query) {
  const q = String(query ?? '').toLowerCase();
  const starts = [];
  const has = [];
  for (const p of people || []) {
    const n = String(p.name ?? '').toLowerCase();
    if (!n) continue;
    if (n.startsWith(q)) starts.push(p);
    else if (q && n.includes(q)) has.push(p);
  }
  return [...starts, ...has];
}

// ------------------------------------------------------------------ replies

/** The start of a message on one line, at most this many bytes of UTF-8 (the quote in a reply). */
export const QUOTE_BYTES = 160;

/** Text on one line and cut to bytes of UTF-8 at a character's start. */
export function oneLine(text, maxBytes = QUOTE_BYTES) {
  const s = String(text ?? '').replace(/\s+/g, ' ').trim();
  const bytes = new TextEncoder().encode(s);
  if (bytes.length <= maxBytes) return s;
  let end = maxBytes;
  while (end > 0 && (bytes[end] & 0xc0) === 0x80) end--;
  return new TextDecoder().decode(bytes.subarray(0, end));
}
