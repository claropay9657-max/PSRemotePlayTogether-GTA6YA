// Twitch alerts for OBS: the card of the app's stream alerts (qml/scenes/StreamAlert.qml),
// one at a time, with its sound. The data comes from the app (LiveAlerts) as server-sent
// events. Names and messages come from viewers: they only ever go in as text.
'use strict';

const stage = document.getElementById('stage');
let shownId = null;
let timeTimer = null;

function fit() {
  const scale = Math.min(window.innerWidth / 1920, window.innerHeight / 1080);
  stage.style.transform = `scale(${scale})`;
}
window.addEventListener('resize', fit);
fit();

function el(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined && text !== null) e.textContent = String(text);
  return e;
}

function rgba(hex, a) {
  const n = parseInt(hex.slice(1), 16);
  return `rgba(${n >> 16}, ${(n >> 8) & 255}, ${n & 255}, ${a})`;
}

function icon(name, size) {
  const i = el('img');
  i.src = `/twitch/${name}.svg`;
  i.alt = '';
  i.width = size;
  i.height = size;
  return i;
}

/** Between two colours: t = 0 the first, 1 the second. */
function mix(a, b, t) {
  const x = parseInt(a.slice(1), 16), y = parseInt(b.slice(1), 16);
  const c = (s) => Math.round(((x >> s) & 255) * (1 - t) + ((y >> s) & 255) * t);
  return `rgb(${c(16)}, ${c(8)}, ${c(0)})`;
}

/** Who it is about: the viewer's initial with the emblem as a badge, the emblem, or the hype level. */
function face(a) {
  const c1 = a.color || '#8c52ff';
  const c2 = a.color2 || '#f0469b';
  const box = el('div', 'face');
  const ring = el('div', 'ring');
  ring.style.backgroundImage = `linear-gradient(180deg, ${c1}, ${c2})`;
  const inside = el('div', 'inside ' + (a.face || 'initial'));
  if (a.face === 'emblem') {
    inside.append(icon(a.icon, 104));
  } else if (a.face === 'level') {
    inside.append(el('div', 'lvl', 'LEVEL'), el('div', 'lvln', a.level || 1));
  } else {
    inside.append(document.createTextNode(a.initial || '?'));
  }
  ring.append(inside);
  box.append(ring);
  if ((a.face || 'initial') === 'initial') {
    const badge = el('div', 'badge' + (a.icon === 'star-badge' ? ' star' : ''));
    if (a.icon !== 'star-badge') badge.style.borderColor = c1;
    badge.append(icon(a.icon, a.icon === 'star-badge' ? 84 : 50));
    if (a.icon === 'star-badge' && a.count > 0) {
      const n = el('div', 'n', a.count);
      if (a.count > 99) n.style.fontSize = '20px';
      badge.append(n);
    }
    if (a.kind === 'gift' && a.count > 0) badge.append(el('div', 'times', `×${a.count}`));
    box.append(badge);
  }
  return box;
}

/** The "2 more coming up" pill: initials (or emblems) of the next ones. */
function more(data) {
  const box = el('div', 'more');
  const who = el('div', 'heads');
  for (const n of data.next || []) {
    const dot = el('span');
    if (n.face === 'initial') {
      dot.textContent = n.initial || '';
      dot.style.background = rgba(n.color || '#cdb6ff', 0.9);
    } else {
      dot.style.background = '#120f27';
      dot.append(icon(n.icon, 20));
    }
    who.append(dot);
  }
  box.append(who, document.createTextNode(data.waiting === 1 ? '1 more coming up' : `${data.waiting} more coming up`));
  return box;
}

/** The bar that runs down while the alert is up. */
function timeBar(a, data, cls, from, to) {
  const time = el('div', cls);
  const fill = el('div');
  if (from) fill.style.backgroundImage = `linear-gradient(90deg, ${from}, ${to || from})`;
  const seconds = a.seconds || 6;
  const spent = Math.min(seconds * 1000, data.shownMs || 0);
  fill.style.width = `${100 - (spent / (seconds * 10))}%`;
  fill.style.transition = `width ${seconds * 1000 - spent}ms linear`;
  time.append(fill);
  // Starts running down once it is on the page
  requestAnimationFrame(() => requestAnimationFrame(() => { fill.style.width = '0%'; }));
  return time;
}

/** The kind's own card of "All alerts": its emblem large on top, then what happened. */
function setCard(a, data) {
  const c1 = a.color || '#8c52ff';
  const ink = a.ink || '#d8c6ff';
  const wrap = el('div', 'alert set');
  const box = el('div', 'setcard');
  box.style.borderColor = rgba(c1, 0.55);
  box.style.backgroundImage = `radial-gradient(360px 220px at 50% 0%, ${rgba(c1, a.kind === 'follow' ? 0.22 : 0.28)}, ${rgba(c1, 0)} 75%)`;
  box.style.boxShadow = `0 0 50px ${rgba(c1, 0.3)}, 0 24px 60px rgba(0,0,0,0.5)`;

  const emblem = el('div', 'emblem');
  if (a.kind === 'hype') {
    emblem.append(el('div', 'lvl', 'LEVEL'), el('div', 'lvln', a.level || 1));
    emblem.classList.add('level');
  } else {
    const big = String(a.emblem || '').endsWith('-big');
    emblem.append(icon(a.emblem, big ? 132 : 104));
    if (a.kind === 'resub') {
      const share = Math.max(1, Math.min(12, a.bars || 1)) / 12;
      const ring = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
      ring.setAttribute('class', 'ring');
      ring.setAttribute('width', '132');
      ring.setAttribute('height', '132');
      ring.setAttribute('viewBox', '0 0 120 120');
      const c = document.createElementNS(ring.namespaceURI, 'circle');
      for (const [k, v] of [['cx', 60], ['cy', 60], ['r', 54], ['fill', 'none'], ['stroke', '#a77bff'], ['stroke-width', 7], ['stroke-linecap', 'round'],
        ['stroke-dasharray', `${(339.3 * share).toFixed(1)} 340`], ['transform', 'rotate(-90 60 60)']]) c.setAttribute(k, v);
      ring.append(c);
      emblem.append(ring);
    }
    if ((a.kind === 'sub' || a.kind === 'resub') && a.count > 0) {
      const n = el('div', 'n', a.count);
      n.style.top = a.kind === 'sub' ? '46px' : '47px';
      emblem.append(n);
    }
    if (a.kind === 'gift' && a.count > 0) emblem.append(el('div', 'times', `×${a.count}`));
  }
  box.append(emblem);

  const label = el('div', 'sc-label', a.setLabel || a.label || '');
  label.style.color = ink;
  box.append(label);
  if (a.big) {
    const big = el('div', 'bigrow');
    const u = el('span', 'u', a.bigUnit || '');
    u.style.color = ink;
    big.append(el('span', 'v', a.big), u);
    box.append(big);
  } else if (a.kind !== 'hype') {
    const title = el('div', 'sc-who' + (a.titleGradient ? ' gradient' : ''), a.title || a.name || '');
    if ((a.title || a.name || '').length > 18 && !a.titleGradient) title.classList.add('long');
    box.append(title);
  }
  if (a.stat) {
    const row = el('div', 'statrow');
    const v = el('span', 'v', a.stat);
    v.style.color = mix(c1, '#ffffff', 0.35);
    const u = el('span', 'u', a.statUnit || '');
    u.style.color = ink;
    row.append(v, u);
    box.append(row);
  }
  if (typeof a.segments === 'number') {
    const steps = el('div', 'sc-steps');
    const level = a.level || 1;
    const first = Math.max(1, level - 2);
    for (let i = 0; i < 5; i++) {
      const step = first + i;
      const fill = step <= level ? 1 : (step === level + 1 ? a.segments : 0);
      const s = el('span');
      const from = mix('#f0469b', '#f5a35a', Math.min(1, i * 0.22));
      const to = mix('#f36a86', '#f5a35a', Math.min(1, (i + 1) * 0.22));
      if (fill >= 1) s.style.backgroundImage = `linear-gradient(90deg, ${from}, ${to})`;
      else if (fill > 0) s.style.backgroundImage = `linear-gradient(90deg, ${from} ${Math.round(fill * 100)}%, rgba(255,255,255,0.10) ${Math.round(fill * 100)}%)`;
      steps.append(s);
    }
    box.append(steps);
  }
  if (!a.stat && a.detail) box.append(el('div', 'sc-line' + (['bits', 'points', 'hype'].includes(a.kind) ? ' strong' : '') + (a.kind === 'bits' ? ' bits' : ''), a.detail));
  if (a.note) box.append(el('div', 'sc-note', a.note));
  box.append(timeBar(a, data, 'cardbar', a.bar1 || c1, a.bar2 || c1));
  wrap.append(box);
  if (data.waiting > 0) wrap.append(more(data));
  return wrap;
}

function card(a, data) {
  if (!a.wide) return setCard(a, data);
  const c1 = a.color || '#8c52ff';
  const c2 = a.color2 || '#f0469b';
  const ink = a.ink || '#d8c6ff';
  const wrap = el('div', 'alert');
  const frame = el('div', 'frame');
  frame.style.boxShadow = `0 0 60px ${rgba(c1, 0.45)}, 0 30px 80px rgba(0,0,0,0.55)`;
  const inner = el('div', 'inner');
  inner.style.backgroundImage = `radial-gradient(700px 300px at 18% 0%, ${rgba(c1, 0.30)}, ${rgba(c1, 0)} 70%), radial-gradient(600px 260px at 100% 100%, ${rgba(c2, 0.18)}, ${rgba(c2, 0)} 70%)`;
  inner.append(el('div', 'rings'));

  const top = el('div', 'top');
  const words = el('div', 'words');
  const labelRow = el('div', 'labelrow');
  const kind = el('div', 'kind', a.label || '');
  kind.style.color = ink;
  labelRow.append(kind);
  if (a.chip) {
    const chip = el('div', 'chiprow', a.chip);
    chip.style.background = rgba(c1, 0.22);
    chip.style.border = `1px solid ${rgba(c1, 0.6)}`;
    labelRow.append(chip);
  }
  words.append(labelRow);

  if (a.big) {
    const big = el('div', 'big');
    const u = el('div', 'u', a.bigUnit || '');
    u.style.color = ink;
    big.append(el('div', 'v', a.big), u);
    words.append(big);
  } else {
    const title = el('div', 'title' + (a.titleGradient ? ' gradient' : ''), a.title || a.name || '');
    if ((a.title || '').length > 28) title.classList.add('long');
    words.append(title);
  }

  const detailRow = el('div', 'detailrow');
  if (a.stat) {
    const v = el('div', 'stat', a.stat);
    v.style.color = mix(c1, '#ffffff', 0.35);
    const u = el('div', 'statunit', a.statUnit || '');
    u.style.color = ink;
    detailRow.append(v, u);
  } else if (a.detail) {
    detailRow.append(el('div', 'detail' + (a.bars > 0 ? ' short' : ''), a.detail));
  }
  if (a.bars > 0) {
    const bars = el('div', 'bars');
    for (let i = 0; i < 12; i++) {
      const b = el('span');
      if (i < a.bars) b.style.background = mix('#c4a6ff', '#f0469b', i / 12);
      bars.append(b);
    }
    detailRow.append(bars);
  }
  if (typeof a.progress === 'number' && a.progress >= 0) {
    const p = el('div', 'prog');
    const f = el('div');
    f.style.width = `${Math.max(6, Math.round(a.progress * 100))}%`;
    f.style.backgroundImage = `linear-gradient(90deg, ${c1}, ${c2})`;
    p.append(f);
    detailRow.append(p);
  }
  if (detailRow.childNodes.length) words.append(detailRow);
  if (typeof a.segments === 'number') {
    const steps = el('div', 'steps');
    const level = a.level || 1;
    const first = Math.max(1, level - 2);
    for (let i = 0; i < 5; i++) {
      const step = first + i;
      const fill = step <= level ? 1 : (step === level + 1 ? a.segments : 0);
      const s = el('span');
      const from = mix('#f0469b', '#f5a35a', Math.min(1, i * 0.22));
      const to = mix('#f36a86', '#f5a35a', Math.min(1, (i + 1) * 0.22));
      if (fill >= 1) s.style.backgroundImage = `linear-gradient(90deg, ${from}, ${to})`;
      else if (fill > 0) s.style.backgroundImage = `linear-gradient(90deg, ${from} ${Math.round(fill * 100)}%, rgba(255,255,255,0.10) ${Math.round(fill * 100)}%)`;
      steps.append(s);
    }
    words.append(steps);
  }
  if (a.note) words.append(el('div', 'note', a.note));
  top.append(face(a), words);
  inner.append(top);

  if (a.message) {
    const say = el('div', 'say' + (a.messageTint ? ' tint' : ''));
    say.append(el('div', 'tip'), el('div', 'text', a.message));
    inner.append(say);
  }

  inner.append(timeBar(a, data, 'timebar'));

  frame.append(inner);
  wrap.append(frame);
  if (data.waiting > 0) wrap.append(more(data));
  return wrap;
}

/** The sparkles of the design around the card (drawn by the app: no viewer text in here). */
function sparkles() {
  const box = el('div', 'sparkles');
  box.innerHTML = '<svg width="1920" height="620" viewBox="0 0 1920 620" aria-hidden="true">'
    + '<path d="M430 120 l7 18 l18 7 l-18 7 l-7 18 l-7 -18 l-18 -7 l18 -7 z" fill="#ffd6e8" opacity="0.95"/>'
    + '<path d="M470 330 l4 10 l10 4 l-10 4 l-4 10 l-4 -10 l-10 -4 l10 -4 z" fill="#b8f9ff" opacity="0.8"/>'
    + '<path d="M1488 96 l9 22 l22 9 l-22 9 l-9 22 l-9 -22 l-22 -9 l22 -9 z" fill="#efe7ff" opacity="0.95"/>'
    + '<path d="M1530 300 l5 12 l12 5 l-12 5 l-5 12 l-5 -12 l-12 -5 l12 -5 z" fill="#fde4a8" opacity="0.85"/>'
    + '<path d="M1420 420 l3 8 l8 3 l-8 3 l-3 8 l-3 -8 l-8 -3 l8 -3 z" fill="#ffd6e8" opacity="0.7"/>'
    + '<path d="M520 450 l3 7 l7 3 l-7 3 l-3 7 l-3 -7 l-7 -3 l7 -3 z" fill="#efe7ff" opacity="0.6"/>'
    + '</svg>';
  return box;
}

function render(data) {
  const a = data.current || {};
  if (!a.kind) {
    stage.classList.remove('up');
    shownId = null;
    clearTimeout(timeTimer);
    timeTimer = setTimeout(() => { if (!shownId) stage.replaceChildren(); }, 400);
    return;
  }
  const fresh = a.id !== shownId;
  shownId = a.id;
  clearTimeout(timeTimer);
  stage.replaceChildren(el('div', 'shade'), sparkles(), card(a, data));
  if (fresh) {
    stage.classList.remove('up');
    requestAnimationFrame(() => requestAnimationFrame(() => stage.classList.add('up')));
    if (data.volume > 0 && (data.shownMs || 0) < 1500) play(a.kind, data.volume, a.id);
  } else {
    stage.classList.add('up');
  }
}

/** A sound of the app at a level 0..100 (the same curve as in the app: half way is a quarter). */
function play(kind, level, tag) {
  const sound = new Audio(`/alerts/sound/${encodeURIComponent(kind)}?v=${encodeURIComponent(tag)}`);
  const v = Math.max(0, Math.min(1, level / 100));
  sound.volume = v * v;
  sound.play().catch(() => {});
}

// Chat sounds the streamer wants on the stream: each new one once, none from before the page came
let chatSounds = null;
function chatSound(data) {
  const n = data.chatSounds || 0;
  if (chatSounds !== null && n > chatSounds && data.chatVolume > 0) play('chat', data.chatVolume, n);
  chatSounds = n;
}

function receive(json) {
  try {
    const data = JSON.parse(json);
    chatSound(data);
    render(data);
  } catch (e) {
    // a broken message: wait for the next
  }
}

fetch('/alerts/data').then((r) => r.text()).then(receive).catch(() => {});
const events = new EventSource('/events?alerts=1');
events.addEventListener('alert', (e) => receive(e.data));
