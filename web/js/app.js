// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The page: reads the invite, lets the player join, and puts picture, sound,
// controller, players and chat together while a session runs.

import { Sound } from './audio.js';
import { parseInvite } from './meet.js';
import { KEYBOARD_MAP, Pad, controllerName } from './pad.js';
import * as P from './proto.js';
import { JoinError, Session } from './session.js';
import { VideoPlayer } from './video.js';

const $ = (id) => document.getElementById(id);
const LOCAL = ['localhost', '127.0.0.1', '[::1]'].includes(location.hostname);
/** After a lost connection the page keeps trying to get back in for this long. */
const REJOIN_MS = 60000;

const ui = {
  front: $('front'),
  landing: $('card-landing'),
  join: $('card-join'),
  connecting: $('card-connecting'),
  message: $('card-message'),
  stage: $('stage'),
  screen: $('screen'),
  note: $('stage-note'),
  players: $('players'),
  toasts: $('toasts'),
  stats: $('stats'),
  cams: $('cams'),
  chat: $('chat'),
  chatList: $('chat-list'),
  chatInput: $('chat-input'),
  chatBadge: $('chat-badge'),
};

let invite = null;
let session = null;
let player = null;
let pad = null;
const sound = new Sound();
let wantVoice = false;
let micMuted = false;
let deafened = false;
let lastState = null;
let known = new Map(); // slot -> name, to tell who came and went
let rumble = { seq: 0, left: 0, right: 0 };
let padTimer = 0;
let slowTimer = 0;
let idleTimer = 0;
let wakeLock = null;
let leaving = false;
let unread = 0;
const mySent = new Set();
const avatarUrls = new Map(); // picture name -> object URL
const cams = new Map(); // slot -> {seq, data, got, count, url, el, img}
let statsLast = { at: 0, bytes: 0, shown: 0 };
let statsText = { mbit: '0.0', fps: 0 };

// ------------------------------------------------------------------ small helpers

function show(card) {
  for (const c of [ui.landing, ui.join, ui.connecting, ui.message]) c.hidden = c !== card;
  ui.front.hidden = false;
  ui.stage.hidden = true;
}

function toast(text) {
  const el = document.createElement('div');
  el.className = 'toast';
  el.textContent = text;
  ui.toasts.append(el);
  setTimeout(() => el.remove(), 4200);
}

function message(title, text, retryLabel = 'Try again') {
  $('message-title').textContent = title;
  $('message-text').textContent = text;
  $('message-retry').textContent = retryLabel;
  show(ui.message);
}

function typing() {
  const el = document.activeElement;
  return !!(el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA')) || $('keys').open;
}

function playerId() {
  let stored = null;
  try {
    stored = sessionStorage.getItem('psrpt.player');
  } catch (e) {
    stored = null;
  }
  if (stored && /^[0-9a-f]{16}$/.test(stored)) {
    return new Uint8Array(stored.match(/../g).map((h) => parseInt(h, 16)));
  }
  const id = crypto.getRandomValues(new Uint8Array(8));
  id[0] |= 1;
  try {
    sessionStorage.setItem('psrpt.player', Array.from(id, (b) => b.toString(16).padStart(2, '0')).join(''));
  } catch (e) {
    // private mode: a new one after every reload, which only costs the old place
  }
  return id;
}

function remember(key, value) {
  try {
    localStorage.setItem(key, value);
  } catch (e) {
    // not kept, then
  }
}

function recall(key) {
  try {
    return localStorage.getItem(key) || '';
  } catch (e) {
    return '';
  }
}

// ------------------------------------------------------------------ the invite

function readInvite() {
  let text = location.hash.length > 1 ? location.hash : '';
  if (text) {
    // Out of the address bar: a link in a screenshot or a stream lets anybody in
    try {
      sessionStorage.setItem('psrpt.invite', text);
    } catch (e) {
      // then it stays where it is
    }
    try {
      history.replaceState(null, '', location.pathname + location.search);
    } catch (e) {
      // a file opened from disk
    }
  } else {
    try {
      text = sessionStorage.getItem('psrpt.invite') || '';
    } catch (e) {
      text = '';
    }
  }
  return text ? parseInvite(text, LOCAL) : null;
}

function showJoin() {
  $('join-name').value = recall('psrpt.name');
  $('join-voice').checked = recall('psrpt.voice') === '1';
  const warning = $('join-warning');
  const problems = [];
  if (typeof RTCPeerConnection !== 'function') {
    problems.push('This browser has no WebRTC, which the connection to the host needs. A current Chrome, Edge, Firefox or Safari has it.');
    $('join-button').disabled = true;
  } else if (!VideoPlayer.supported()) {
    problems.push('This browser cannot show the picture (it lacks WebCodecs). You can still join with your controller only, for example when you sit in front of the same TV. For picture and sound use a current Chrome, Edge, Firefox or Safari.');
  } else if (!Sound.supported()) {
    problems.push('This browser can show the picture but not play the sound. A current Chrome, Edge, Firefox or Safari can do both.');
  }
  if (!window.isSecureContext) {
    problems.push('This page was not opened over HTTPS, so the browser keeps controllers and the microphone from it.');
  }
  warning.hidden = problems.length === 0;
  warning.textContent = problems.join(' ');
  $('join-voice').disabled = !Sound.canSpeak();
  show(ui.join);
  $('join-name').focus();
}

// ------------------------------------------------------------------ joining

function makeSession() {
  const s = new Session({
    secret: invite.secret,
    meetUrl: invite.meetUrl,
    name: $('join-name').value.trim() || 'Guest',
    playerId: playerId(),
    want: VideoPlayer.supported()
      ? P.WANT_VIDEO | (Sound.supported() ? P.WANT_AUDIO | P.WANT_VOICE : 0) | P.WANT_CAMS
      : 0,
  }, {
    onStatus: (text) => {
      $('connecting-text').textContent = text;
    },
    onState,
    onVideoConfig: (config) => {
      if (player) player.setConfig(config);
    },
    onVideoFrame: (frame) => {
      if (player) player.push(frame);
    },
    onAudio: (opus) => sound.pushGame(opus),
    onVoice: (slot, opus) => sound.pushVoice(slot, opus),
    onChat,
    onBlob: onAvatar,
    onCamFragment,
    onClosed,
  });
  s.setMedia(mediaFlags());
  if (pad) {
    pad.read();
    s.setController(pad.kind);
  }
  return s;
}

function mediaFlags() {
  return sound.micOn ? P.MEDIA_VOICE | (micMuted ? P.MEDIA_MUTED : 0) : 0;
}

async function join() {
  remember('psrpt.name', $('join-name').value.trim());
  wantVoice = $('join-voice').checked && !$('join-voice').disabled;
  remember('psrpt.voice', wantVoice ? '1' : '0');
  leaving = false;
  $('connecting-text').textContent = 'Getting ready…';
  show(ui.connecting);

  // Now, while this is still the click on "Join": browsers start sound only then
  const soundOk = await sound.start();
  let micProblem = '';
  if (wantVoice && soundOk) micProblem = await sound.startMic();

  session = makeSession();
  const mine = session;
  try {
    await mine.connect();
  } catch (e) {
    if (mine !== session) return; // cancelled, or another attempt has taken over
    session = null;
    sound.stopMic();
    if (e instanceof JoinError && e.kind === 'cancelled') showJoin();
    else message('That did not work', e && e.message ? e.message : String(e));
    return;
  }
  if (mine !== session) {
    mine.close();
    return;
  }
  enterStage();
  if (!soundOk && sound.problem) toast(sound.problem);
  if (micProblem) toast(micProblem);
}

function enterStage() {
  ui.front.hidden = true;
  ui.stage.hidden = false;
  ui.note.hidden = true;
  lastState = null;
  known = new Map();
  rumble = { seq: 0, left: 0, right: 0 };
  unread = 0;
  updateBadge();
  statsLast = { at: performance.now(), bytes: 0, shown: 0 };

  if (!player) {
    player = new VideoPlayer(ui.screen, {
      onNeedKeyframe: () => {
        if (session) session.requestKeyframe();
      },
      onSize: () => {},
      onError: (text) => showNote(text),
    });
  }
  player.setCodec(session.welcome.stream.codec);
  sound.setGameFormat(session.welcome.audio);
  sound.onVoiceFrame = (opus) => {
    if (session) session.sendVoice(opus);
  };
  if (!VideoPlayer.supported()) {
    showNote('Controller only: this browser cannot show the picture. Your controller plays along.');
  }

  clearInterval(padTimer);
  padTimer = setInterval(padTick, 4);
  clearInterval(slowTimer);
  slowTimer = setInterval(slowTick, 250);
  updateTools();
  wake();
  if (navigator.wakeLock) {
    navigator.wakeLock.request('screen').then((lock) => {
      wakeLock = lock;
    }).catch(() => {});
  }
}

function leaveStage() {
  clearInterval(padTimer);
  clearInterval(slowTimer);
  clearTimeout(idleTimer);
  if (pad) pad.rumble(0, 0);
  if (player) {
    player.close();
    player = null;
  }
  sound.reset();
  sound.stopMic();
  for (const slot of Array.from(cams.keys())) removeCam(slot);
  if (wakeLock) wakeLock.release().catch(() => {});
  wakeLock = null;
  if (document.fullscreenElement) document.exitFullscreen().catch(() => {});
  ui.chat.hidden = true;
  ui.stage.classList.remove('chat-open');
  ui.stats.hidden = true;
  ui.players.replaceChildren();
  ui.chatList.replaceChildren();
}

function leave() {
  leaving = true;
  if (session) session.close();
  session = null;
  leaveStage();
  showJoin();
}

function showNote(text) {
  ui.note.textContent = text;
  ui.note.hidden = !text;
}

/** The connection ended without the player wanting it. */
function onClosed(reason) {
  if (leaving) return;
  session = null;
  if (reason === 'kicked' || reason === 'ended') {
    leaveStage();
    message(reason === 'kicked' ? 'You were removed' : 'The session is over',
      reason === 'kicked' ? 'The host removed you from the session.' : 'The host has stopped sharing. When they share again, the same link lets you back in.',
      'Join again');
    return;
  }
  // The connection broke, or the host is getting its console back: try to return
  rejoin(reason === 'reconnecting'
    ? 'The host is reconnecting to the console. You are back in as soon as the game is.'
    : 'The connection was lost. Joining again…');
}

async function rejoin(text) {
  showNote(text);
  if (pad) pad.rumble(0, 0);
  sound.reset();
  const deadline = performance.now() + REJOIN_MS;
  let lastProblem = '';
  while (!leaving && performance.now() < deadline) {
    const attempt = makeSession();
    attempt.h.onStatus = () => {};
    session = attempt;
    try {
      await attempt.connect();
    } catch (e) {
      if (leaving || attempt !== session) return;
      session = null;
      lastProblem = e && e.message ? e.message : String(e);
      // A full session or a wrong version does not get better by waiting
      if (e instanceof JoinError && (e.kind === 'full' || e.kind === 'version' || e.kind === 'unsupported')) break;
      await new Promise((resolve) => setTimeout(resolve, 1500));
      continue;
    }
    if (leaving || attempt !== session) {
      attempt.close();
      return;
    }
    if (player) {
      player.close();
      player = null;
    }
    enterStage();
    toast('Back in the session');
    return;
  }
  if (leaving) return;
  session = null;
  leaveStage();
  message('The connection was lost', lastProblem || 'The host could not be reached any more.');
}

// ------------------------------------------------------------------ while playing

function padTick() {
  if (!session || !pad) return;
  const state = pad.read();
  session.setController(pad.kind || (pad.keyboardUsed ? P.CONTROLLER_OTHER : P.CONTROLLER_NONE));
  session.setPad(state);
}

function slowTick() {
  if (!session) return;
  if (pad && (rumble.left || rumble.right)) pad.rumble(rumble.left, rumble.right);
  updateTalking();
  updateStats();
}

function onState(s) {
  const first = !lastState;
  lastState = s;
  if (player) player.setCodec(s.stream.codec);
  sound.setGameFormat(s.audio);

  // Controller feedback from the game
  if (s.feedback.rumbleSeq !== rumble.seq) {
    rumble = { seq: s.feedback.rumbleSeq, left: s.feedback.rumbleLeft, right: s.feedback.rumbleRight };
    if (pad) pad.rumble(rumble.left, rumble.right);
  }

  // Who came and who went
  const now = new Map(s.participants.map((p) => [p.slot, p.name]));
  if (!first) {
    for (const [slot, name] of now) {
      if (!known.has(slot) && slot !== s.yourSlot) toast(`${name} joined`);
    }
    for (const [slot, name] of known) {
      if (!now.has(slot)) {
        toast(`${name} left`);
        sound.forgetVoice(slot);
        removeCam(slot);
      }
    }
  }
  known = now;
  for (const p of s.participants) {
    if (!(p.media & P.MEDIA_CAMERA)) removeCam(p.slot);
  }
  renderPlayers();
}

function renderPlayers() {
  const s = lastState;
  if (!s) return;
  const nodes = [];
  for (const p of s.participants) {
    const you = p.slot === s.yourSlot;
    const el = document.createElement('div');
    el.className = 'player' + (p.inControl ? ' control' : '');
    el.dataset.slot = String(p.slot);
    const face = document.createElement('div');
    face.className = 'face';
    const url = p.avatar.valid ? avatarUrls.get(P.hashKey(p.avatar.hash)) : '';
    if (url) face.style.backgroundImage = `url("${url}")`;
    else face.textContent = (p.name.trim()[0] || '?').toUpperCase();
    const name = document.createElement('span');
    name.className = 'name';
    name.textContent = p.name + (you ? ' (you)' : '');
    el.append(face, name);
    const tag = (text, cls) => {
      const t = document.createElement('span');
      t.className = 'tag ' + cls;
      t.textContent = text;
      el.append(t);
    };
    if (p.host) tag('HOST', 'host');
    if ((p.media & P.MEDIA_VOICE) && (p.media & (P.MEDIA_MUTED | P.MEDIA_SILENCED))) tag('MUTED', 'muted');
    if (!p.host && !p.canControl) tag('WATCHING', '');
    if (!you && (p.media & P.MEDIA_VOICE)) {
      el.title = sound.muted.has(p.slot) ? 'Click to hear them again' : 'Click to mute them for you';
      el.style.cursor = 'pointer';
      el.style.opacity = sound.muted.has(p.slot) ? '0.5' : '';
      el.addEventListener('click', () => {
        sound.setPlayerMuted(p.slot, !sound.muted.has(p.slot));
        renderPlayers();
      });
    }
    nodes.push(el);
  }
  ui.players.replaceChildren(...nodes);
  updateTalking();
}

function updateTalking() {
  if (!lastState) return;
  const talking = sound.talkingNow(sound.micOn && !micMuted ? lastState.yourSlot : undefined);
  for (const el of ui.players.children) {
    el.classList.toggle('talking', talking.has(Number(el.dataset.slot)));
  }
}

function onAvatar(ref, data) {
  const key = P.hashKey(ref.hash);
  if (!avatarUrls.has(key)) avatarUrls.set(key, URL.createObjectURL(new Blob([data])));
  renderPlayers();
}

function updateStats() {
  const now = performance.now();
  if (now - statsLast.at < 1000) return;
  const seconds = (now - statsLast.at) / 1000;
  const shown = player ? player.stats.shown : 0;
  statsText = {
    mbit: (((session.stats.bytes - statsLast.bytes) * 8) / seconds / 1e6).toFixed(1),
    fps: Math.round((shown - statsLast.shown) / seconds),
  };
  statsLast = { at: now, bytes: session.stats.bytes, shown };
  if (ui.stats.hidden) return;
  const a = session.assembler.stats;
  const stream = lastState ? lastState.stream : session.welcome.stream;
  ui.stats.textContent = [
    `Picture     ${ui.screen.width}×${ui.screen.height}, ${statsText.fps} of ${stream.fps} per second`,
    `Format      ${player && player.codecName ? player.codecName : 'none'}`,
    `Arriving    ${statsText.mbit} Mbit/s`,
    `To the host ${Math.round(session.rttMs)} ms there and back`,
    `Frames      ${a.delivered} shown, ${a.dropped} lost, ${a.nackFragments} pieces asked for again`,
    `Controller  ${pad && pad.kind ? controllerName(pad.kind) : pad && pad.keyboardUsed ? 'Keyboard' : 'none'}`,
  ].join('\n');
}

// ------------------------------------------------------------------ webcams of the others

function onCamFragment(c) {
  let cam = cams.get(c.slot);
  if (!cam) {
    const el = document.createElement('div');
    el.className = 'cam';
    const img = document.createElement('img');
    img.alt = '';
    const label = document.createElement('span');
    el.append(img, label);
    ui.cams.append(el);
    cam = { seq: -1, data: null, got: null, count: 0, url: '', el, img, label, done: -1 };
    cams.set(c.slot, cam);
  }
  // Only the newest picture is collected
  if (cam.done >= 0 && ((c.frameSeq - cam.done) | 0) <= 0) return;
  if (c.frameSeq !== cam.seq) {
    if (cam.seq >= 0 && ((c.frameSeq - cam.seq) | 0) < 0) return;
    cam.seq = c.frameSeq;
    cam.data = new Uint8Array(c.totalSize);
    cam.got = new Uint8Array(c.fragCount);
    cam.count = 0;
  }
  if (cam.data.length !== c.totalSize || cam.got[c.fragIndex]) return;
  cam.data.set(c.data, c.fragIndex * P.CAM_FRAGMENT_SIZE);
  cam.got[c.fragIndex] = 1;
  if (++cam.count !== cam.got.length) return;
  cam.done = cam.seq;
  const url = URL.createObjectURL(new Blob([cam.data], { type: 'image/jpeg' }));
  const old = cam.url;
  cam.url = url;
  cam.img.onload = () => {
    if (old) URL.revokeObjectURL(old);
  };
  cam.img.src = url;
  const who = lastState ? lastState.participants.find((p) => p.slot === c.slot) : null;
  cam.label.textContent = who ? who.name : '';
}

function removeCam(slot) {
  const cam = cams.get(slot);
  if (!cam) return;
  cam.el.remove();
  if (cam.url) URL.revokeObjectURL(cam.url);
  cams.delete(slot);
}

// ------------------------------------------------------------------ chat

function updateBadge() {
  ui.chatBadge.hidden = unread === 0;
  ui.chatBadge.textContent = String(unread);
}

function onChat(m) {
  const mine = lastState && m.slot === lastState.yourSlot && mySent.has(m.upId);
  const el = document.createElement('div');
  el.className = 'msg' + (mine ? ' mine' : '') + (m.history ? ' old' : '');
  const who = document.createElement('div');
  who.className = 'who';
  who.textContent = m.name;
  const time = document.createElement('time');
  time.textContent = new Date(m.timeMs).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  who.append(time);
  const text = document.createElement('div');
  text.className = 'text';
  appendWithLinks(text, m.text);
  el.append(who, text);
  const empty = ui.chatList.querySelector('.chat-empty');
  if (empty) empty.remove();
  const atEnd = ui.chatList.scrollHeight - ui.chatList.scrollTop - ui.chatList.clientHeight < 60;
  ui.chatList.append(el);
  if (atEnd || mine) ui.chatList.scrollTop = ui.chatList.scrollHeight;
  if (ui.chat.hidden && !m.history && !mine) {
    unread++;
    updateBadge();
    toast(`${m.name}: ${m.text.length > 80 ? m.text.slice(0, 80) + '…' : m.text}`);
  }
}

/** The text as it is; only addresses that start with http(s) become links. */
function appendWithLinks(parent, text) {
  const pattern = /https?:\/\/[^\s<>"']+/g;
  let last = 0;
  for (const match of text.matchAll(pattern)) {
    if (match.index > last) parent.append(text.slice(last, match.index));
    const a = document.createElement('a');
    a.href = match[0];
    a.textContent = match[0];
    a.target = '_blank';
    a.rel = 'noopener noreferrer';
    parent.append(a);
    last = match.index + match[0].length;
  }
  if (last < text.length) parent.append(text.slice(last));
}

function openChat(open) {
  ui.chat.hidden = !open;
  ui.stage.classList.toggle('chat-open', open);
  if (open) {
    unread = 0;
    updateBadge();
    if (!ui.chatList.children.length) {
      const empty = document.createElement('div');
      empty.className = 'chat-empty';
      empty.textContent = 'Nothing written yet.';
      ui.chatList.append(empty);
    }
    ui.chatList.scrollTop = ui.chatList.scrollHeight;
    ui.chatInput.focus();
  } else ui.chatInput.blur();
  wake();
}

// ------------------------------------------------------------------ the bar

function updateTools() {
  const mic = $('btn-mic');
  mic.hidden = !Sound.canSpeak();
  mic.classList.toggle('off', sound.micOn && micMuted);
  $('btn-mic-text').textContent = !sound.micOn ? 'Join voice' : micMuted ? 'Mic muted' : 'Mic on';
  const voices = $('btn-voices');
  voices.hidden = !Sound.supported();
  voices.classList.toggle('off', deafened);
  $('btn-voices-text').textContent = deafened ? 'Voices off' : 'Voices on';
  if (session) session.setMedia(mediaFlags());
}

/** Shows the bar and the mouse pointer for a moment. */
function wake() {
  ui.stage.classList.remove('idle');
  clearTimeout(idleTimer);
  idleTimer = setTimeout(() => {
    if (!ui.chat.hidden || typing() || ui.stage.querySelector('.bar:hover')) {
      wake();
      return;
    }
    ui.stage.classList.add('idle');
  }, 3200);
}

function keyLabel(code) {
  const special = { ArrowUp: '↑', ArrowDown: '↓', ArrowLeft: '←', ArrowRight: '→', Enter: 'Enter', Backspace: '⌫' };
  return special[code] || code.replace(/^Key/, '');
}

// ------------------------------------------------------------------ wiring

function start() {
  pad = new Pad(typing);

  $('landing-form').addEventListener('submit', (e) => {
    e.preventDefault();
    const parsed = parseInvite($('landing-invite').value, LOCAL);
    const error = $('landing-error');
    if (!parsed) {
      error.textContent = 'That is not a link or invite of PSRemotePlayTogether. Copy the whole link your friend sent you.';
      error.hidden = false;
      return;
    }
    error.hidden = true;
    invite = parsed;
    try {
      sessionStorage.setItem('psrpt.invite', $('landing-invite').value.trim());
    } catch (err) {
      // only for this visit, then
    }
    showJoin();
  });

  $('join-form').addEventListener('submit', (e) => {
    e.preventDefault();
    join();
  });
  $('connecting-cancel').addEventListener('click', () => {
    const s = session;
    session = null;
    if (s) s.close();
    sound.stopMic();
    showJoin();
  });
  $('message-retry').addEventListener('click', () => (invite ? showJoin() : show(ui.landing)));

  // The controller the browser sees, before joining
  setInterval(() => {
    if (ui.join.hidden) return;
    pad.read();
    const line = $('pad-status');
    line.classList.toggle('ok', !!pad.kind);
    $('pad-status-text').textContent = pad.kind
      ? `${controllerName(pad.kind)} connected.`
      : 'Press any button on your controller so that the browser sees it. No controller? The keyboard works too.';
  }, 400);

  $('btn-leave').addEventListener('click', leave);
  $('btn-full').addEventListener('click', () => {
    if (document.fullscreenElement) document.exitFullscreen().catch(() => {});
    else if (ui.stage.requestFullscreen) ui.stage.requestFullscreen().catch(() => {});
  });
  $('btn-stats').addEventListener('click', () => {
    ui.stats.hidden = !ui.stats.hidden;
    statsLast.at = 0;
    if (session) updateStats();
  });
  $('btn-chat').addEventListener('click', () => openChat(ui.chat.hidden));
  $('chat-close').addEventListener('click', () => openChat(false));
  $('chat-form').addEventListener('submit', (e) => {
    e.preventDefault();
    const text = ui.chatInput.value.trim();
    if (!text || !session) return;
    const id = session.sendChat(text);
    if (id) {
      mySent.add(id);
      ui.chatInput.value = '';
    }
  });
  ui.chatInput.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') openChat(false);
  });

  $('btn-mic').addEventListener('click', async () => {
    if (!sound.micOn) {
      const problem = await sound.startMic();
      if (problem) toast(problem);
      micMuted = false;
    } else micMuted = !micMuted;
    sound.setMicMuted(micMuted);
    updateTools();
  });
  $('btn-voices').addEventListener('click', () => {
    deafened = !deafened;
    sound.setDeafened(deafened);
    updateTools();
  });
  const volume = $('volume');
  volume.value = recall('psrpt.volume') || '100';
  sound.setGameVolume(Number(volume.value) / 100);
  volume.addEventListener('input', () => {
    sound.setGameVolume(Number(volume.value) / 100);
    remember('psrpt.volume', volume.value);
  });

  const keys = $('keys');
  $('keys-list').replaceChildren(...KEYBOARD_MAP.map(([code, what]) => {
    const row = document.createElement('div');
    const kbd = document.createElement('kbd');
    kbd.textContent = keyLabel(code);
    const label = document.createElement('span');
    label.textContent = what;
    row.append(kbd, label);
    return row;
  }));
  const openKeys = () => (keys.showModal ? keys.showModal() : keys.setAttribute('open', ''));
  $('join-keys').addEventListener('click', openKeys);
  $('btn-keys').addEventListener('click', openKeys);
  $('keys-close').addEventListener('click', () => keys.close());

  for (const type of ['mousemove', 'mousedown', 'touchstart', 'keydown']) {
    ui.stage.addEventListener(type, wake, { passive: true });
  }
  // A browser that held the sound back lets it play at the next click or key press
  for (const type of ['pointerdown', 'keydown']) {
    window.addEventListener(type, () => sound.wake(), { passive: true });
  }
  ui.screen.addEventListener('dblclick', () => $('btn-full').click());

  // Leaving the page says goodbye, so that the others do not wait for a timeout
  window.addEventListener('pagehide', () => {
    if (session) session.close();
  });
  document.addEventListener('visibilitychange', () => {
    // Coming back to the tab: the picture starts over at once instead of catching up
    if (!document.hidden && session && session.connected) session.requestKeyframe();
    if (!document.hidden && session && navigator.wakeLock && !wakeLock) {
      navigator.wakeLock.request('screen').then((lock) => {
        wakeLock = lock;
      }).catch(() => {});
    }
  });

  // For somebody trying things out on their own machine: a look inside from the console
  if (LOCAL) {
    window.psrpt = {
      get session() { return session; },
      get player() { return player; },
      get state() { return lastState; },
      sound,
      pad,
    };
  }

  invite = readInvite();
  if (invite) showJoin();
  else show(ui.landing);
}

start();
