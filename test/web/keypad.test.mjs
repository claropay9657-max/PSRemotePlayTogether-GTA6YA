// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The keyboard and the mouse as a controller in the browser page (web/js/keypad.js). The page
// has to work out the same controller as the app does, so that playing feels the same in
// both: keypad-vectors.json holds a script of keys, mouse movements and wheel notches
// together with what the app's own code made of each moment (test/web/keypad-vectors.cpp
// writes it), and the page's code is played the same script here.
// Run: node --test test/web

import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { BROWSERS_OWN, CURVES, Converter, FUNCTIONS, LAYOUTS, SECTIONS, SLOTS, buttonInput, curveAt, curveMadeValid, curveName, defaultOptions, inputLabel, keyInput } from '../../web/js/keypad.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const vectors = JSON.parse(fs.readFileSync(path.join(here, 'keypad-vectors.json'), 'utf8'));

test('the page works out the same controller as the app, moment for moment', () => {
  let states = 0;
  for (const run of vectors.runs) {
    const c = new Converter();
    let step = 0;
    for (const op of run.ops) {
      step++;
      switch (op[0]) {
        case 'o': c.setOptions(op[1]); break;
        case 'b': c.setBindings(op[1]); break;
        case 'p': c.press(op[1], op[2] === 1); break;
        case 'm': c.move(op[1], op[2], op[3]); break;
        case 'w': c.wheel(op[1], op[2]); break;
        case 'r': c.releaseAll(); break;
        case 's': {
          const s = c.state(op[1]);
          assert.deepEqual([s.buttons, s.l2, s.r2, s.lx, s.ly, s.rx, s.ry], op[2], `${run.name}, step ${step}, at ${op[1]} ms`);
          assert.equal(c.busy(op[1]) ? 1 : 0, op[3], `${run.name}, step ${step}: whether more is coming`);
          states++;
          break;
        }
        default: assert.fail(`an unknown step: ${op[0]}`);
      }
    }
  }
  assert.equal(states, vectors.states);
  assert.ok(states > 2000, `only ${states} moments`);
});

test('the layouts the page comes with bind what the app-made script binds', () => {
  // The script was written with the same two layouts: if the page's change, the script has to be made again
  const bound = (name) => vectors.runs.find((r) => r.name === name).ops.find((op) => op[0] === 'b')[1];
  assert.deepEqual(bound('the keys as before'), normal(LAYOUTS.classic));
  assert.deepEqual(bound('mouse and WASD'), normal(LAYOUTS.shooter));
});

/** A layout with its functions in the order the app has them, as the script writes it. */
function normal(layout) {
  const out = {};
  for (const [id] of FUNCTIONS) if (layout[id]) out[id] = layout[id];
  return out;
}

test('every function is known, and a layout binds each input once', () => {
  const ids = FUNCTIONS.map(([id]) => id);
  assert.equal(new Set(ids).size, ids.length);
  for (const [name, layout] of Object.entries(LAYOUTS)) {
    const seen = new Set();
    for (const [id, inputs] of Object.entries(layout)) {
      assert.ok(ids.includes(id), `${name}: ${id} is no function`);
      assert.ok(inputs.length >= 1 && inputs.length <= SLOTS);
      for (const input of inputs) {
        assert.ok(!seen.has(input), `${name}: ${input} is bound twice`);
        seen.add(input);
      }
    }
    // What a browser keeps for itself is not bound: Esc gives the mouse back, Ctrl with W closes the tab
    for (const input of seen) {
      assert.ok(!BROWSERS_OWN.has(input) && !/^k:(Control|Meta)/.test(input), `${name}: ${input} is the browser's`);
    }
    // The key that catches the mouse and lets it go, as in the app
    assert.deepEqual(layout.mouse, ['k:F8']);
  }
});

test('the cards of the settings show every function once, as the app groups them', () => {
  const shown = SECTIONS.flatMap((section) => section.keys.map(([id]) => id));
  assert.deepEqual(shown.slice().sort(), FUNCTIONS.map(([id]) => id).sort());
  assert.deepEqual(SECTIONS.map((section) => section.title),
    ['Left stick', 'Right stick', 'Face buttons', 'D-pad', 'Shoulders and triggers', 'Middle', 'Walking and the mouse']);
});

test('the key for the mouse is nobody\'s button', () => {
  const c = new Converter();
  c.setBindings(LAYOUTS.shooter);
  assert.equal(c.press('k:F8', true), 'mouse');
  assert.deepEqual(c.state(1), { buttons: 0, l2: 0, r2: 0, lx: 0, ly: 0, rx: 0, ry: 0 });
  assert.equal(c.press('k:F8', false), 'mouse');
});

test('bindings that make no sense are left out', () => {
  const c = new Converter();
  c.setBindings({ cross: ['k:KeyK', 'k:KeyK', 'nonsense', 'm:9', 'k:KeyA', 'k:KeyB', 'k:KeyC'], circle: ['k:KeyK', 'w:up'], nothing: ['k:KeyZ'] });
  assert.deepEqual(c.getBindings().cross, ['k:KeyK', 'k:KeyA', 'k:KeyB']); // three at most, each once
  assert.deepEqual(c.getBindings().circle, ['w:up']); // K is Cross already
  assert.equal(c.getBindings().nothing, undefined);
  assert.equal(c.bound('k:KeyK'), true);
  assert.equal(c.bound('w:up'), false); // the mouse does not aim
  c.setOptions({ aim: true });
  assert.equal(c.bound('w:up'), true);
});

test('when the mouse comes back to the page its buttons are let go, the keys are not', () => {
  const c = new Converter();
  c.setBindings(LAYOUTS.shooter);
  c.setOptions({ aim: true });
  c.press('k:KeyW', true);
  c.press('m:1', true);
  c.move(30, 0, 10);
  assert.equal(c.state(12).r2, 255);
  assert.ok(c.state(12).rx > 0);
  c.releaseMouse();
  const s = c.state(13);
  assert.equal(s.r2, 0);
  assert.equal(s.rx, 0);
  assert.equal(s.ly, -32767);
});

test('the curves and what is set stay within bounds', () => {
  assert.deepEqual(curveMadeValid([0.5, 0.2, 1.4, -1]), [0.5, 0.5, 1, 1]);
  assert.deepEqual(curveMadeValid(undefined), [0, 0, 0, 0]);
  assert.equal(curveAt(CURVES.even, 0.5), 0.5);
  assert.equal(curveAt(CURVES.even, -1), 0);
  assert.equal(curveAt(CURVES.even, 3), 1);
  assert.equal(curveName(CURVES.fine), 'fine');
  assert.equal(curveName([0.1, 0.2, 0.3, 0.4]), 'custom');
  const c = new Converter();
  c.setOptions({ sensX: 'nonsense', sensY: 99, deadZone: 7, smoothingMs: 0, aimButton: 'r2', curve: 'x' });
  assert.equal(c.options.sensX, 0.05);
  assert.equal(c.options.sensY, 10);
  assert.equal(c.options.deadZone, 0.5);
  assert.equal(c.options.smoothingMs, 1);
  assert.equal(c.options.aimButton, '');
  assert.deepEqual(c.options.curve, CURVES.even); // what is no curve at all is the even one
  assert.equal(defaultOptions().aim, false);
});

test('inputs are named the way a key cap shows them', () => {
  assert.equal(inputLabel('k:KeyW'), 'W');
  assert.equal(inputLabel('k:Digit1'), '1');
  assert.equal(inputLabel('k:ShiftLeft'), 'Shift');
  assert.equal(inputLabel('k:Enter'), 'Return');
  assert.equal(inputLabel('k:F8'), 'F8');
  assert.equal(inputLabel('k:AltLeft', ''), 'Alt');
  assert.equal(inputLabel('k:AltLeft', 'mac'), 'Option');
  assert.equal(inputLabel('k:ControlLeft', 'mac'), 'Control');
  assert.equal(inputLabel('k:MetaLeft', 'windows'), 'Win');
  assert.equal(inputLabel('k:ArrowUp'), '↑');
  assert.equal(inputLabel('k:Numpad5'), 'Num 5');
  assert.equal(inputLabel('m:1'), 'Left click');
  assert.equal(inputLabel('m:2'), 'Right click');
  assert.equal(inputLabel('m:3'), 'Middle click');
  assert.equal(inputLabel('m:4'), 'Mouse 4');
  assert.equal(inputLabel('w:down'), 'Wheel down');
  assert.equal(keyInput({ code: 'KeyW' }), 'k:KeyW');
  assert.equal(keyInput({ code: '' }), '');
  // A browser counts left, middle, right, back, forward
  assert.deepEqual([0, 1, 2, 3, 4].map((button) => buttonInput({ button })), ['m:1', 'm:3', 'm:2', 'm:4', 'm:5']);
});
