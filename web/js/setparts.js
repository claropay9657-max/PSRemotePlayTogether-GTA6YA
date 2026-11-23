// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The pieces the app's settings pages are made of (gui/src/qml/controls: SettingsGroup,
// SettingsRow, Toggle, Segmented, ValueSlider, PillButton, PopupCard), for the windows of
// this page that are one of the app's settings pages: the same sizes, colours and behaviour,
// so that whoever knows the app finds the same thing here.

import { button, el } from './dom.js';
import { icon } from './menu.js';

/** The small capital heading over a group. */
export const label = (text) => el('h3', 'set-label', text);

/** A group of settings: a heading and its rows on one card, a thin line between two rows. */
export function group(title, rows) {
  const g = el('section', 'set-group');
  if (title) g.append(label(title));
  const card = el('div', 'set-card');
  card.append(...rows);
  g.append(card);
  g.card = card;
  return g;
}

/**
 * One setting: what it is and what it does on the left, its control on the right; where the
 * row is too narrow for both, the control goes under the text.
 * @param {{title: string, subtitle?: string, sub?: boolean, textMin?: number, controls?: Node[], switch?: HTMLInputElement}} o
 *   sub: belongs to the row above (set in); switch: an on/off switch, which a click on the text flips too
 */
export function row(o) {
  const r = el(o.switch ? 'label' : 'div', 'set-row' + (o.sub ? ' sub' : ''));
  const text = el('span', 'set-text');
  if (o.textMin) text.style.setProperty('--text-min', o.textMin + 'px');
  r.title_ = el('b', '', o.title);
  r.subtitle = el('small', '', o.subtitle || '');
  r.subtitle.hidden = !o.subtitle;
  text.append(r.title_, r.subtitle);
  const controls = el('span', 'set-controls');
  controls.append(...(o.switch ? [o.switch] : o.controls || []));
  r.append(text, controls);
  /** Greyed out and out of reach, with the row it belongs to. */
  r.setEnabled = (on) => {
    r.classList.toggle('off', !on);
    r.inert = !on;
  };
  r.setSubtitle = (words) => {
    r.subtitle.textContent = words;
    r.subtitle.hidden = !words;
  };
  return r;
}

/** An on/off switch: purple when on. */
export function toggle(name, on, onChange) {
  const input = el('input', 'switch');
  input.type = 'checkbox';
  input.setAttribute('role', 'switch');
  input.setAttribute('aria-label', name);
  input.checked = !!on;
  input.addEventListener('change', () => onChange(input.checked));
  return input;
}

/** A few choices side by side, one of them chosen; the arrow keys go from one to the next. */
export function segmented(name, labels, chosen, onChoose) {
  const seg = el('span', 'set-seg');
  seg.setAttribute('role', 'radiogroup');
  seg.setAttribute('aria-label', name);
  const buttons = labels.map((text, index) => {
    const b = button('', text, () => onChoose(index));
    b.setAttribute('role', 'radio');
    b.addEventListener('keydown', (e) => {
      const to = e.key === 'ArrowLeft' ? index - 1 : e.key === 'ArrowRight' ? index + 1 : -1;
      if (to < 0 || to >= buttons.length) return;
      e.preventDefault();
      buttons[to].focus();
    });
    return b;
  });
  seg.append(...buttons);
  seg.set = (index) => buttons.forEach((b, i) => {
    b.classList.toggle('chosen', i === index);
    b.setAttribute('aria-checked', i === index ? 'true' : 'false');
  });
  seg.set(chosen);
  return seg;
}

/** A value on a line, with the number beside it. `format` writes the number. */
export function slider(name, { min, max, step, value, width = 230, valueWidth = 52, format }, onMove) {
  const wrap = el('span', 'set-slider');
  wrap.style.width = width + 'px';
  const input = el('input');
  input.type = 'range';
  input.min = String(min);
  input.max = String(max);
  input.step = String(step);
  input.value = String(value);
  input.setAttribute('aria-label', name);
  const shown = el('output', '', '');
  shown.style.width = valueWidth + 'px';
  const draw = () => {
    const v = Number(input.value);
    // How much of the line is behind the handle, for the colour on it
    input.style.setProperty('--at', ((v - min) / (max - min)) * 100 + '%');
    shown.textContent = format(v);
    input.setAttribute('aria-valuetext', shown.textContent);
  };
  input.addEventListener('input', () => {
    draw();
    onMove(Number(input.value));
  });
  wrap.append(input, shown);
  wrap.set = (v) => {
    input.value = String(v);
    draw();
  };
  wrap.setEnabled = (on) => {
    input.disabled = !on;
    wrap.classList.toggle('off', !on);
  };
  draw();
  return wrap;
}

/** A rounded button: "primary" starts something, "ghost" is everything else. */
export function pill(kind, text, iconName, onClick, { small = true, iconAfter = false } = {}) {
  const b = button(`pill ${kind}${small ? ' small' : ''}`, '', onClick);
  b.set = (newKind, newText, newIcon) => {
    b.className = `pill ${newKind}${small ? ' small' : ''}`;
    const parts = [];
    if (newIcon) parts.push(icon(newIcon, small ? 15 : 18));
    if (newText) parts.push(el('span', '', newText));
    b.replaceChildren(...(iconAfter ? parts.reverse() : parts));
  };
  b.set(kind, text, iconName);
  return b;
}

/** A question in front of everything, as the app asks it: Yes or No. Resolves to the answer. */
export function ask(title, text) {
  return new Promise((resolve) => {
    const d = el('dialog', 'dialog ask-card');
    const no = button('pill ghost', 'No', () => d.close('no'));
    const yes = button('pill primary', 'Yes', () => d.close('yes'));
    const buttons = el('div', 'ask-card-buttons');
    buttons.append(no, yes);
    d.append(el('h2', '', title), el('p', '', text), buttons);
    d.addEventListener('close', () => {
      d.remove();
      resolve(d.returnValue === 'yes');
    });
    document.body.append(d);
    d.showModal();
    yes.focus();
  });
}

/** A small menu under a button: `entries` are [text, onClick]. Closes on a choice, a click elsewhere or Escape. */
export function menuUnder(anchor, entries) {
  const m = el('div', 'set-menu');
  m.setAttribute('role', 'menu');
  const close = () => {
    m.remove();
    document.removeEventListener('pointerdown', outside, true);
    document.removeEventListener('keydown', keys, true);
    anchor.setAttribute('aria-expanded', 'false');
  };
  const outside = (e) => {
    if (!m.contains(e.target)) close();
  };
  const items = entries.map(([text, onClick]) => {
    const b = button('', text, () => {
      close();
      onClick();
    });
    b.setAttribute('role', 'menuitem');
    return b;
  });
  const keys = (e) => {
    if (e.key === 'Escape') {
      e.preventDefault();
      e.stopPropagation();
      close();
      anchor.focus();
    } else if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
      e.preventDefault();
      const at = items.indexOf(document.activeElement);
      items[(at + (e.key === 'ArrowDown' ? 1 : items.length - 1)) % items.length].focus();
    }
  };
  m.append(...items);
  anchor.after(m);
  anchor.setAttribute('aria-expanded', 'true');
  document.addEventListener('pointerdown', outside, true);
  document.addEventListener('keydown', keys, true);
  items[0].focus();
  return close;
}
