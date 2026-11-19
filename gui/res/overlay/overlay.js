// Play Together stream overlay. Gets its data from the app over a
// server-sent-events connection and draws one card per player.
(function () {
  'use strict';

  const PAD_WIDTH = 840;   // size of the page a CryptoPad skin is laid out in
  const PAD_HEIGHT = 720;

  // Bits of the controller state, same as in the app
  const BUTTON = {
    cross: 1 << 0, circle: 1 << 1, square: 1 << 2, triangle: 1 << 3,
    left: 1 << 4, right: 1 << 5, up: 1 << 6, down: 1 << 7,
    l1: 1 << 8, r1: 1 << 9, l3: 1 << 10, r3: 1 << 11,
    options: 1 << 12, share: 1 << 13, touchpad: 1 << 14, ps: 1 << 15,
  };
  const CONTROLLER_SKIN = { 1: 'PS5', 2: 'Xbox', 4: 'PS4' };

  const params = new URLSearchParams(window.location.search);
  const root = document.getElementById('root');

  let config = {
    players: true, pads: true, combined: true, cams: true, voice: true, battery: true, notices: true, chat: true,
    layout: 'row', scale: 30, skin: 'auto', skins: ['PS5'],
    // 'transparent': no boxes, the stream shows through; 'glass': see-through boxes; 'solid': dark boxes
    style: 'transparent',
    // Free placement: every player's card and the console's controller sit where they
    // were dragged to. The app sends the places as p0_x, p0_y, p1_x, ... combined_x,
    // combined_y: fractions of the space left over (0 = left/top edge, 1 = right/bottom).
    free: false,
  };

  // Arrange mode (?edit=1, opened from the app in an ordinary browser): the cards
  // can be dragged, and where they are dropped is saved in the app
  const editing = params.get('edit') === '1';

  // In the ordinary layout the players flow in one box and the console's controller
  // follows; with free placement the boxes dissolve and every card is placed by itself
  const playersBox = document.createElement('div');
  playersBox.className = 'group players-group';
  const combinedBox = document.createElement('div');
  combinedBox.className = 'group combined-group';
  root.append(playersBox, combinedBox);
  let state = { active: false, players: [] };
  const cards = new Map();   // key -> { el, frame, ready, skin, pad }

  function effectiveConfig() {
    const c = Object.assign({}, config);
    if (params.has('show')) {
      const show = params.get('show').split(',').map((s) => s.trim());
      c.players = show.includes('players');
      c.pads = show.includes('pads');
      c.combined = show.includes('combined');
      c.cams = show.includes('cams');
    }
    if (params.has('voice')) c.voice = params.get('voice') !== '0';
    if (params.has('battery')) c.battery = params.get('battery') !== '0';
    if (params.has('chat')) c.chat = params.get('chat') !== '0';
    if (params.has('free')) c.free = params.get('free') !== '0';
    if (editing) c.free = true;
    if (params.has('layout')) c.layout = params.get('layout') === 'column' ? 'column' : 'row';
    if (params.has('scale')) c.scale = Math.max(10, Math.min(100, parseInt(params.get('scale'), 10) || c.scale));
    if (params.has('skin')) c.skin = params.get('skin');
    if (params.has('style')) c.style = params.get('style');
    if (c.style !== 'glass' && c.style !== 'solid') c.style = 'transparent';
    return c;
  }

  function skinFor(c, controller) {
    let skin = c.skin && c.skin !== 'auto' ? c.skin : (CONTROLLER_SKIN[controller] || 'PS5');
    if (c.skins && c.skins.length && !c.skins.includes(skin)) skin = c.skins.includes('PS5') ? 'PS5' : c.skins[0];
    return skin;
  }

  function toStandard(p) {
    const b = p ? p.b : 0;
    const axis = (v) => Math.max(-1, Math.min(1, (v || 0) / 32767));
    return {
      a: b & BUTTON.cross, b: b & BUTTON.circle, x: b & BUTTON.square, y: b & BUTTON.triangle,
      lb: b & BUTTON.l1, rb: b & BUTTON.r1,
      select: b & BUTTON.share, start: b & BUTTON.options,
      meta: b & BUTTON.ps, touchpad: b & BUTTON.touchpad,
      lt: p ? (p.l2 || 0) / 255 : 0, rt: p ? (p.r2 || 0) / 255 : 0,
      dpad_up: b & BUTTON.up, dpad_down: b & BUTTON.down, dpad_left: b & BUTTON.left, dpad_right: b & BUTTON.right,
      left_stick_x: p ? axis(p.lx) : 0, left_stick_y: p ? axis(p.ly) : 0,
      right_stick_x: p ? axis(p.rx) : 0, right_stick_y: p ? axis(p.ry) : 0,
      ls: b & BUTTON.l3, rs: b & BUTTON.r3,
    };
  }

  function el(tag, className, text) {
    const e = document.createElement(tag);
    if (className) e.className = className;
    if (text !== undefined) e.textContent = text;
    return e;
  }

  function createCard(key) {
    const card = { key: key, skin: null, ready: false, pad: null };
    card.el = el('div', 'card');
    card.who = el('div', 'who');
    card.avatar = el('div', 'avatar');
    card.avatarImg = el('img');
    card.avatarImg.alt = '';
    card.initial = el('span', 'initial');
    card.avatar.append(card.avatarImg, card.initial);
    const meta = el('div', 'meta');
    card.name = el('div', 'name');
    card.tags = el('div', 'tags');
    meta.append(card.name, card.tags);
    card.mic = el('div', 'mic');
    card.who.append(card.avatar, meta, card.mic);
    card.camWrap = el('div', 'cam');
    card.camImg = el('img');
    card.camImg.alt = '';
    card.camWrap.append(card.camImg);
    card.padWrap = el('div', 'pad');
    card.el.append(card.who, card.camWrap, card.padWrap);
    cards.set(key, card);
    if (editing) makeDraggable(card);
    return card;
  }

  function setSkin(card, skin, scale) {
    const factor = scale / 100;
    card.padWrap.style.width = Math.round(PAD_WIDTH * factor) + 'px';
    card.padWrap.style.height = Math.round(PAD_HEIGHT * factor) + 'px';
    if (card.skin === skin && card.frame) {
      card.frame.style.transform = 'scale(' + factor + ')';
      return;
    }
    card.skin = skin;
    card.ready = false;
    card.padWrap.textContent = '';
    const frame = document.createElement('iframe');
    frame.src = '/pad/' + encodeURIComponent(skin) + '/index.html';
    frame.scrolling = 'no';
    frame.setAttribute('allowtransparency', 'true');
    frame.style.width = PAD_WIDTH + 'px';
    frame.style.height = PAD_HEIGHT + 'px';
    frame.style.transform = 'scale(' + factor + ')';
    card.frame = frame;
    card.padWrap.append(frame);
  }

  function sendPad(card) {
    if (card.frame && card.ready && card.frame.contentWindow)
      card.frame.contentWindow.postMessage({ type: 'pad', standard: toStandard(card.pad) }, '*');
  }

  window.addEventListener('message', function (event) {
    if (!event.data || event.data.type !== 'pad-ready') return;
    cards.forEach(function (card) {
      if (card.frame && card.frame.contentWindow === event.source) {
        card.ready = true;
        sendPad(card);
      }
    });
  });

  function render() {
    const c = effectiveConfig();
    document.body.classList.toggle('preview', params.get('bg') === 'dark' || editing);
    document.body.classList.toggle('edit', editing);
    root.className = 'layout-' + c.layout + ' style-' + c.style + (state.active ? '' : ' idle') + (c.free ? ' free' : '');

    const wanted = [];
    if (state.active && (c.players || c.pads || c.cams)) {
      (state.players || []).forEach(function (p) {
        // A source that shows webcams only has nothing to show for players without one
        if (!c.players && !c.pads && !p.camera) return;
        wanted.push({ key: 'p' + p.slot, player: p });
      });
    }
    if (state.active && c.combined && state.combined)
      wanted.push({ key: 'combined', player: null });

    // Remove cards of players who left
    const keys = new Set(wanted.map((w) => w.key));
    cards.forEach(function (card, key) {
      if (!keys.has(key)) {
        card.el.remove();
        cards.delete(key);
      }
    });

    const oneAtATime = state.mixMode === 1;
    let playerIndex = 0;
    wanted.forEach(function (w) {
      const card = cards.get(w.key) || createCard(w.key);
      if (w.player) {
        if (playersBox.children[playerIndex] !== card.el)
          playersBox.insertBefore(card.el, playersBox.children[playerIndex] || null);
        playerIndex++;
      } else if (card.el.parentNode !== combinedBox) {
        combinedBox.append(card.el);
      }

      if (w.player) {
        const p = w.player;
        const showPad = c.pads;
        const showCam = c.cams && p.camera;
        card.el.className = 'card player'
          + (p.inControl ? ' in-control' : '')
          + (!p.canControl ? ' watching' : '')
          + (oneAtATime && !p.inControl ? ' waiting' : '')
          + (p.you ? ' you' : '')
          + (p.talking ? ' talking' : '')
          + (showPad || showCam ? '' : ' no-pad');

        // Microphone: nothing for players who are not in the voice chat
        card.mic.className = 'mic' + (p.voice || p.muted ? (p.muted ? ' off' : ' on') : '');
        card.mic.title = p.muted ? 'Muted' : '';

        // Webcam: one never-ending picture stream per player, opened when the camera goes on
        card.camWrap.style.display = showCam ? '' : 'none';
        const camSrc = showCam ? '/cam/' + p.slot + '.mjpg' : '';
        if (card.camSrc !== camSrc) {
          card.camSrc = camSrc;
          if (camSrc) card.camImg.src = camSrc; else card.camImg.removeAttribute('src');
        }
        if (showCam) {
          // As wide as the controller below it, or a fixed size without one
          const camWidth = showPad ? Math.round(PAD_WIDTH * c.scale / 100) : Math.round(320 * Math.max(c.scale, 40) / 60);
          card.camWrap.style.width = camWidth + 'px';
          card.camWrap.style.height = Math.round(camWidth * 9 / 16) + 'px';
        }
        card.who.style.display = c.players ? '' : 'none';
        card.name.textContent = p.name || ('Player ' + (p.slot + 1));
        card.initial.textContent = (p.name || '?').trim().charAt(0).toUpperCase();
        const src = p.avatar ? '/avatar/' + p.avatar : '';
        if (card.avatarSrc !== src) {
          card.avatarSrc = src;
          if (src) card.avatarImg.src = src; else card.avatarImg.removeAttribute('src');
        }
        card.avatar.classList.toggle('has-image', !!src);

        card.tags.textContent = '';
        if (p.host) card.tags.append(el('span', 'tag host', 'Host'));
        if (!p.canControl) card.tags.append(el('span', 'tag', 'Watching'));
        else if (p.inControl && (oneAtATime || state.mixMode === 2)) card.tags.append(el('span', 'tag control', 'In control'));
        // Controller charge: 0..100 percent, 254 = on a cable, 255 or nothing = unknown
        if (c.battery && typeof p.battery === 'number' && p.battery !== 255) {
          const wired = p.battery > 100;
          const level = wired ? 'wired' : (p.battery <= 20 ? 'low' : (p.battery <= 50 ? 'mid' : 'high'));
          const tag = el('span', 'tag battery ' + level);
          const cell = el('span', 'cell');
          const fill = el('span', 'fill');
          fill.style.width = (wired ? 100 : Math.max(8, p.battery)) + '%';
          cell.append(fill);
          tag.append(cell, document.createTextNode(wired ? 'Cable' : p.battery + ' %'));
          card.tags.append(tag);
        }

        card.padWrap.style.display = showPad ? '' : 'none';
        if (showPad) setSkin(card, skinFor(c, p.controller), c.scale);
        card.pad = p.pad || null;
      } else {
        card.el.className = 'card combined';
        card.mic.className = 'mic';
        card.camWrap.style.display = 'none';
        card.who.style.display = c.players ? '' : 'none';
        card.name.textContent = 'Console';
        card.initial.textContent = '';
        card.avatar.classList.remove('has-image');
        card.avatar.classList.add('console');
        card.tags.textContent = '';
        card.tags.append(el('span', 'tag', 'What the game gets'));
        card.padWrap.style.display = '';
        setSkin(card, skinFor(c, 1), c.scale);
        card.pad = state.combined;
      }
        sendPad(card);
    });
    place();
    announce(c);
  }

  // ------------------------------------------------------------------ joined / left

  const toastBox = el('div', 'toasts');
  document.body.append(toastBox);
  let known = null; // slot -> name; null until the first list of a session

  function toast(text, joined) {
    const t = el('div', 'toast' + (joined ? ' joined' : ' left'), text);
    toastBox.append(t);
    setTimeout(function () { t.classList.add('gone'); }, 4000);
    setTimeout(function () { t.remove(); }, 4600);
  }

  function announce(c) {
    if (!state.active || !(state.players || []).length || params.get('demo') === '1' || demoRunning && !liveSession) {
      if (!state.active) known = null;
      return;
    }
    const now = {};
    state.players.forEach(function (p) { now[p.slot] = p.name || ('Player ' + (p.slot + 1)); });
    // Not for everybody who was already there when the page came in
    if (known && c.notices) {
      Object.keys(now).forEach(function (slot) { if (!(slot in known)) toast(now[slot] + ' joined', true); });
      Object.keys(known).forEach(function (slot) { if (!(slot in now)) toast(known[slot] + ' left', false); });
    }
    known = now;
  }

  // ------------------------------------------------------------------ placement

  let dragged = null;

  function placeCard(card, x, y) {
    const freeX = Math.max(0, window.innerWidth - card.el.offsetWidth);
    const freeY = Math.max(0, window.innerHeight - card.el.offsetHeight);
    card.el.style.left = Math.round(Math.max(0, Math.min(1, x)) * freeX) + 'px';
    card.el.style.top = Math.round(Math.max(0, Math.min(1, y)) * freeY) + 'px';
  }

  function place() {
    const c = effectiveConfig();
    // Cards nobody has placed yet line up along the top, next row when the page is full
    const gap = 14;
    let nextLeft = gap, nextTop = gap, rowHeight = 0;
    cards.forEach(function (card) {
      if (!c.free) {
        card.el.style.left = card.el.style.top = '';
        return;
      }
      if (dragged === card) return;
      const x = c[card.key + '_x'], y = c[card.key + '_y'];
      if (typeof x === 'number' && x >= 0 && typeof y === 'number' && y >= 0) {
        placeCard(card, x, y);
        return;
      }
      if (nextLeft > gap && nextLeft + card.el.offsetWidth > window.innerWidth) {
        nextLeft = gap;
        nextTop += rowHeight + gap;
        rowHeight = 0;
      }
      card.el.style.left = nextLeft + 'px';
      card.el.style.top = nextTop + 'px';
      nextLeft += card.el.offsetWidth + gap;
      rowHeight = Math.max(rowHeight, card.el.offsetHeight);
    });
  }

  window.addEventListener('resize', place);

  function makeDraggable(card) {
    const box = card.el;
    let dx = 0, dy = 0;
    box.addEventListener('pointerdown', function (event) {
      dragged = card;
      dx = event.clientX - box.offsetLeft;
      dy = event.clientY - box.offsetTop;
      box.setPointerCapture(event.pointerId);
      box.classList.add('dragging');
      event.preventDefault();
    });
    box.addEventListener('pointermove', function (event) {
      if (dragged !== card) return;
      const freeX = Math.max(0, window.innerWidth - box.offsetWidth);
      const freeY = Math.max(0, window.innerHeight - box.offsetHeight);
      box.style.left = Math.max(0, Math.min(freeX, event.clientX - dx)) + 'px';
      box.style.top = Math.max(0, Math.min(freeY, event.clientY - dy)) + 'px';
    });
    const drop = function () {
      if (dragged !== card) return;
      dragged = null;
      box.classList.remove('dragging');
      const freeX = Math.max(1, window.innerWidth - box.offsetWidth);
      const freeY = Math.max(1, window.innerHeight - box.offsetHeight);
      const x = Math.max(0, Math.min(1, box.offsetLeft / freeX));
      const y = Math.max(0, Math.min(1, box.offsetTop / freeY));
      config[card.key + '_x'] = x;
      config[card.key + '_y'] = y;
      // The app keeps it in the selected preset and tells every open overlay page
      fetch('/layout?' + card.key + '_x=' + x.toFixed(4) + '&' + card.key + '_y=' + y.toFixed(4), { cache: 'no-store' }).catch(function () {});
    };
    box.addEventListener('pointerup', drop);
    box.addEventListener('pointercancel', drop);
  }

  if (editing) {
    const hint = el('div', 'edit-hint', 'Drag each player and the console’s controller to where you want them. It is saved at once, and OBS follows.');
    document.body.append(hint);
  }

  // ------------------------------------------------------------------ voice

  // The app sends everybody's voices as one stream of raw sound (48 kHz, mono,
  // 16 bit). Played here, OBS picks it up from the browser source.
  const VOICE_RATE = 48000;
  let audio = null;
  let voiceRunning = false;
  let playAt = 0;

  function voiceWanted() {
    return effectiveConfig().voice && params.get('demo') !== '1';
  }

  function scheduleSamples(samples) {
    const buffer = audio.createBuffer(1, samples.length, VOICE_RATE);
    buffer.copyToChannel(samples, 0);
    const source = audio.createBufferSource();
    source.buffer = buffer;
    source.connect(audio.destination);
    const now = audio.currentTime;
    // Start (or start over) a little in the future; skip ahead if we fell behind
    if (playAt < now + 0.01 || playAt > now + 0.6) playAt = now + 0.08;
    source.start(playAt);
    playAt += samples.length / VOICE_RATE;
  }

  async function runVoice() {
    if (voiceRunning) return;
    voiceRunning = true;
    try {
      const response = await fetch('/voice', { cache: 'no-store' });
      const reader = response.body.getReader();
      let odd = null;   // a sample cut in half by the chunk boundary
      while (voiceWanted()) {
        const chunk = await reader.read();
        if (chunk.done) break;
        let bytes = chunk.value;
        if (odd) {
          const joined = new Uint8Array(bytes.length + 1);
          joined[0] = odd[0];
          joined.set(bytes, 1);
          bytes = joined;
          odd = null;
        }
        if (bytes.length % 2) {
          odd = bytes.slice(bytes.length - 1);
          bytes = bytes.subarray(0, bytes.length - 1);
        }
        if (!bytes.length || audio.state !== 'running') continue;
        const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
        const samples = new Float32Array(bytes.length / 2);
        for (let i = 0; i < samples.length; i++) samples[i] = view.getInt16(i * 2, true) / 32768;
        scheduleSamples(samples);
      }
      reader.cancel().catch(function () {});
    } catch (e) {
      // The app went away; try again below
    }
    voiceRunning = false;
    if (voiceWanted()) setTimeout(runVoice, 2000);
  }

  function updateVoice() {
    const hint = document.getElementById('sound-hint');
    if (!voiceWanted()) {
      if (hint) hint.remove();
      return;
    }
    if (!audio) {
      const Ctx = window.AudioContext || window.webkitAudioContext;
      if (!Ctx) return;
      audio = new Ctx({ sampleRate: VOICE_RATE });
    }
    if (audio.state === 'suspended') {
      audio.resume().catch(function () {});
      // An ordinary browser wants a click first. OBS does not.
      if (!hint && !window.obsstudio) {
        const h = el('button', 'sound-hint', 'Click to hear the players');
        h.id = 'sound-hint';
        h.onclick = function () { audio.resume().then(function () { h.remove(); }); };
        document.body.append(h);
      }
    } else if (hint) hint.remove();
    runVoice();
  }

  // ------------------------------------------------------------------ chat

  // The latest messages, bottom left, each for half a minute. Messages that were
  // there before the page came in are not shown.
  const CHAT_SHOW_MS = 30000;
  const SLOT_COLORS = ['#b18cff', '#00f2ff', '#34d399', '#f472b6', '#fbbf24', '#60a5fa', '#fb923c', '#a3e635', '#e879f9'];
  const chatBox = el('div', 'chat-box');
  document.body.append(chatBox);
  let chatSeen = null;      // key -> when the page first had it; null until the first list
  let chatList = [];
  const chatLines = new Map(); // key -> element

  function renderChat() {
    const c = effectiveConfig();
    chatBox.className = 'chat-box style-' + c.style;
    chatBox.style.display = c.chat ? '' : 'none';
    const now = Date.now();
    const show = chatList.filter(function (m) {
      return chatSeen && chatSeen[m.key] && now - chatSeen[m.key] < CHAT_SHOW_MS;
    }).slice(-6);
    const keep = new Set(show.map(function (m) { return m.key; }));
    chatLines.forEach(function (line, key) {
      if (!keep.has(key)) { line.remove(); chatLines.delete(key); }
    });
    show.forEach(function (m) {
      let line = chatLines.get(m.key);
      if (!line) {
        line = el('div', 'chat-line');
        const name = el('span', 'chat-name', m.name || ('Player ' + (m.slot + 1)));
        name.style.color = SLOT_COLORS[m.slot % SLOT_COLORS.length];
        line.append(name, el('span', 'chat-text', m.text || ''));
        if (m.title) line.append(el('span', 'chat-link', m.title));
        chatLines.set(m.key, line);
        chatBox.append(line);
      }
      line.classList.toggle('gone', now - chatSeen[m.key] > CHAT_SHOW_MS - 1500);
    });
  }
  setInterval(renderChat, 500);

  // ------------------------------------------------------------------ data

  function connect() {
    const source = new EventSource('/events');
    source.addEventListener('config', function (event) {
      config = Object.assign(config, JSON.parse(event.data));
      render();
      updateVoice();
    });
    source.addEventListener('chat', function (event) {
      const list = JSON.parse(event.data) || [];
      if (chatSeen === null) {
        chatSeen = {};
        list.forEach(function (m) { chatSeen[m.key] = 1; }); // from before: never shown
      } else {
        list.forEach(function (m) { if (!(m.key in chatSeen)) chatSeen[m.key] = Date.now(); });
      }
      chatList = list;
      renderChat();
    });
    source.addEventListener('state', function (event) {
      const next = JSON.parse(event.data);
      // Arranging without a running session: keep the made-up players to have something to drag
      if (editing && !next.active) {
        if (!demoRunning) demo();
        return;
      }
      liveSession = true;
      state = next;
      render();
    });
    source.onerror = function () {
      // The app was closed. The browser reconnects by itself; show nothing meanwhile.
      state = { active: false, players: [] };
      render();
    };
  }

  let demoRunning = false;
  let liveSession = false;

  function demo() {
    const names = ['Nova', 'Pixel', 'Echo'];
    let t = 0;
    demoRunning = true;
    setInterval(function () {
      if (liveSession) return; // a real session took over
      t += 1;
      const holder = Math.floor(t / 90) % names.length;
      const players = names.map(function (name, i) {
        const phase = t / 12 + i * 2;
        const active = i === holder;
        return {
          slot: i, name: name, host: i === 0, you: i === 0, canControl: true, inControl: active,
          controller: i === 1 ? 2 : 1, avatar: '',
          voice: i !== 2, muted: i === 1, camera: false, talking: i === 0 && Math.floor(t / 45) % 2 === 0,
          pad: active ? {
            b: (Math.floor(t / 15) % 4 === i ? BUTTON.cross : 0) | (Math.floor(t / 40) % 3 === 0 ? BUTTON.r1 : 0),
            l2: 0, r2: Math.round(127 + 127 * Math.sin(phase / 3)),
            lx: Math.round(26000 * Math.cos(phase)), ly: Math.round(26000 * Math.sin(phase)), rx: 0, ry: 0,
          } : { b: 0, l2: 0, r2: 0, lx: 0, ly: 0, rx: 0, ry: 0 },
        };
      });
      state = { active: true, mixMode: 1, players: players, combined: players[holder].pad };
      render();
    }, 33);
  }

  fetch('/config').then((r) => r.json()).then(function (c) {
    config = Object.assign(config, c);
    render();
    updateVoice();
  }).catch(function () {});

  if (params.get('demo') === '1') {
    demo();
  } else {
    connect();
  }
  render();
})();
