// The Twitch elements on the OBS overlay page: the same elements, places and looks as in the
// app (qml/scenes/TwitchLayer.qml), on a 1920 x 1080 stage scaled to the browser source. The
// data comes from the app (TwitchElements) as the "twitch" event of /events, which overlay.js
// hands over through window.psrptTwitch. Names and messages are viewers' text: they only
// ever go in as text, never as markup.
(function () {
  'use strict';

  const params = new URLSearchParams(window.location.search);
  const editing = params.get('edit') === '1';
  const demo = params.get('demo') === '1';

  const stage = document.createElement('div');
  stage.id = 'twitch';
  document.body.append(stage);
  const guideX = el('div', 'guide x');
  const guideY = el('div', 'guide y');
  const where = el('div', 'where');

  let page = { look: 'glass', joined: true, places: {}, active: false, data: {}, sample: {} };
  let now = Date.now();
  const boxes = new Map(); // id -> element on the stage
  let dragging = null;

  function el(tag, cls, text) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined && text !== null) e.textContent = String(text);
    return e;
  }
  function icon(name, cls) {
    const i = el('img', cls || '');
    i.src = `/twitch/${name}.svg`;
    i.alt = '';
    return i;
  }
  function num(n) {
    return Number(n || 0).toLocaleString('en-US', { maximumFractionDigits: 0 });
  }
  function compact(n) {
    n = Number(n || 0);
    if (n >= 1e6) return (n / 1e6).toFixed(1) + 'M';
    if (n >= 1e4) return (n / 1e3).toFixed(1) + 'K';
    return num(n);
  }
  function clock(ms) {
    const s = Math.max(0, Math.floor(ms / 1000));
    const h = Math.floor(s / 3600), m = Math.floor(s / 60) % 60, sec = s % 60;
    const two = (v) => (v < 10 ? '0' : '') + v;
    return h > 0 ? `${h}:${two(m)}:${two(sec)}` : `${m}:${two(sec)}`;
  }
  function ago(ms) {
    const s = Math.max(0, Math.floor(ms / 1000));
    if (s < 60) return ['now', ''];
    if (s < 3600) return [`${Math.floor(s / 60)} min`, 'ago'];
    if (s < 86400) return [`${Math.floor(s / 3600)} h`, 'ago'];
    return [`${Math.floor(s / 86400)} d`, 'ago'];
  }
  /** Between two colours: t = 0 the first, 1 the second. */
  function mix(a, b, t) {
    const x = parseInt(a.slice(1), 16), y = parseInt(b.slice(1), 16);
    const c = (s) => Math.round(((x >> s) & 255) * (1 - t) + ((y >> s) & 255) * t);
    return `rgb(${c(16)}, ${c(8)}, ${c(0)})`;
  }
  function rgba(hex, a) {
    const n = parseInt(hex.slice(1), 16);
    return `rgba(${n >> 16}, ${(n >> 8) & 255}, ${n & 255}, ${a})`;
  }

  // ------------------------------------------------------------------ data

  const has = (v) => v !== undefined && v !== null && !(Array.isArray(v) && v.length === 0);
  const showSample = () => editing || demo;
  function pick(key) {
    const v = (page.data || {})[key];
    if (has(v)) return { v, fake: false };
    return showSample() ? { v: (page.sample || {})[key], fake: true } : { v: undefined, fake: false };
  }
  const until = (t, fake) => (fake ? t : t - now);

  // ------------------------------------------------------------------ the elements

  function latest(o) {
    const box = el('div', 'latest box' + (o.empty ? ' empty' : ''));
    if (o.empty) box.style.color = rgba(o.tint, 0.6);
    const tile = el('div', 'tile');
    tile.style.background = rgba(o.tint, o.empty ? 0.10 : 0.18);
    tile.append(icon(o.icon));
    const mid = el('div', 'mid');
    const caps = el('div', 'caps', o.label);
    caps.style.color = o.labelColor;
    mid.append(caps, el('div', 'name ell', o.empty ? o.emptyText : o.name));
    box.append(tile, mid);
    if (!o.empty) {
      const right = el('div', 'right');
      const value = el('div', 'value', o.value);
      value.style.color = o.valueColor || '#ffffff';
      right.append(value, el('div', 'unit', o.unit));
      box.append(right);
    }
    return box;
  }

  function goal(o) {
    const box = el('div', 'goal box');
    const head = el('div', 'head');
    const caps = el('div', 'caps l', o.label);
    caps.style.color = o.labelColor;
    const amount = el('div', 'amount num', o.amount + ' ');
    if (o.target) amount.append(el('span', '', '/ ' + o.target));
    head.append(icon(o.icon), caps, el('div', 'grow'), amount);
    const bar = el('div', 'bar');
    const fill = el('div');
    fill.style.width = Math.round(Math.max(0, Math.min(1, o.value)) * 100) + '%';
    fill.style.backgroundImage = `linear-gradient(90deg, ${o.from}, ${o.to})`;
    fill.style.boxShadow = `0 0 16px ${rgba(o.to, 0.6)}`;
    bar.append(fill);
    box.append(head, bar, el('div', 'text', o.text));
    return box;
  }

  function goalText(d) {
    const left = Math.max(0, (d.target || 0) - (d.current || 0));
    const what = left > 0 ? `${num(left)} to go` : 'Made it – thank you!';
    return d.text ? `${what} – ${d.text}` : what;
  }

  const BUILD = {
    follower() {
      const d = (page.data || {}).follower;
      const when = d ? ago(now - d.at) : ['', ''];
      return { has: !!d, node: latest({ icon: 'heart', tint: '#00f2ff', labelColor: '#8ff7ff', label: 'Latest follower', name: d ? d.name : '',
        value: when[0], unit: when[1], empty: !d, emptyText: 'shows up with your first follower' }) };
    },
    subscriber() {
      const d = (page.data || {}).subscriber;
      return { has: !!d, node: latest({ icon: 'star', tint: '#a77bff', labelColor: '#d8c6ff', label: 'Latest subscriber', name: d ? d.name : '',
        value: `${d ? d.months || 1 : 1} mo`, valueColor: '#a77bff', unit: d ? d.tier : '', empty: !d, emptyText: 'shows up with your first sub' }) };
    },
    gift() {
      const d = (page.data || {}).gift;
      return { has: !!d, node: latest({ icon: 'gift', tint: '#f0469b', labelColor: '#ffc3dd', label: 'Latest gift subs', name: d ? d.name : '',
        value: `×${d ? d.count || 1 : 1}`, valueColor: '#f0469b', unit: d && d.total > 0 ? `${num(d.total)} in total` : '', empty: !d,
        emptyText: 'shows up with your first gift subs' }) };
    },
    bits() {
      const d = (page.data || {}).bits;
      return { has: !!d, node: latest({ icon: 'gem', tint: '#f5b942', labelColor: '#fde4a8', label: 'Latest bits', name: d ? d.name : '',
        value: d ? num(d.bits) : '', valueColor: '#f5b942', unit: d && d.bits === 1 ? 'bit' : 'bits', empty: !d, emptyText: 'shows up with your first cheer' }) };
    },
    raid() {
      const d = (page.data || {}).raid;
      return { has: !!d, node: latest({ icon: 'raid', tint: '#34d399', labelColor: '#c6f6e2', label: 'Latest raid', name: d ? d.name : '',
        value: d ? num(d.viewers) : '', valueColor: '#34d399', unit: d && d.viewers === 1 ? 'viewer' : 'viewers', empty: !d, emptyText: 'shows up with your first raid' }) };
    },
    redemption() {
      const d = (page.data || {}).redemption;
      return { has: !!d, node: latest({ icon: 'points', tint: '#c06cf0', labelColor: '#e2c8ff', label: 'Latest redemption',
        name: d ? d.name + (d.reward ? ' · ' + d.reward : '') : '', value: d && d.cost > 0 ? num(d.cost) : '', valueColor: '#c06cf0',
        unit: d && d.cost > 0 ? 'points' : '', empty: !d, emptyText: 'shows up with the first reward taken' }) };
    },
    topcheer() {
      const p = pick('topcheer');
      if (!p.v) return null;
      const d = p.v;
      const box = el('div', 'cheer box');
      const face = el('div', 'face');
      let pic;
      // Pictures only from Twitch's own image servers (the app checks it, and so does this page)
      if (d.avatar && /^https:\/\/static-cdn\.jtvnw\.net\//.test(d.avatar)) {
        pic = el('img', 'pic');
        pic.src = d.avatar;
        pic.alt = '';
      } else {
        pic = el('div', 'pic', (d.name || '?').charAt(0).toUpperCase());
      }
      face.append(pic, icon('crown', 'crown'));
      const mid = el('div', 'mid');
      const caps = el('div', 'caps', d.stream === false ? 'Top cheerer · all time' : 'Top cheerer · this stream');
      caps.style.color = '#fde4a8';
      mid.append(caps, el('div', 'name ell', d.name));
      const bits = el('div', 'bits');
      bits.append(icon('gem'), el('span', '', num(d.bits)));
      box.append(face, mid, bits);
      return { has: has((page.data || {}).topcheer), node: box };
    },
    session() {
      const p = pick('session');
      if (!p.v) return null;
      const d = p.v;
      const box = el('div', 'session box');
      const caps = el('div', 'caps soft', 'This stream');
      const stats = el('div', 'stats');
      for (const [ic, tint, color, n, what] of [['heart', '#00f2ff', '#8ff7ff', '+' + num(d.followers), 'followers'],
        ['star', '#a77bff', '#d8c6ff', '+' + num(d.subs), 'subs'], ['gem', '#f5b942', '#fde4a8', num(d.bits), 'bits']]) {
        const stat = el('div', 'stat');
        const tile = el('div', 'tile');
        tile.style.background = rgba(tint, 0.18);
        tile.append(icon(ic));
        const words = el('div');
        const b = el('b', '', n);
        b.style.color = color;
        words.append(b, el('i', '', what));
        stat.append(tile, words);
        stats.append(stat);
      }
      box.append(caps, stats);
      const real = (page.data || {}).session;
      return { has: !!real && ((real.followers || 0) + (real.subs || 0) + (real.bits || 0)) > 0, node: box };
    },
    viewers() {
      const data = page.data || {};
      const withUptime = page.joined && place('uptime').on;
      const count = data.viewers >= 0 ? data.viewers : (showSample() ? page.sample.viewers : -1);
      const box = el('div', 'pill box');
      box.append(el('span', 'dot'));
      if (!withUptime) box.append(el('span', 'live', 'LIVE'), icon('viewer'));
      if (count >= 0) {
        box.append(el('span', 'big', num(count)));
        const w = el('span', 'small', 'watching');
        if (withUptime) w.style.marginLeft = '-6px';
        box.append(w);
      }
      const up = uptimeMs();
      if (withUptime && up !== null) {
        if (count >= 0) box.append(el('span', 'sep'));
        box.append(icon('clock'), el('span', 'big num', clock(up)));
      }
      return { has: data.viewers >= 0 || (withUptime && data.since > 0), node: box };
    },
    uptime() {
      if (page.joined && place('viewers').on) return null;
      const up = uptimeMs();
      if (up === null) return null;
      const box = el('div', 'pill box');
      box.style.gap = '10px';
      box.append(icon('clock'), el('span', 'big num', clock(up)), el('span', 'small', 'live'));
      return { has: (page.data || {}).since > 0, node: box };
    },
    goal_follow() {
      const p = pick('goal_follow');
      if (!p.v) return null;
      const d = p.v;
      return { has: has((page.data || {}).goal_follow), node: goal({ icon: 'target', labelColor: '#8ff7ff', label: d.reached ? 'Follower goal reached!' : 'Follower goal',
        amount: num(d.current), target: num(d.target), value: d.target > 0 ? d.current / d.target : 0, from: '#8ff7ff', to: '#00f2ff', text: goalText(d) }) };
    },
    goal_sub() {
      const p = pick('goal_sub');
      if (!p.v) return null;
      const d = p.v;
      return { has: has((page.data || {}).goal_sub), node: goal({ icon: 'star', labelColor: '#d8c6ff', label: d.reached ? 'Sub goal reached!' : 'Sub goal',
        amount: num(d.current), target: num(d.target), value: d.target > 0 ? d.current / d.target : 0, from: '#d8c6ff', to: '#8c52ff', text: goalText(d) }) };
    },
    charity() {
      const p = pick('charity');
      if (!p.v) return null;
      const d = p.v;
      return { has: has((page.data || {}).charity), node: goal({ icon: 'charity', labelColor: '#ffd1d9', label: d.name ? `Charity · ${d.name}` : 'Charity',
        amount: d.current || '', target: d.target || '', value: d.progress || 0, from: '#fda4af', to: '#fb7185',
        text: `${Math.round((d.progress || 0) * 100)} % raised – thank you, all of you!` }) };
    },
    hype() {
      const p = pick('hype');
      if (!p.v) return null;
      const d = p.v;
      const level = d.level || 1;
      const part = d.goal > 0 ? Math.min(1, (d.progress || 0) / d.goal) : 0;
      const first = Math.max(1, level - 2);
      const box = el('div', 'hype box');
      const lvl = el('div', 'lvl tile');
      lvl.append(el('div', 'l', 'LEVEL'), el('div', 'n', level));
      const rest = el('div', 'rest');
      const top = el('div', 'top');
      const caps = el('div', 'caps l', 'Hype train');
      caps.style.color = '#ffd6c8';
      top.append(icon('hype'), caps, el('div', 'grow'));
      if (d.ends) top.append(el('div', 'left num', `${clock(until(d.ends, p.fake))} left`));
      top.lastChild.style.marginLeft = 'auto';
      const steps = el('div', 'steps');
      for (let i = 0; i < 5; i++) {
        const step = first + i;
        const fill = step <= level ? 1 : (step === level + 1 ? part : 0);
        const s = el('span');
        const a = mix('#f0469b', '#f5a35a', Math.min(1, i * 0.22));
        const b = mix('#f36a86', '#f5a35a', Math.min(1, (i + 1) * 0.22));
        if (fill >= 1) s.style.backgroundImage = `linear-gradient(90deg, ${a}, ${b})`;
        else if (fill > 0) s.style.backgroundImage = `linear-gradient(90deg, ${a} ${Math.round(fill * 100)}%, var(--track) ${Math.round(fill * 100)}%)`;
        steps.append(s);
      }
      const pc = `${Math.round(part * 100)} % to level ${level + 1}`;
      rest.append(top, steps, el('div', 'text ell', d.top ? `${pc} · top: ${d.top}` : pc));
      box.append(lvl, rest);
      return { has: has((page.data || {}).hype), node: box };
    },
    poll() {
      const p = pick('poll');
      if (!p.v) return null;
      const d = p.v;
      const choices = (d.choices || []).slice(0, 5);
      const total = Math.max(1, d.total || 0);
      let best = -1, most = 0;
      choices.forEach((c, i) => { if (c.votes > most) { most = c.votes; best = i; } });
      const box = el('div', 'poll box');
      const head = el('div', 'head');
      const caps = el('div', 'caps l', 'Poll');
      caps.style.color = '#cfe3ff';
      const chip = el('div', 'chip num', d.done ? 'Final' : `${clock(until(d.ends || 0, p.fake))} left`);
      chip.style.background = 'rgba(96, 165, 250, 0.18)';
      chip.style.color = '#cfe3ff';
      chip.style.marginLeft = 'auto';
      head.append(icon('poll'), caps, chip);
      box.append(head, el('div', 'question', d.title || ''));
      choices.forEach((c, i) => {
        const row = el('div', 'choice' + (i === best ? ' lead' : ''));
        const fill = el('div', 'fill');
        fill.style.width = Math.round(100 * c.votes / total) + '%';
        const words = el('div', 'row');
        words.append(el('span', '', c.title), el('span', 'num', `${Math.round(100 * c.votes / total)} %`));
        row.append(fill, words);
        box.append(row);
      });
      box.append(el('div', 'votes', d.total === 1 ? '1 vote' : `${num(d.total)} votes`));
      return { has: has((page.data || {}).poll), node: box };
    },
    prediction() {
      const p = pick('prediction');
      if (!p.v) return null;
      const d = p.v;
      const outcomes = d.outcomes || [];
      const total = outcomes.reduce((s, o) => s + (o.points || 0), 0);
      const colors = ['#3b82f6', '#f0469b', '#34d399', '#f5b942', '#a77bff', '#00f2ff', '#fb923c', '#a3e635', '#e879f9', '#60a5fa'];
      const lights = ['#93c5fd', '#f9a8d4', '#a7f3d0', '#fde68a', '#d8c6ff', '#8ff7ff', '#fdba74', '#d9f99d', '#f5d0fe', '#cfe3ff'];
      const box = el('div', 'pred box');
      const head = el('div', 'head');
      const caps = el('div', 'caps l', 'Prediction');
      caps.style.color = '#d6dbff';
      const chip = el('div', 'chip num', d.done ? (d.winner >= 0 ? 'Decided' : 'Over') : d.locked ? 'Locked'
        : `closes in ${clock(until(d.locks || 0, p.fake))}`);
      chip.style.background = 'rgba(129, 140, 248, 0.2)';
      chip.style.color = '#d6dbff';
      chip.style.marginLeft = 'auto';
      head.append(icon('prediction'), caps, chip);
      const split = el('div', 'split');
      outcomes.forEach((o, i) => {
        const part = el('div');
        part.style.background = colors[i % colors.length];
        part.style.flexGrow = String(total > 0 ? Math.max(0.02, (o.points || 0) / total) : 1);
        split.append(part);
      });
      const outs = el('div', 'outs');
      outcomes.forEach((o, i) => {
        const won = d.winner === i;
        const out = el('div', 'out');
        out.style.background = rgba(colors[i % colors.length], won ? 0.32 : 0.16);
        if (won) out.style.boxShadow = `0 0 0 2px ${colors[i % colors.length]} inset`;
        if (d.done && d.winner >= 0 && !won) out.style.opacity = '0.55';
        const t = el('div', 't');
        const name = el('span', '', (won ? '✓ ' : '') + o.title);
        name.style.color = lights[i % lights.length];
        t.append(name, el('span', '', total > 0 ? `${Math.round(100 * (o.points || 0) / total)} %` : '–'));
        const odds = o.points > 0 ? ` · 1:${(total / o.points).toFixed(2)}` : '';
        out.append(t, el('div', 's', `${compact(o.points)} points${odds}`));
        outs.append(out);
      });
      box.append(head, el('div', 'question', d.title || ''), split, outs);
      return { has: has((page.data || {}).prediction), node: box };
    },
    adbreak() {
      const p = pick('adbreak');
      if (!p.v) return null;
      const d = p.v;
      const running = d.until ? until(d.until, p.fake) : 0;
      const coming = d.next ? until(d.next, p.fake) : 0;
      const box = el('div', 'pill ad box');
      if (running > 0) {
        const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
        svg.setAttribute('width', '30');
        svg.setAttribute('height', '30');
        svg.setAttribute('viewBox', '0 0 36 36');
        const back = document.createElementNS(svg.namespaceURI, 'circle');
        for (const [k, v] of [['cx', 18], ['cy', 18], ['r', 14], ['fill', 'none'], ['stroke', 'rgba(251,191,36,0.25)'], ['stroke-width', 5]]) back.setAttribute(k, v);
        const ring = document.createElementNS(svg.namespaceURI, 'circle');
        const share = Math.max(0.02, Math.min(1, running / 90000));
        for (const [k, v] of [['cx', 18], ['cy', 18], ['r', 14], ['fill', 'none'], ['stroke', '#fbbf24'], ['stroke-width', 5], ['stroke-linecap', 'round'],
          ['stroke-dasharray', `${(88 * share).toFixed(1)} 88`], ['transform', 'rotate(-90 18 18)']]) ring.setAttribute(k, v);
        svg.append(back, ring);
        box.append(svg, el('span', 'big', 'Ad break'));
        const s = el('span', 'small', 'back in ');
        const b = el('b', 'num', clock(running));
        b.style.color = '#fde68a';
        s.append(b);
        box.append(s);
      } else {
        box.append(icon('ad'), el('span', 'small', 'Ad break in'));
        const b = el('span', 'big num', clock(coming));
        b.style.color = '#fde68a';
        box.append(b);
      }
      const real = (page.data || {}).adbreak;
      const realRunning = real && real.until ? real.until - now : 0;
      const realComing = real && real.next ? real.next - now : 0;
      return { has: !!real && (realRunning > 0 || (realComing > 0 && realComing <= 300000)), node: box };
    },
    recent() {
      const p = pick('recent');
      if (!p.v) return null;
      const box = el('div', 'recent box');
      const items = el('div', 'items');
      const icons = { follow: 'heart', sub: 'star', gift: 'gift', bits: 'gem', raid: 'raid', points: 'points', charity: 'charity', shoutout: 'shoutout' };
      p.v.forEach((r, i) => {
        if (i > 0) items.append(el('span', 'dotsep'));
        const item = el('span', 'item');
        const words = el('span');
        words.append(el('b', '', r.name), document.createTextNode(' ' + r.text));
        item.append(icon(icons[r.kind] || 'star'), words);
        items.append(item);
      });
      box.append(el('div', 'tab', 'RECENT'), items, el('div', 'fade'));
      return { has: has((page.data || {}).recent), node: box };
    },
    chat() {
      const p = pick('chat');
      if (!p.v) return null;
      const box = el('div', 'chat');
      const head = el('div', 'head');
      head.append(icon('chat'), document.createTextNode('TWITCH CHAT'));
      box.append(head);
      const lines = p.v.slice(-5);
      const badgeIcons = { sub: 'star', bits: 'gem', mod: 'mod', vip: 'vip', broadcaster: 'broadcaster' };
      lines.forEach((m, i) => {
        const fromEnd = lines.length - 1 - i;
        const line = el('div', 'line box');
        line.style.opacity = String(fromEnd >= 4 ? 0.55 : fromEnd === 3 ? 0.7 : fromEnd === 2 ? 0.85 : 1);
        if ((m.badges || []).length) {
          const badges = el('span', 'badges');
          for (const b of m.badges) if (badgeIcons[b]) badges.append(icon(badgeIcons[b]));
          line.append(badges);
        }
        const msg = el('span', 'msg');
        const name = el('b', '', m.name);
        if (/^#[0-9A-Fa-f]{6}$/.test(m.color || '')) name.style.color = m.color;
        msg.append(name, document.createTextNode(m.text));
        line.append(msg);
        box.append(line);
      });
      return { has: has((page.data || {}).chat), node: box };
    },
  };

  const TITLES = {
    follower: 'Latest follower', subscriber: 'Latest subscriber', gift: 'Latest gift subs', bits: 'Latest bits', raid: 'Latest raid',
    redemption: 'Latest redemption', topcheer: 'Top cheerer', session: 'This stream', viewers: 'Viewers', uptime: 'Uptime',
    goal_follow: 'Follower goal', goal_sub: 'Sub goal', hype: 'Hype train', poll: 'Poll', prediction: 'Prediction', charity: 'Charity',
    adbreak: 'Ad break', recent: 'Recent events', chat: 'Twitch chat',
  };

  function place(id) {
    return (page.places || {})[id] || { on: false, x: 0, y: 0, scale: 100 };
  }
  function uptimeMs() {
    const since = (page.data || {}).since;
    if (since > 0) return now - since;
    return showSample() ? page.sample.since : null;
  }

  // ------------------------------------------------------------------ drawing

  function render() {
    now = Date.now();
    stage.className = 'look-' + (['transparent', 'solid'].includes(page.look) ? page.look : 'glass') + (editing ? ' editing' : '');
    const seen = new Set();
    for (const id of Object.keys(BUILD)) {
      const pl = place(id);
      const built = pl.on ? BUILD[id]() : null;
      const show = built && (editing || demo || (page.active && built.has));
      if (!show) continue;
      seen.add(id);
      let holder = boxes.get(id);
      if (!holder) {
        holder = el('div', 'tw');
        holder.dataset.id = id;
        if (editing) makeDraggable(holder);
        boxes.set(id, holder);
        stage.append(holder);
      }
      if (dragging === holder) continue; // keep it under the pointer
      // Only what changed is drawn anew: no flicker of pictures in OBS
      const html = built.node.outerHTML;
      if (holder._html !== html) {
        holder._html = html;
        holder.replaceChildren(built.node);
        if (editing) {
          const tag = el('div', 'tag');
          tag.append(icon('move'), document.createTextNode(id === 'viewers' && page.joined && place('uptime').on ? 'Viewers and uptime' : TITLES[id]));
          holder.append(tag);
        }
      }
      const node = holder.firstChild;
      const scale = Math.max(0.4, Math.min(2.5, (pl.scale || 100) / 100));
      holder.style.transform = `scale(${scale})`;
      const w = node.offsetWidth * scale;
      const h = node.offsetHeight * scale;
      holder.style.left = Math.round(32 + Math.max(0, Math.min(1, pl.x)) * Math.max(0, 1920 - 64 - w)) + 'px';
      holder.style.top = Math.round(32 + Math.max(0, Math.min(1, pl.y)) * Math.max(0, 1080 - 64 - h)) + 'px';
      const tag = holder.querySelector('.tag');
      if (tag) tag.classList.toggle('below', holder.offsetTop < 70);
    }
    boxes.forEach((holder, id) => {
      if (!seen.has(id) && dragging !== holder) {
        holder.remove();
        boxes.delete(id);
      }
    });
  }

  function fit() {
    // A 16:9 browser source: the stage fills its width
    stage.style.transform = `scale(${window.innerWidth / 1920})`;
  }
  window.addEventListener('resize', fit);
  fit();

  // ------------------------------------------------------------------ arranging

  if (editing) stage.append(guideX, guideY, where);

  function makeDraggable(holder) {
    let dx = 0, dy = 0;
    const toStage = (e) => {
      const s = window.innerWidth / 1920;
      return { x: e.clientX / s, y: e.clientY / s };
    };
    holder.addEventListener('pointerdown', (e) => {
      const p = toStage(e);
      dx = p.x - holder.offsetLeft;
      dy = p.y - holder.offsetTop;
      dragging = holder;
      holder.classList.add('dragging');
      holder.setPointerCapture(e.pointerId);
      e.preventDefault();
    });
    holder.addEventListener('pointermove', (e) => {
      if (dragging !== holder) return;
      const r = holder.getBoundingClientRect();
      const s = window.innerWidth / 1920;
      const w = r.width / s, h = r.height / s;
      const p = toStage(e);
      let x = Math.max(32, Math.min(1920 - 32 - w, p.x - dx));
      let y = Math.max(32, Math.min(1080 - 32 - h, p.y - dy));
      // Edges catch the edges of the others
      let lineX = -1, lineY = -1, bx = 9, by = 9;
      boxes.forEach((other) => {
        if (other === holder) return;
        const o = other.getBoundingClientRect();
        const ox = o.left / s, ow = o.width / s, oy = o.top / s, oh = o.height / s;
        for (const [mine, theirs] of [[x, ox], [x + w, ox + ow], [x, ox + ow], [x + w, ox]]) {
          if (Math.abs(mine - theirs) < bx) { bx = Math.abs(mine - theirs); x += theirs - mine; lineX = theirs; }
        }
        for (const [mine, theirs] of [[y, oy], [y + h, oy + oh]]) {
          if (Math.abs(mine - theirs) < by) { by = Math.abs(mine - theirs); y += theirs - mine; lineY = theirs; }
        }
      });
      holder.style.left = Math.round(x) + 'px';
      holder.style.top = Math.round(y) + 'px';
      guideX.style.display = lineX >= 0 ? 'block' : 'none';
      guideX.style.left = lineX + 'px';
      guideY.style.display = lineY >= 0 ? 'block' : 'none';
      guideY.style.top = lineY + 'px';
      const fx = Math.round(100 * (x - 32) / Math.max(1, 1920 - 64 - w));
      const fy = Math.round(100 * (y - 32) / Math.max(1, 1080 - 64 - h));
      where.textContent = (lineX >= 0 || lineY >= 0 ? 'lines up with the elements around · ' : '') + `${fx} % · ${fy} %`;
      where.style.display = 'flex';
      where.style.left = Math.round(Math.min(1920 - 520, x + w / 2 - 150)) + 'px';
      where.style.top = Math.round(y + h + 64 < 1080 ? y + h + 18 : y - 62) + 'px';
    });
    const drop = () => {
      if (dragging !== holder) return;
      dragging = null;
      holder.classList.remove('dragging');
      guideX.style.display = guideY.style.display = where.style.display = 'none';
      const s = window.innerWidth / 1920;
      const r = holder.getBoundingClientRect();
      const w = r.width / s, h = r.height / s;
      const id = holder.dataset.id;
      const x = Math.max(0, Math.min(1, (holder.offsetLeft - 32) / Math.max(1, 1920 - 64 - w)));
      const y = Math.max(0, Math.min(1, (holder.offsetTop - 32) / Math.max(1, 1080 - 64 - h)));
      const pl = place(id);
      pl.x = x;
      pl.y = y;
      // The app keeps it, once for everywhere, and tells every open page
      fetch(`/layout?tw_${id}_x=${x.toFixed(4)}&tw_${id}_y=${y.toFixed(4)}`, { cache: 'no-store' }).catch(() => {});
    };
    holder.addEventListener('pointerup', drop);
    holder.addEventListener('pointercancel', drop);
  }

  // ------------------------------------------------------------------ data from the app

  window.psrptTwitch = function (next) {
    if (!next || typeof next !== 'object') return;
    page = Object.assign(page, next);
    render();
  };
  fetch('/twitch/data', { cache: 'no-store' }).then((r) => r.json()).then(window.psrptTwitch).catch(() => {});
  // Countdowns, "2 min ago", the uptime
  setInterval(render, 1000);
})();
