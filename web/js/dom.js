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

export function setOptions(s, options) {
  const keep = s.value;
  s.replaceChildren(...options.map((o, i) => {
    const opt = el('option', '', Array.isArray(o) ? o[1] : o);
    opt.value = String(Array.isArray(o) ? o[0] : i);
    return opt;
  }));
  if (keep !== '') s.value = keep;
}
