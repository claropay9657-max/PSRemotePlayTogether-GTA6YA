// Stream scenes for OBS: /scene/<id> shows that scene, /scene/current the one chosen in
// the app's stream menu (nothing while the game is shown). The data comes from the app
// (LiveScenes) as server-sent events. Names and texts come from other players and from
// the streamer: they only ever go in as text, never as HTML.
'use strict';

const stage = document.getElementById('stage');
const wanted = (location.pathname.split('/')[2] || 'current').toLowerCase();
// ?demo=1: made-up players and a public room, to set a scene up in OBS without a session
const demo = new URLSearchParams(location.search).get('demo') === '1';
const demoStart = Date.now();
let data = null;
let builtKey = '';

// ------------------------------------------------------------------ the stage

function fit() {
  const scale = Math.min(window.innerWidth / 1920, window.innerHeight / 1080);
  stage.style.transform = `scale(${scale})`;
  stage.style.left = `${(window.innerWidth - 1920 * scale) / 2}px`;
  stage.style.top = `${(window.innerHeight - 1080 * scale) / 2}px`;
}
window.addEventListener('resize', fit);
fit();

function el(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined && text !== null) e.textContent = String(text);
  return e;
}

function svg(markup, size) {
  // Only drawings of this file, never anything that came in
  const ns = 'http://www.w3.org/2000/svg';
  const s = document.createElementNS(ns, 'svg');
  s.setAttribute('width', size);
  s.setAttribute('height', size);
  s.setAttribute('viewBox', '0 0 24 24');
  s.setAttribute('fill', 'none');
  s.setAttribute('stroke-width', '2');
  s.setAttribute('stroke-linecap', 'round');
  s.setAttribute('stroke-linejoin', 'round');
  s.setAttribute('aria-hidden', 'true');
  s.innerHTML = markup;
  return s;
}

const ICON_MIC = '<rect x="9" y="3" width="6" height="11" rx="3"></rect><path d="M5 11a7 7 0 0 0 14 0"></path><path d="M12 18v3"></path>';
const ICON_CLOCK = '<circle cx="12" cy="12" r="9"></circle><path d="M12 7v5l3 2"></path>';
const ICON_WIFI = '<path d="M2 8.5a15 15 0 0 1 20 0"></path><path d="M5 12a10.5 10.5 0 0 1 14 0"></path><path d="M8.5 15.5a5.5 5.5 0 0 1 7 0"></path><circle cx="12" cy="19" r="1"></circle>';
const ICON_CAL = '<rect x="3" y="5" width="18" height="16" rx="3"></rect><path d="M3 10h18"></path><path d="M8 3v4"></path><path d="M16 3v4"></path>';

function brand(big) {
  const b = el('div', big ? 'brand big' : 'brand');
  const img = el('img');
  img.src = '/scene/logo.svg';
  img.alt = 'PSRemotePlayTogether';
  b.append(img, el('span', '', 'PSRemotePlayTogether'));
  return b;
}

function avatar(p, extra) {
  if (p.avatarUrl) {
    const img = el('img', 'avatar' + (extra ? ' ' + extra : ''));
    img.src = p.avatarUrl;
    img.alt = '';
    return img;
  }
  return el('div', 'avatar initial' + (extra ? ' ' + extra : ''), p.initial || '?');
}

function stack(players, max) {
  const s = el('div', 'stack');
  for (const p of players.slice(0, max)) s.append(avatar(p));
  return s;
}

/** "A", "A and B", "A, B and C" */
function names(list) {
  if (list.length <= 1) return list.join('');
  return list.slice(0, -1).join(', ') + ' and ' + list[list.length - 1];
}

function textIf(cls, text) {
  return text ? el('div', cls, text) : null;
}

function joinText(d) {
  if (d.joinMode === 'web') return ['Join from your browser, no download: ', d.joinShown];
  if (d.joinMode === 'app') return ['Get the app and find the room: ', d.joinShown];
  return ['Ask in the chat for an invite.', ''];
}

// ------------------------------------------------------------------ the scenes

function starting(d) {
  const s = el('div', 'scene starting');
  const top = el('div', 'row');
  const pill = el('div', 'pill neutral');
  pill.append(el('span', 'dot wait'), document.createTextNode('Stream starts soon'));
  top.append(brand(true), el('div', 'grow'), pill);

  const main = el('div', 'main');
  const left = el('div', 'left');
  left.append(el('div', 'label', 'Live in'));
  const count = el('div', 'countdown', d.countdown);
  count.dataset.bind = 'countdown';
  left.append(count);
  const h1 = el('h1', 'grad', d.texts.starting_title);
  left.append(h1);
  const line = textIf('line', d.texts.starting_line);
  if (line) left.append(line);

  const right = el('div', 'right');
  const room = el('div', 'card room-card');
  const head = el('div', 'head');
  head.append(el('div', 'small-label', 'In the room'));
  const c = el('div', 'count');
  c.append(el('b', '', d.playerCount));
  c.append(document.createTextNode(d.maxPlayers > 0 ? ` / ${d.maxPlayers} players` : (d.playerCount === 1 ? ' player' : ' players')));
  head.append(c);
  room.append(head);
  for (const p of d.players.slice(0, 6)) {
    const r = el('div', 'player-row');
    r.dataset.slot = p.slot;
    r.append(avatar(p), el('div', 'name', p.name));
    if (p.live) r.append(el('div', 'tag live', 'LIVE'));
    else if (p.host) r.append(el('div', 'tag host', 'HOST'));
    room.append(r);
  }
  right.append(room);
  const along = el('div', 'card brand-card along');
  along.append(el('div', 't', 'Want to play along?'));
  const [what, where] = joinText(d);
  const dd = el('div', 'd', what);
  if (where) dd.append(el('b', '', where));
  along.append(dd);
  right.append(along);
  main.append(left, right);

  const bottom = el('div', 'row');
  const bar = el('div', 'bar');
  const fill = el('div');
  fill.dataset.width = 'progress';
  fill.style.width = `${Math.round((d.progress || 0) * 100)}%`;
  bar.append(fill);
  bottom.append(bar);
  if (d.channelLine) bottom.append(el('div', 'foot', d.channelLine));

  s.append(top, main, bottom);
  return s;
}

function lobby(d) {
  const s = el('div', 'scene lobby');
  const top = el('div', 'row');
  const img = el('img');
  img.src = '/scene/logo.svg';
  img.alt = 'PSRemotePlayTogether';
  img.style.cssText = 'width:56px;height:56px;border-radius:14px';
  const titles = el('div', 'titles');
  titles.append(el('div', 't', d.texts.lobby_title));
  const sub = [d.roomTitle, d.texts.lobby_line].filter((x) => x).join(' · ');
  if (sub) titles.append(el('div', 's', sub));
  top.append(img, titles, el('div', 'grow'));
  if (d.inVoice > 0) {
    const pill = el('div', 'pill good');
    pill.append(el('span', 'dot good'), document.createTextNode(`${d.inVoice} in voice chat`));
    top.append(pill);
  }

  const tiles = el('div', 'tiles');
  const n = Math.max(1, d.players.length);
  const cols = n === 1 ? 1 : (n <= 4 ? 2 : 3);
  tiles.style.gridTemplateColumns = `repeat(${cols}, minmax(0, 1fr))`;
  tiles.style.gridTemplateRows = `repeat(${Math.ceil(n / cols)}, minmax(0, 1fr))`;
  for (const p of d.players.slice(0, 9)) {
    const t = el('div', 'tile');
    t.dataset.slot = p.slot;
    if (p.camUrl) {
      const cam = el('img', 'cam');
      cam.src = p.camUrl;
      cam.alt = `Webcam of ${p.name}`;
      t.append(cam);
    } else {
      t.append(avatar(p));
    }
    const plate = el('div', 'nameplate');
    plate.append(svg(ICON_MIC, 22), el('div', 'n', p.name));
    plate.firstChild.setAttribute('class', 'mic');
    if (p.live) plate.append(el('div', 'r live', 'LIVE'));
    else if (p.host) plate.append(el('div', 'r', 'HOST'));
    t.append(plate);
    tiles.append(t);
  }

  const bar = el('div', 'bottom-bar');
  if (d.gamePicture) {
    const pic = el('img');
    pic.src = d.gamePicture;
    pic.alt = '';
    bar.append(pic);
  }
  if (d.game) {
    const g = el('div', '', 'Next up: ');
    g.append(el('b', '', d.game));
    bar.append(g);
  }
  bar.append(el('div', 'grow'));
  const [what, where] = joinText(d);
  const j = el('div', 'j', what);
  if (where) j.append(el('b', '', where));
  bar.append(j);

  s.append(top, tiles, bar);
  return s;
}

function brb(d) {
  const s = el('div', 'scene brb');
  if (d.gamePicture) {
    const back = el('img', 'backdrop');
    back.src = d.gamePicture;
    back.alt = '';
    s.append(back);
  }
  s.append(el('div', 'vignette'));
  const layer = el('div', 'layer');
  layer.append(brand(false));
  const center = el('div', 'center');
  const pill = el('div', 'pill wait');
  const clock = svg(ICON_CLOCK, 24);
  clock.setAttribute('stroke', '#f5b942');
  pill.append(clock, document.createTextNode(`Back in about ${d.breakMinutes} min`));
  center.append(pill, el('h1', 'grad', d.texts.brb_title));
  const line = textIf('big-line', d.texts.brb_line);
  if (line) center.append(line);
  layer.append(center);
  const bottom = el('div', 'row');
  bottom.style.justifyContent = 'center';
  if (d.players.length) {
    const who = el('div', 'who');
    who.append(stack(d.players, 5), el('div', 'n', names(d.players.map((p) => p.name))));
    if (d.game) who.append(el('div', 'sep'), el('div', 'g', d.game));
    bottom.append(who);
  }
  layer.append(bottom);
  s.append(layer);
  return s;
}

function play(d) {
  const s = el('div', 'scene play');
  const left = el('div', 'left');
  left.append(brand(false), el('h1', 'grad', d.texts.play_title));
  const line = textIf('line', d.texts.play_line);
  if (line) left.append(line);
  const steps = el('div', 'steps');
  (d.steps || []).forEach((text, i) => {
    const st = el('div', 'step');
    st.append(el('div', 'n', i + 1), el('div', 't', text));
    steps.append(st);
  });
  left.append(steps);

  const right = el('div', 'right');
  const card = el('div', 'game-card');
  const pic = el('div', 'pic');
  if (d.gamePicture) {
    const img = el('img');
    img.src = d.gamePicture;
    img.alt = 'Live picture of the game';
    pic.append(img);
  }
  const chip = el('div', 'live-chip');
  chip.append(el('span'), document.createTextNode('Live'));
  pic.append(chip);
  card.append(pic);
  const body = el('div', 'body');
  body.append(el('div', 't', d.roomTitle || (d.hostName ? `${d.hostName}'s session` : 'Our session')));
  if (d.game) body.append(el('div', 's', d.hostName ? `${d.hostName} plays ${d.game}` : d.game));
  const chips = el('div', 'chips');
  chips.append(el('div', 'chip tone-play', d.mixText));
  if (d.public) chips.append(el('div', 'chip tone-door', d.policyText));
  if (d.browsers) chips.append(el('div', 'chip tone-web', 'Browsers welcome'));
  body.append(chips);
  const places = el('div', 'places');
  places.append(stack(d.players, 5));
  const p = el('div', 'p');
  if (d.freePlaces >= 0) {
    p.append(el('b', '', d.freePlaces === 1 ? '1 place free' : `${d.freePlaces} places free`));
    p.append(document.createTextNode(` · ${d.playerCount} / ${d.maxPlayers} players`));
  } else {
    p.append(document.createTextNode(`${d.playerCount} playing`));
  }
  places.append(p);
  body.append(places);
  card.append(body);
  right.append(card);

  const box = el('div', 'card brand-card qr-box');
  if (d.qr) {
    const qr = el('img');
    qr.src = d.qr;
    qr.alt = 'QR code to the room';
    const t = el('div');
    t.style.cssText = 'display:flex;flex-direction:column;gap:6px';
    // An address breaks after a slash, not in the middle of a word
    const u = el('div', 'u');
    (d.joinShown || '').split('/').forEach((part, i, all) => {
      u.append(document.createTextNode(part + (i < all.length - 1 ? '/' : '')));
      if (i < all.length - 1) u.append(document.createElement('wbr'));
    });
    t.append(el('div', 'k', 'Scan, or open'), u);
    box.append(qr, t);
  } else {
    const t = el('div');
    t.append(el('div', 'k', 'Want in?'), el('div', 'u', 'Ask in the chat for an invite'));
    box.append(t);
  }
  right.append(box);

  s.append(left, right);
  return s;
}

function difficulties(d) {
  const s = el('div', 'scene difficulties');
  s.append(brand(false));
  const center = el('div', 'center');
  center.style.gap = '24px';
  const wifi = svg(ICON_WIFI, 96);
  wifi.setAttribute('stroke', '#f5b942');
  wifi.setAttribute('stroke-width', '1.6');
  center.append(wifi, el('h1', '', d.texts.difficulties_title));
  const line = textIf('big-line', d.texts.difficulties_line);
  if (line) center.append(line);
  const rows = el('div', 'card rows');
  for (const r of d.connection || []) {
    const row = el('div', 'state-row');
    row.append(el('span', `dot ${r.tone}`), el('div', 'w', r.what), el('div', `s ${r.tone}`, r.state));
    rows.append(row);
  }
  s.append(center, rows);
  const foot = textIf('footer-line', d.texts.difficulties_footer);
  if (foot) s.append(foot);
  return s;
}

function ending(d) {
  const s = el('div', 'scene ending');
  s.append(brand(false));
  const center = el('div', 'center');
  center.append(el('h1', 'grad', d.texts.ending_title));
  const stats = [];
  if (d.game) stats.push(['Played', d.game]);
  stats.push(['Together for', d.togetherText]);
  stats.push(['Players', d.seenCount === 1 ? '1 player' : `${d.seenCount} players`]);
  const grid = el('div', 'stats');
  grid.style.gridTemplateColumns = `repeat(${stats.length}, minmax(0, 1fr))`;
  for (const [l, v] of stats) {
    const c = el('div', 'card stat');
    c.append(el('div', 'l', l), el('div', 'v', v));
    grid.append(c);
  }
  center.append(grid);
  if (d.seenNames && d.seenNames.length) {
    const played = el('div', 'played');
    played.append(stack(d.players, 6), el('div', 't', `Played by ${names(d.seenNames)}`));
    center.append(played);
  }
  const bottom = el('div', 'row');
  bottom.style.gap = '24px';
  if (d.nextLine) {
    const c = el('div', 'end-chip brand-chip');
    const cal = svg(ICON_CAL, 26);
    cal.setAttribute('stroke', '#cdb6ff');
    c.append(cal, document.createTextNode(d.nextLine));
    bottom.append(c);
  }
  if (d.channelLine) bottom.append(el('div', 'end-chip plain', `Follow ${d.channelLine}`));
  bottom.append(el('div', 'grow'));
  if (d.joinMode === 'web') bottom.append(el('div', 'foot', `Play together: ${d.joinShown}`));
  s.append(center, bottom);
  return s;
}

const SCENES = { starting, lobby, brb, play, difficulties, ending };

// ------------------------------------------------------------------ keeping it up to date

/** What decides the layout; the countdown and who talks change in place. */
function layoutKey(d, id) {
  const players = (d.players || []).map((p) => [p.slot, p.name, p.avatarUrl, p.camUrl, p.host, p.live]);
  return JSON.stringify([id, players, d.texts, d.game, d.roomTitle, d.steps, d.joinMode, d.joinShown, d.qr,
    d.gamePicture, d.mixText, d.policyText, d.browsers, d.public, d.playerCount, d.maxPlayers, d.freePlaces,
    d.inVoice, d.breakMinutes, d.togetherText, d.seenNames, d.connection, d.nextLine, d.channelLine, d.hostName]);
}

function render() {
  if (!data) return;
  const id = wanted === 'current' ? data.scene : wanted;
  const make = SCENES[id];
  if (!make) {
    // The game: nothing on top of it
    stage.replaceChildren();
    builtKey = '';
    return;
  }
  const key = layoutKey(data, id);
  if (key !== builtKey) {
    stage.replaceChildren(make(data));
    builtKey = key;
  }
  for (const e of stage.querySelectorAll('[data-bind]')) e.textContent = data[e.dataset.bind];
  for (const e of stage.querySelectorAll('[data-width]')) e.style.width = `${Math.round((data[e.dataset.width] || 0) * 100)}%`;
  const bySlot = new Map((data.players || []).map((p) => [String(p.slot), p]));
  for (const e of stage.querySelectorAll('[data-slot]')) {
    const p = bySlot.get(e.dataset.slot);
    e.classList.toggle('talking', !!(p && p.talking));
    e.classList.toggle('muted', !!(p && p.muted));
    const a = e.querySelector('.avatar');
    if (a) a.classList.toggle('talking', !!(p && p.talking));
  }
}

function demoData(real) {
  const p = (slot, name, extra) => Object.assign({ slot, name, initial: name[0], voice: true, avatarUrl: '', camUrl: '' }, extra);
  const left = Math.max(0, 300 - Math.floor((Date.now() - demoStart) / 1000));
  const pad = (n) => (n < 10 ? '0' + n : '' + n);
  return Object.assign({}, real, {
    scene: wanted === 'current' ? 'starting' : wanted,
    players: [p(0, 'Nova', { host: true, talking: true }), p(1, 'Leo'), p(2, 'Mia'), p(3, 'Alex', { muted: true })],
    playerCount: 4, maxPlayers: 5, freePlaces: 1, inVoice: 4, hostName: 'Nova', ownName: 'Nova',
    game: 'Your game', roomTitle: 'Late night boss practice', public: true, browsers: true,
    mixText: 'Everybody plays together', policyText: 'Ask to join',
    joinMode: 'web', joinShown: 'crypto90.github.io/PSRemotePlayTogether', qr: '/scene/qr.svg?demo=1',
    steps: ['Open the link below in Chrome, Edge, Firefox or Safari.',
      'Pick “Late night boss practice” under Public rooms.',
      'Plug in a controller and knock – Nova lets you in.'],
    countdown: `${pad(Math.floor(left / 60))}:${pad(left % 60)}`, progress: 1 - left / 300,
    breakMinutes: 5, togetherText: '2 h 14 min', seenNames: ['Nova', 'Leo', 'Mia', 'Alex'], seenCount: 4,
    connection: [{ what: 'Console', state: 'Reconnecting…', tone: 'wait' }, { what: 'Voice chat', state: 'Connected', tone: 'good' },
      { what: 'Players', state: '4 in the room', tone: 'good' }],
    channelLine: real.channelLine || 'twitch.tv/yourchannel', nextLine: real.nextLine || 'Next stream: Friday, 8 pm',
    gamePicture: '',
  });
}

let real = null;
function receive(json) {
  try {
    real = JSON.parse(json);
  } catch (e) {
    return;
  }
  data = demo ? demoData(real) : real;
  render();
}
if (demo) {
  setInterval(() => {
    if (real) {
      data = demoData(real);
      render();
    }
  }, 1000);
}

fetch('/scene/data').then((r) => r.text()).then(receive).catch(() => {});
const events = new EventSource('/events?scene=1');
events.addEventListener('scene', (e) => receive(e.data));
