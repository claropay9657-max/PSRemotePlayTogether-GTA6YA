// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// What the intro film (IntroFilm.qml) moves with. Nothing in the film runs an animation of
// its own: every position, size and colour is worked out from the film's time `t` (seconds)
// with these, so any moment can be shown, jumped to and looked at on its own.

.pragma library

function clamp(x, a, b) {
    return x < a ? a : x > b ? b : x;
}

/** How far `t` is through [a, b]: 0 before, 1 after. */
function seg(t, a, b) {
    return clamp((t - a) / (b - a), 0, 1);
}

function mix(a, b, k) {
    return a + (b - a) * k;
}

function outCubic(k) {
    const u = 1 - k;
    return 1 - u * u * u;
}

function inCubic(k) {
    return k * k * k;
}

function inOutCubic(k) {
    return k < 0.5 ? 4 * k * k * k : 1 - Math.pow(-2 * k + 2, 3) / 2;
}

function outQuint(k) {
    return 1 - Math.pow(1 - k, 5);
}

function inOutSine(k) {
    return 0.5 - 0.5 * Math.cos(Math.PI * k);
}

/** Past the end a little and back, as something that lands. */
function outBack(k, s) {
    if (s === undefined)
        s = 1.70158;
    const u = k - 1;
    return 1 + (s + 1) * u * u * u + s * u * u;
}

/** 0 to 1 from `at` on within `dur` seconds, arriving softly. */
function enter(t, at, dur) {
    return outCubic(seg(t, at, at + (dur === undefined ? 0.6 : dur)));
}

/** 1 to 0 from `at` on within `dur` seconds, leaving quickly. */
function leave(t, at, dur) {
    return 1 - inCubic(seg(t, at, at + (dur === undefined ? 0.4 : dur)));
}

/** There between `a` and `b`: fades in after `a`, out before `b`. */
function during(t, a, b, fadeIn, fadeOut) {
    if (fadeIn === undefined)
        fadeIn = 0.5;
    if (fadeOut === undefined)
        fadeOut = 0.4;
    return Math.min(outCubic(seg(t, a, a + fadeIn)), 1 - inCubic(seg(t, b - fadeOut, b)));
}

/** A spring let go at `at`: 0, then swinging up to 1 and settling. For things that pop in. */
function pop(t, at, stiffness, damping) {
    const k = t - at;
    if (k <= 0)
        return 0;
    return 1 - Math.exp(-(damping === undefined ? 7.5 : damping) * k) * Math.cos((stiffness === undefined ? 11 : stiffness) * k);
}

/** 0, up to 1 and back to 0 within `dur` seconds from `at`. */
function bump(t, at, dur) {
    const k = seg(t, at, at + dur);
    return k <= 0 || k >= 1 ? 0 : Math.sin(Math.PI * k);
}

/** Up at once at `at`, then dying away: a flash, a ring going out. */
function decay(t, at, rate) {
    const k = t - at;
    return k < 0 ? 0 : Math.exp(-(rate === undefined ? 5 : rate) * k);
}

/** A number between 0 and 1 that is always the same for the same `n`: for stars and sparks. */
function hash(n) {
    const s = Math.sin(n * 127.1 + 311.7) * 43758.5453;
    return s - Math.floor(s);
}

/**
 * Along keys [[time, value], ...] (times rising): the value at `t`, easing in and out
 * between two keys. Before the first key its value, after the last one its value.
 */
function track(t, keys) {
    if (t <= keys[0][0])
        return keys[0][1];
    for (let i = 1; i < keys.length; ++i) {
        if (t < keys[i][0]) {
            const k = inOutCubic((t - keys[i - 1][0]) / (keys[i][0] - keys[i - 1][0]));
            return keys[i - 1][1] + (keys[i][1] - keys[i - 1][1]) * k;
        }
    }
    return keys[keys.length - 1][1];
}

/** A point on the curve from (x0, y0) to (x1, y1) that bends towards (cx, cy); k from 0 to 1. */
function quadX(x0, cx, x1, k) {
    const u = 1 - k;
    return u * u * x0 + 2 * u * k * cx + k * k * x1;
}

function lerpColor(a, b, k) {
    return Qt.rgba(a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k, a.a + (b.a - a.a) * k);
}

/** `c` with another opacity. */
function alpha(c, a) {
    return Qt.rgba(c.r, c.g, c.b, a);
}

/**
 * `text` in two lines, broken after the comma or the full stop nearest to its middle: for a
 * line too long for where it stands. As it is where it has neither near its middle.
 */
function halved(text) {
    const middle = text.length / 2;
    let at = -1;
    for (let i = 1; i < text.length - 1; ++i) {
        if ((text[i - 1] === "," || text[i - 1] === ".") && text[i] === " " && (at < 0 || Math.abs(i - middle) < Math.abs(at - middle)))
            at = i;
    }
    if (at < text.length * 0.25 || at > text.length * 0.75)
        return text;
    return text.slice(0, at) + "\n" + text.slice(at + 1);
}
