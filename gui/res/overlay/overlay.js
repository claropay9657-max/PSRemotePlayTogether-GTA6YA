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
    players: true, pads: true, combined: true, cams: true, voice: true,
    layout: 'row', scale: 30, skin: 'auto', skins: ['PS5'],
  };
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
    if (params.has('layout')) c.layout = params.get('layout') === 'column' ? 'column' : 'row';
    if (params.has('scale')) c.scale = Math.max(10, Math.min(100, parseInt(params.get('scale'), 10) || c.scale));
    if (params.has('skin')) c.skin = params.get('skin');
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
    document.body.classList.toggle('preview', params.get('bg') === 'dark');
    root.className = 'layout-' + c.layout + (state.active ? '' : ' idle');

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
    wanted.forEach(function (w, index) {
      const card = cards.get(w.key) || createCard(w.key);
      if (root.children[index] !== card.el)
        root.insertBefore(card.el, root.children[index] || null);

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

  // ------------------------------------------------------------------ data

  function connect() {
    const source = new EventSource('/events');
    source.addEventListener('config', function (event) {
      config = Object.assign(config, JSON.parse(event.data));
      render();
      updateVoice();
    });
    source.addEventListener('state', function (event) {
      state = JSON.parse(event.data);
      render();
    });
    source.onerror = function () {
      // The app was closed. The browser reconnects by itself; show nothing meanwhile.
      state = { active: false, players: [] };
      render();
    };
  }

  function demo() {
    const names = ['Nova', 'Pixel', 'Echo'];
    let t = 0;
    setInterval(function () {
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

  if (params.get('demo') === '1') demo();
  else connect();
  render();
})();
