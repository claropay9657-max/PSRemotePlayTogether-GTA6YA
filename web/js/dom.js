// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Making elements, for everything the page builds while a game runs.

/**
 * @param {string} tag
 * @param {string} [cls]
 * @param {string} [text]
 * @returns {HTMLElement}
 */
export function el(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
}

/** A button that does not submit anything. */
export function button(cls, text, onClick) {
  const b = el('button', cls, text);
  b.type = 'button';
  if (onClick) b.addEventListener('click', onClick);
  return b;
}

/** A choice of several; `options` are [value, label] pairs or plain labels (then the value is the index). */
export function select(cls, options, onChange) {
  const s = el('select', cls);
  setOptions(s, options);
  if (onChange) s.addEventListener('change', () => onChange(s.value, s.selectedIndex));
  return s;
}

/** The text as it is; only addresses that start with http(s) become links. */
export function appendWithLinks(parent, text) {
  const pattern = /https?:\/\/[^\s<>"']+/g;
  let last = 0;
  for (const match of text.matchAll(pattern)) {
    if (match.index > last) parent.append(text.slice(last, match.index));
    // What ends a sentence is not part of the address, as in the app
    const address = match[0].replace(/[.,;:!?)\]}]+$/, '');
    if (!address.includes('://') || address.endsWith('://')) continue;
    const a = el('a', '', address);
    a.href = address;
    a.target = '_blank';
    a.rel = 'noopener noreferrer';
    parent.append(a);
    last = match.index + address.length;
  }
  if (last < text.length) parent.append(text.slice(last));
}

export function setOptions(s, options) {
  const keep = s.value;
  s.replaceChildren(...options.map((o, i) => {
    const opt = el('option', '', Array.isArray(o) ? o[1] : o);
    opt.value = String(Array.isArray(o) ? o[0] : i);
    return opt;
  }));
  if (keep !== '') s.value = keep;
}
