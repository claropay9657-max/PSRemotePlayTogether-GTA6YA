// One controller, drawn with a CryptoPad skin. Runs inside an <iframe> of the
// overlay page, which passes the live controller state in with postMessage.
//
// Adapted from CryptoPad's static/controller.js (https://github.com/Crypto90/CryptoPad):
// same drawing code, fed by the app instead of a socket.io connection.
(function () {
  const STICK_OFFSET = 22;
  const STICK_CURVING = 1;
  const ANALOGUE_STICK_THRESHOLD = 0.12;
  const ROTATE_BOUNDARY = 120;

  const controller = document.getElementById('gamepad-0');

  function lineDistance(a, b) {
    return Math.sqrt(a * a + b * b);
  }

  function updateAxis(value, valueV, stickId) {
    if (!controller) return;
    const active = lineDistance(value, valueV) >= ANALOGUE_STICK_THRESHOLD;

    const stickEl = controller.querySelector('[data-name="' + stickId + '"]');
    if (stickEl) {
      const offsetH = active ? value * STICK_OFFSET : 0;
      const offsetV = active ? valueV * STICK_OFFSET : 0;
      stickEl.style.marginLeft = offsetH + 'px';
      stickEl.style.marginTop = offsetV + 'px';
      if (STICK_CURVING)
        stickEl.style.transform = 'rotateX(' + (offsetV * -1) + 'deg) rotateY(' + offsetH + 'deg)';
    }

    const wheelEl = controller.querySelector('[data-name="' + stickId + '-wheel"]');
    if (wheelEl)
      wheelEl.style.transform = 'rotate(' + ((active ? value : 0) * ROTATE_BOUNDARY) + 'deg)';
  }

  function setPressed(id, isPressed) {
    const el = document.getElementById(id);
    if (el) el.classList.toggle('pressed', !!isPressed);
  }

  function apply(s) {
    if (controller) controller.classList.remove('invisible');

    setPressed('a', s.a);
    setPressed('b', s.b);
    setPressed('x', s.x);
    setPressed('y', s.y);
    setPressed('lb', s.lb);
    setPressed('rb', s.rb);
    setPressed('select', s.select);
    setPressed('start', s.start);
    setPressed('meta', s.meta);
    setPressed('touchpad', s.touchpad);

    const ltEl = document.getElementById('lt-fill');
    const rtEl = document.getElementById('rt-fill');
    if (ltEl) ltEl.style.opacity = Math.max(0, Math.min(1, s.lt));
    if (rtEl) rtEl.style.opacity = Math.max(0, Math.min(1, s.rt));

    setPressed('dpad-up', s.dpad_up);
    setPressed('dpad-down', s.dpad_down);
    setPressed('dpad-left', s.dpad_left);
    setPressed('dpad-right', s.dpad_right);

    updateAxis(s.left_stick_x, s.left_stick_y, 'stick-1');
    updateAxis(s.right_stick_x, s.right_stick_y, 'stick-2');
    setPressed('left-stick', s.ls);
    setPressed('right-stick', s.rs);
  }

  window.addEventListener('message', function (event) {
    if (event.data && event.data.type === 'pad' && event.data.standard)
      apply(event.data.standard);
  });

  // The skins fade the controller in on the first data; show it resting until then
  if (controller) {
    controller.classList.remove('invisible');
    controller.style.transition = 'none';
  }
  if (window.parent !== window)
    window.parent.postMessage({ type: 'pad-ready' }, '*');
})();
