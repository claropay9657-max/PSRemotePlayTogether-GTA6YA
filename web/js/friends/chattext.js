// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Chat text as the app shows it (gui/src/chattext.cpp): :shortcodes: and faces become emoji
// before a message goes out, and a message is shown as escaped text with its links clickable and
// its line breaks kept. The same tables as the session chat's in web/js/app.js.

/** The :shortcodes: the app turns into emoji. */
export const SHORTCODES = {
  ':smile:': '😄', ':joy:': '😂', ':wink:': '😉', ':heart:': '❤️', ':fire:': '🔥', ':thumbsup:': '👍', ':+1:': '👍',
  ':thumbsdown:': '👎', ':clap:': '👏', ':gg:': '🎮', ':skull:': '💀', ':eyes:': '👀', ':cry:': '😢', ':rage:': '😡',
  ':party:': '🥳', ':trophy:': '🏆', ':100:': '💯', ':rocket:': '🚀', ':ok:': '👌', ':wave:': '👋',
  ':thinking:': '🤔', ':sweat:': '😅', ':cool:': '😎', ':pray:': '🙏',
};

const SHORTCODE_PATTERNS = Object.entries(SHORTCODES)
  .map(([code, emoji]) => [new RegExp(code.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'gi'), emoji]);

// Faces only where they stand on their own, so that links and times stay as they are
const FACES = [
  [/(^|\s):-?\)(?=\s|$)/g, '$1🙂'], [/(^|\s):-?D(?=\s|$)/g, '$1😄'], [/(^|\s);-?\)(?=\s|$)/g, '$1😉'],
  [/(^|\s):-?\((?=\s|$)/g, '$1🙁'], [/(^|\s):-?[pP](?=\s|$)/g, '$1😛'], [/(^|\s)<3(?=\s|$)/g, '$1❤️'],
  [/(^|\s)[xX]D(?=\s|$)/g, '$1😆'],
];

/** :shortcodes: (in any case) and a few faces typed as text become emoji. */
export function withEmoji(text) {
  let out = String(text);
  for (const [pattern, emoji] of SHORTCODE_PATTERNS) out = out.replace(pattern, emoji);
  for (const [pattern, face] of FACES) out = out.replace(pattern, face);
  return out;
}

/** & < > " as entities, as Qt's toHtmlEscaped does. */
export function escapeHtml(text) {
  return String(text).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

/**
 * The text as HTML: escaped, http(s) links as links (without the punctuation that ends a
 * sentence), line breaks as <br>. Links open in a new tab: the page itself must stay.
 */
export function chatHtml(text) {
  const s = String(text);
  const link = /https?:\/\/[^\s<>"']+/gi;
  let out = '';
  let last = 0;
  for (let m = link.exec(s); m; m = link.exec(s)) {
    let url = m[0];
    while (url && '.,;:!?)]}'.includes(url[url.length - 1])) url = url.slice(0, -1);
    out += escapeHtml(s.slice(last, m.index));
    out += `<a href="${escapeHtml(url)}" target="_blank" rel="noopener noreferrer">${escapeHtml(url)}</a>`;
    last = m.index + url.length;
  }
  out += escapeHtml(s.slice(last));
  return out.replace(/\n/g, '<br>');
}
