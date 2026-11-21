#!/usr/bin/env python3
"""
Alert sounds for PSRemotePlayTogether's Twitch alerts.

Everything is synthesized here from scratch: no samples, no presets, nothing
borrowed, so the sounds can ship with the AGPL project without questions.
Each alert has its own instrument and its own gesture, matching what happened:

  follow         marimba "hel-lo" after a bubble pop: someone new says hi
  subscribe      soft impact, a synth arpeggio up into a bright chord: a celebration
  resub          warm electric piano, a homecoming cadence: welcome back
  giftsubs       ribbon swish, box pop, confetti and a music box: presents
  bits           a zip and glassy crystal tones: gems lighting up
  raid           riser, war drums, a deep impact and horns: a crowd storms in
  channelpoints  a tactile button and a digital blip: something happened in the game
  hypetrain      chugging that speeds up, then a train whistle: all aboard
  poll           three ticks (the options) and a questioning chime
  prediction     a reversed swell, a whole-tone harp and glass: what will happen?
  goal           a bar filling up and a vibraphone chord: target reached
  charity        a heartbeat and a warm nylon guitar: kindness
  shoutout       a swoosh and two bright brass stabs: hey, look over here
  adbreak        a soft lofi piano that winds down like a stopped tape: short break
  chat           a tiny soft two-note drop, quieter than the alerts: a new chat message

    python3 make_alert_sounds.py OUT_DIR [name ...]

Writes 48 kHz stereo 24-bit WAV files, each mastered to the same loudness
(EBU R128 / ITU BS.1770 integrated loudness) with peaks below -1.5 dBFS.
Needs only numpy.
"""
import os
import sys
import wave

import numpy as np
from numpy.lib.stride_tricks import sliding_window_view

SR = 48000
_RNG = np.random.default_rng(20261121)


# ================================================================ basics

def rng(seed):
    return np.random.default_rng(seed)


def n_of(dur):
    return max(1, int(round(dur * SR)))


def tt(dur):
    return np.arange(n_of(dur)) / SR


_NAMES = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def N(name):
    """'A4' -> 440.0; sharps '#', flats 'b'."""
    letter, rest, acc = name[0], name[1:], 0
    while rest and rest[0] in '#b':
        acc += 1 if rest[0] == '#' else -1
        rest = rest[1:]
    midi = 12 * (int(rest) + 1) + _NAMES[letter] + acc
    return 440.0 * 2 ** ((midi - 69) / 12)


def _arr(x, n):
    return np.broadcast_to(np.asarray(x, float), (n,)).astype(float)


def _norm(y, peak=1.0):
    m = np.max(np.abs(y))
    return y * (peak / m) if m > 0 else y


def _npow2(n):
    return 1 << int(n - 1).bit_length()


def stereo(x):
    x = np.asarray(x, float)
    return x if x.ndim == 2 else np.stack([x, x], 1) * np.sqrt(0.5)


def env_hit(t, a=0.002, d=0.5, h=0.0):
    """Smooth attack, optional hold, exponential decay."""
    e = np.exp(-np.maximum(t - a - h, 0) / d)
    if a > 0:
        e = e * np.sin(0.5 * np.pi * np.clip(t / a, 0, 1)) ** 2
    return e


def env_adsr(t, a, d, s, rel_at, r):
    up = np.sin(0.5 * np.pi * np.clip(t / a, 0, 1)) ** 2
    body = s + (1 - s) * np.exp(-np.maximum(t - a, 0) / d)
    e = np.where(t < a, up, body)
    return e * np.where(t > rel_at, np.exp(-np.maximum(t - rel_at, 0) / r), 1.0)


def fade_tail(y, ms=10):
    k = min(len(y), int(ms * SR / 1000))
    if k > 1:
        w = np.cos(np.linspace(0, np.pi / 2, k)) ** 2
        y = y.copy()
        y[-k:] *= w if y.ndim == 1 else w[:, None]
    return y


# ================================================================ oscillators

def phase(f, n, ph0=0.0):
    return np.cumsum(_arr(f, n)) / SR + ph0


def sine(f, n, ph0=0.0):
    return np.sin(2 * np.pi * phase(f, n, ph0))


def _blep(p, dt):
    y = np.zeros_like(p)
    m = p < dt
    x = p[m] / dt[m]
    y[m] = x + x - x * x - 1
    m = p > 1 - dt
    x = (p[m] - 1) / dt[m]
    y[m] = x * x + x + x + 1
    return y


def saw(f, n, r=None, ph0=None):
    """Band-limited sawtooth (PolyBLEP), free of aliasing whistles."""
    dt = _arr(f, n) / SR
    if ph0 is None:
        ph0 = (_RNG if r is None else r).random()
    p = (np.cumsum(dt) + ph0) % 1.0
    return 2 * p - 1 - _blep(p, dt)


def pulse(f, n, width=0.5, ph0=0.0):
    """Band-limited pulse from two saws, free of DC."""
    dt = _arr(f, n) / SR
    p = (np.cumsum(dt) + ph0) % 1.0
    q = (p + 1 - width) % 1.0
    return (2 * p - 1 - _blep(p, dt)) - (2 * q - 1 - _blep(q, dt))


def fm(fc, n, ratio, index, ph0=0.0):
    fc = _arr(fc, n)
    return np.sin(2 * np.pi * phase(fc, n, ph0) + _arr(index, n) * np.sin(2 * np.pi * phase(fc * ratio, n)))


def noise(n, r=None, color='white'):
    r = _RNG if r is None else r
    w = r.standard_normal(n)
    if color == 'pink' and n > 4:
        W = np.fft.rfft(w)
        f = np.fft.rfftfreq(n, 1 / SR)
        f[0] = f[1]
        w = np.fft.irfft(W / np.sqrt(f / 1000), n)
    return w / (np.std(w) + 1e-12)


def supersaw(f, n, voices=7, spread=18.0, r=None):
    offs = np.array([-1.0, -0.64, -0.3, 0.0, 0.27, 0.61, 0.97])[:voices] * spread
    pans = [-0.85, 0.6, -0.35, 0.0, 0.35, -0.6, 0.85][:voices]
    st = np.zeros((n, 2))
    for o, p in zip(offs, pans):
        st += pan(saw(_arr(f, n) * 2 ** (o / 1200), n, r), p)
    return st / np.sqrt(voices)


# ================================================================ filters and effects

def svf(x, fc, q=0.707, mode='lp'):
    """Zero-delay-feedback state variable filter; the cutoff may move per sample."""
    x = np.asarray(x, float)
    if x.ndim == 2:
        return np.stack([svf(x[:, c], fc, q, mode) for c in range(x.shape[1])], 1)
    n = len(x)
    g = np.tan(np.pi * np.clip(_arr(fc, n), 10, 0.45 * SR) / SR)
    k = 1.0 / q
    a1 = 1 / (1 + g * (g + k))
    a2 = g * a1
    a3 = g * a2
    xs, A1, A2, A3 = x.tolist(), a1.tolist(), a2.tolist(), a3.tolist()
    lo = [0.0] * n
    bd = [0.0] * n
    ic1 = ic2 = 0.0
    for i in range(n):
        v3 = xs[i] - ic2
        v1 = A1[i] * ic1 + A2[i] * v3
        v2 = ic2 + A2[i] * ic1 + A3[i] * v3
        ic1 = 2 * v1 - ic1
        ic2 = 2 * v2 - ic2
        lo[i] = v2
        bd[i] = v1
    lo = np.array(lo)
    bd = np.array(bd)
    if mode == 'lp':
        return lo
    if mode == 'bp':
        return bd * k
    return x - k * bd - lo


def eq(x, curve):
    """A static, zero-phase EQ given as a magnitude curve over frequency."""
    x = np.asarray(x, float)
    n = len(x)
    M = _npow2(n + 8192)
    X = np.fft.rfft(x, M, axis=0)
    H = curve(np.maximum(np.fft.rfftfreq(M, 1 / SR), 1e-3))
    return np.fft.irfft(X * (H[:, None] if x.ndim == 2 else H), M, axis=0)[:n]


def hp(fc, order=2):
    return lambda f: 1 / np.sqrt(1 + (fc / f) ** (2 * order))


def lp(fc, order=2):
    return lambda f: 1 / np.sqrt(1 + (f / fc) ** (2 * order))


def bell_db(fc, db, octaves=1.0):
    return lambda f: 10 ** (db * np.exp(-0.5 * (np.log2(f / fc) / (octaves / 2)) ** 2) / 20)


def chain(*curves):
    return lambda f: np.prod([c(f) for c in curves], axis=0)


def pan(x, p=0.0):
    """Constant-power pan for mono, balance for stereo; p may move (an array)."""
    x = np.asarray(x, float)
    a = (np.clip(p, -1, 1) + 1) * np.pi / 4
    if x.ndim == 2:
        return np.stack([x[:, 0] * np.cos(a) * np.sqrt(2), x[:, 1] * np.sin(a) * np.sqrt(2)], 1)
    return np.stack([x * np.cos(a), x * np.sin(a)], 1)


def move(x, p0, p1):
    n = len(x)
    return pan(x, np.linspace(p0, p1, n))


def chorus(st, rate=0.5, depth_ms=2.5, base_ms=9.0, mix=0.4):
    st = stereo(st)
    n = len(st)
    idx = np.arange(n)
    t = idx / SR
    out = np.empty_like(st)
    for c, ph in ((0, 0.0), (1, np.pi / 2)):
        d = (base_ms + depth_ms * np.sin(2 * np.pi * rate * t + ph)) * SR / 1000
        wet = np.interp(idx - d, idx, st[:, c], left=0.0, right=0.0)
        out[:, c] = (1 - mix) * st[:, c] + mix * wet
    return out


def autopan(st, rate, depth):
    st = stereo(st)
    t = np.arange(len(st)) / SR
    s = np.sin(2 * np.pi * rate * t)
    return np.stack([st[:, 0] * (1 - depth * (0.5 + 0.5 * s)), st[:, 1] * (1 - depth * (0.5 - 0.5 * s))], 1)


def warm(x, drive=1.5):
    """Gentle tape-like saturation for signals that peak around 1."""
    return np.tanh(x * drive) / np.tanh(drive)


def pingpong(st, time, fb=0.4, repeats=4, tone=5000, mix=0.3):
    st = stereo(st)
    n = len(st)
    out = np.zeros((n + n_of(time * repeats), 2))
    out[:n] += st
    sig = st.mean(1)
    for k in range(1, repeats + 1):
        sig = eq(sig, lp(tone, 1))
        d = n_of(time * k)
        out[d:d + n, (k + 1) % 2] += mix * fb ** (k - 1) * sig
    return out


def tape_stop(st, start, length):
    """The tape slows down to a halt: pitch and speed fall together."""
    n = len(st)
    t = np.arange(n) / SR
    u = np.clip((t - start) / length, 0, 1)
    pos = np.cumsum((1 - u) ** 1.7) - 1
    out = np.stack([np.interp(pos, np.arange(n), st[:, c]) for c in range(2)], 1)
    out *= ((1 - u) ** 0.45)[:, None]
    out[t >= start + length] = 0
    return out


# ================================================================ reverb

def make_ir(rt60, predelay=0.015, hf=0.5, lf=1.15, build=0.03, early=10, air=12000, seed=1):
    """A synthetic room: early reflections, then a dense tail whose highs die sooner."""
    r = rng(seed)
    n = n_of(predelay + rt60 * 1.15 + 0.05)
    t = np.arange(n) / SR
    M = _npow2(n)
    f = np.maximum(np.fft.rfftfreq(M, 1 / SR), 1.0)
    cross = [250, 1000, 3000, 7000]
    mult = [lf, 1.0, 0.85, 0.45 + 0.4 * hf, 0.22 + 0.45 * hf]
    steps = [1 / (1 + np.exp(-(np.log2(f) - np.log2(c)) / 0.18)) for c in cross]
    masks = [1 - steps[0]] + [steps[i] - steps[i + 1] for i in range(len(cross) - 1)] + [steps[-1]]
    airc = lp(air, 1)(f)
    late = np.maximum(t - predelay, 0)
    ir = np.zeros((n, 2))
    for c in range(2):
        W = np.fft.rfft(r.standard_normal(n), M)
        tail = np.zeros(n)
        for m, mk in zip(mult, masks):
            tail += np.fft.irfft(W * mk * airc, M)[:n] * np.exp(-6.9078 * late / (rt60 * m))
        tail *= np.clip((t - predelay) / build, 0, 1) ** 2
        er = np.zeros(n)
        for k in range(early):
            i = n_of(r.uniform(0.002, predelay + 0.04))
            if i < n:
                er[i] += r.uniform(0.3, 1.0) * (1 - 0.5 * k / early) * r.choice([-1, 1])
        er = eq(er, lp(7000, 1))
        tail /= np.sqrt(np.sum(tail ** 2)) + 1e-12
        er /= np.sqrt(np.sum(er ** 2)) + 1e-12
        ir[:, c] = tail + 0.4 * er
    return fade_tail(ir, 1000 * len(ir) / SR * 0.15) / np.sqrt(np.mean(np.sum(ir ** 2, 0)))


_IRS = {}
_IR_SPECS = {
    'small': dict(rt60=0.45, predelay=0.005, hf=0.3, seed=11),
    'room': dict(rt60=0.95, predelay=0.012, hf=0.45, seed=12),
    'plate': dict(rt60=1.7, predelay=0.008, hf=0.85, lf=0.9, build=0.01, seed=13),
    'hall': dict(rt60=2.4, predelay=0.022, hf=0.5, seed=14),
    'epic': dict(rt60=3.3, predelay=0.03, hf=0.45, lf=1.3, seed=15),
}


def ir(name):
    if name not in _IRS:
        _IRS[name] = make_ir(**_IR_SPECS[name])
    return _IRS[name]


def convolve(x, h):
    n = len(x) + len(h) - 1
    M = _npow2(n)
    return np.fft.irfft(np.fft.rfft(x, M, axis=0) * np.fft.rfft(h, M, axis=0), M, axis=0)[:n]


# ================================================================ mastering

_K1 = ([1.53512485958697, -2.69169618940638, 1.19839281085285], [1.0, -1.69065929318241, 0.73248077421585])
_K2 = ([1.0, -2.0, 1.0], [1.0, -1.99004745483398, 0.99007225036621])


def lufs(y):
    """Integrated loudness after ITU-R BS.1770 (K-weighting, 400 ms blocks, gates)."""
    n = len(y)
    M = _npow2(n + SR)
    z = np.exp(-2j * np.pi * np.fft.rfftfreq(M, 1 / SR) / SR)

    def H(b, a):
        return (b[0] + b[1] * z + b[2] * z * z) / (a[0] + a[1] * z + a[2] * z * z)

    yk = np.fft.irfft(np.fft.rfft(y, M, axis=0) * (H(*_K1) * H(*_K2))[:, None], M, axis=0)[:n + SR // 5]
    blk, hop = n_of(0.4), n_of(0.1)
    if len(yk) < blk:
        yk = np.pad(yk, ((0, blk - len(yk)), (0, 0)))
    z2 = np.array([np.sum(np.mean(yk[i:i + blk] ** 2, 0)) for i in range(0, len(yk) - blk + 1, hop)])
    lv = -0.691 + 10 * np.log10(z2 + 1e-20)
    gated = z2[lv > -70]
    if not len(gated):
        return -100.0
    rel = -0.691 + 10 * np.log10(np.mean(gated)) - 10
    return -0.691 + 10 * np.log10(np.mean(z2[(lv > -70) & (lv > rel)]))


def limiter(y, ceiling_db=-1.5, look=0.0015, release=0.07):
    """Offline look-ahead peak limiter; returns the signal and its deepest gain reduction in dB."""
    c = 10 ** (ceiling_db / 20)
    need = np.minimum(1.0, c / np.maximum(np.max(np.abs(y), axis=1), 1e-12))
    w = max(1, n_of(look))
    mn = sliding_window_view(np.pad(need, w, constant_values=1.0), 2 * w + 1).min(axis=1)
    a = np.exp(-1 / (release * SR))
    g = mn.tolist()
    cur = 1.0
    for i in range(len(g)):
        v = g[i]
        cur = v if v < cur else a * cur + (1 - a) * v
        g[i] = cur
    g = np.convolve(np.pad(np.array(g), w, mode='edge'), np.ones(2 * w + 1) / (2 * w + 1), mode='valid')
    return y * g[:, None], -20 * np.log10(np.min(g))


def master(y, target=-16.0, ceiling=-1.5, hp_hz=30):
    y = eq(y, hp(hp_hz, 2))
    gr = 0.0
    for _ in range(4):
        y = y * 10 ** ((target - lufs(y)) / 20)
        y, gr = limiter(y, ceiling)
    level = np.max(np.abs(y), axis=1)
    loud = np.nonzero(level > 10 ** (-60 / 20))[0]
    end = min(len(y), (loud[-1] if len(loud) else len(y)) + n_of(0.02))
    y = fade_tail(y[:end], 250)
    return y, gr


class Mix:
    """Layers placed in time and space, a send to one shared room, then mastering."""

    def __init__(self, dur):
        self.n = n_of(dur)
        self.dry = np.zeros((self.n, 2))
        self.send = np.zeros((self.n, 2))

    def add(self, sig, at=0.0, p=0.0, gain=1.0, send=0.0):
        sig = np.asarray(sig, float)
        st = pan(sig, p) if (sig.ndim == 1 or p) else sig
        st = st * gain
        i = n_of(at) if at > 0 else 0
        e = i + len(st)
        if e > self.n:
            # Grow instead of cutting a ringing note short, which would click
            self.dry = np.pad(self.dry, ((0, e - self.n), (0, 0)))
            self.send = np.pad(self.send, ((0, e - self.n), (0, 0)))
            self.n = e
        self.dry[i:e] += st
        if send:
            self.send[i:e] += st * send

    def render(self, room=None, ret=1.0, post=None, target=-16.0):
        out = self.dry
        if room is not None and np.any(self.send):
            s = eq(self.send, chain(hp(200, 2), lp(10000, 1)))
            s = np.stack([0.72 * s[:, 0] + 0.28 * s[:, 1], 0.28 * s[:, 0] + 0.72 * s[:, 1]], 1)
            wet = convolve(s, ir(room)) * ret
            full = np.zeros((max(self.n, len(wet)), 2))
            full[:self.n] += self.dry
            full[:len(wet)] += wet
            out = full
        if post:
            out = post(out)
        return master(out, target)


# ================================================================ instruments

def struck(f, dur, partials, vel=0.8, bright=0.7, attack=0.002, knock=0.0, knock_lp=2500.0, seed=0):
    """A struck object as a sum of decaying modes; softer mallets damp the high ones."""
    t = tt(dur)
    n = len(t)
    r = rng(seed)
    y = np.zeros(n)
    for ratio, a, d in partials:
        fr = f * ratio
        if fr > 0.45 * SR:
            continue
        amp = a * bright ** np.log2(max(ratio, 1.0))
        y += amp * np.sin(2 * np.pi * fr * t + r.uniform(0, 0.4)) * np.exp(-t / d)
    if attack > 0:
        y *= np.sin(0.5 * np.pi * np.clip(t / attack, 0, 1)) ** 2
    if knock:
        y += knock * eq(noise(n, r) * np.exp(-t / 0.01), chain(hp(150, 1), lp(knock_lp, 2)))
    return fade_tail(_norm(y) * vel, 6)


def marimba(f, vel=0.8, seed=0):
    tau = 0.42 * (523 / f) ** 0.45
    p = [(1.0, 1.0, tau), (2.0, 0.03, tau * 0.6), (3.98, 0.5, tau * 0.16), (9.85, 0.25, tau * 0.05)]
    return struck(f, tau * 6, p, vel, bright=0.45 + 0.3 * vel, attack=0.002, knock=0.06, knock_lp=1600, seed=seed)


def vibraphone(f, vel=0.8, motor=5.3, depth=0.32, seed=0):
    tau = 1.6 * (440 / f) ** 0.35
    p = [(1.0, 1.0, tau), (3.99, 0.32, tau * 0.2), (9.9, 0.1, tau * 0.06)]
    y = struck(f, min(4.0, tau * 3.5), p, vel, bright=0.5 + 0.35 * vel, attack=0.0018, knock=0.02, knock_lp=4500, seed=seed)
    t = np.arange(len(y)) / SR
    return np.stack([y * (1 - depth * (0.5 + 0.5 * np.sin(2 * np.pi * motor * t))),
                     y * (1 - depth * (0.5 + 0.5 * np.sin(2 * np.pi * motor * t + 1.3)))], 1) * np.sqrt(0.5)


def musicbox(f, vel=0.8, seed=0):
    tau = 0.75 * (1319 / f) ** 0.4
    p = [(1.0, 1.0, tau), (1.0011, 0.35, tau * 0.9), (2.0, 0.05, tau * 0.4), (6.27, 0.18, tau * 0.12), (17.55, 0.05, tau * 0.04)]
    return struck(f, tau * 4.5, p, vel, bright=0.85, attack=0.0006, knock=0.05, knock_lp=9000, seed=seed)


def glock(f, vel=0.8, seed=0):
    tau = 1.1 * (1000 / f) ** 0.3
    p = [(1.0, 1.0, tau), (2.76, 0.3, tau * 0.35), (5.40, 0.12, tau * 0.15), (8.93, 0.05, tau * 0.07)]
    return struck(f, tau * 4, p, vel, bright=0.8, attack=0.001, seed=seed)


def crystal(f, vel=0.8, ratio=1.414, seed=0):
    """Glassy FM: an inharmonic shimmer that settles into a pure tone; twin voices beat slowly."""
    tau = 0.85 * (1500 / f) ** 0.25
    t = tt(tau * 4.5)
    n = len(t)
    idx = vel * (1.8 * np.exp(-t / 0.05) + 0.35 * np.exp(-t / 0.5))
    a = fm(f, n, ratio, idx) * env_hit(t, 0.0012, tau)
    b = fm(f * 1.0009 + 0.7, n, ratio, idx * 0.8, 0.3) * env_hit(t, 0.0012, tau * 0.85)
    h = sine(f * 2.0, n) * env_hit(t, 0.0012, tau * 0.25) * 0.18
    return fade_tail(_norm(np.stack([0.75 * a + 0.35 * b + h, 0.35 * a + 0.75 * b + h], 1)) * vel)


def rhodes(f, vel=0.8, dur=2.0, rel=0.25):
    """FM electric piano: a round body, a bell-like tine in the attack, more bark when hit harder."""
    t = tt(dur + rel)
    n = len(t)
    tau = 1.7 * (220 / f) ** 0.35
    bi = vel * (1.2 * np.exp(-t / 0.22) + 0.25) + 0.08
    body = fm(f, n, 1.0, bi) * env_hit(t, 0.0015, tau)
    twin = fm(f * 1.0015, n, 1.0, bi * 0.7, 0.2) * env_hit(t, 0.0015, tau * 0.9)
    tine = fm(f, n, 14.0, vel * 1.3 * np.exp(-t / 0.014)) * env_hit(t, 0.0008, 0.3) * 0.22
    y = body + 0.5 * twin + tine
    y *= np.where(t > dur, np.exp(-np.maximum(t - dur, 0) / (rel / 4)), 1.0)
    return fade_tail(_norm(y) * vel, 8)


def felt_piano(f, vel=0.6, dur=2.0, rel=0.3, seed=0):
    """A soft, felted piano: two slightly detuned strings, dull hammer, quickly fading highs."""
    t = tt(dur + rel)
    n = len(t)
    r = rng(seed)
    y = np.zeros(n)
    for k in range(1, int(min(28, 0.42 * SR / f)) + 1):
        fk = k * f * np.sqrt(1 + 0.00035 * k * k)
        a = k ** -1.5 * np.exp(-k / (2.2 + 5 * vel))
        d = max(0.05, 2.4 * (220 / f) ** 0.45 / (1 + 0.07 * k * k * (f / 440)))
        for det in (-0.7, 0.7):
            y += 0.5 * a * np.sin(2 * np.pi * fk * 2 ** (det / 1200) * t + r.uniform(0, 6.28)) * np.exp(-t / d)
    y += eq(noise(n, r) * np.exp(-t / 0.006), chain(hp(60), lp(900, 2))) * 0.05
    y *= np.sin(0.5 * np.pi * np.clip(t / 0.003, 0, 1)) ** 2
    y *= np.where(t > dur, np.exp(-np.maximum(t - dur, 0) / (rel / 4)), 1.0)
    return fade_tail(_norm(y) * vel, 8)


def pluck_string(f, vel=0.7, dur=2.5, pos=0.14, bright=0.4, tau=1.6, seed=0):
    """A plucked string (nylon guitar, harp): harmonics shaped by where it is plucked."""
    t = tt(dur)
    n = len(t)
    r = rng(seed)
    y = np.zeros(n)
    for k in range(1, int(min(40, 0.42 * SR / f)) + 1):
        fk = k * f * np.sqrt(1 + 0.00008 * k * k)
        a = abs(np.sin(np.pi * k * pos)) / k ** (1.25 + 0.9 * (1 - bright))
        d = tau * (196 / f) ** 0.3 / (1 + 0.012 * k * k)
        y += a * np.sin(2 * np.pi * fk * t + r.uniform(0, 0.3)) * np.exp(-t / d)
    y += eq(noise(n, r) * np.exp(-t / 0.004), chain(hp(1500), lp(6000))) * 0.04
    y *= np.sin(0.5 * np.pi * np.clip(t / 0.0008, 0, 1)) ** 2
    return fade_tail(_norm(y) * vel)


def brass(f, dur, vel=0.9, attack=0.04, rel=0.35, seed=0):
    """A small brass section: detuned saws that brighten as they get louder, a lip scoop, vibrato."""
    t = tt(dur + rel)
    n = len(t)
    r = rng(seed)
    e = env_adsr(t, attack, 0.25, 0.72, dur, rel / 3)
    vib_in = np.clip((t - 0.25) / 0.3, 0, 1)
    out = np.zeros(n)
    for c in (-7, 0, 6):
        scoop = 2 ** (-30 * np.exp(-t / 0.05) / 1200)
        vib = 2 ** (10 * vib_in * np.sin(2 * np.pi * 5.3 * t + r.uniform(0, 6.28)) / 1200)
        out += saw(f * 2 ** (c / 1200) * scoop * vib, n, r)
    out = svf(out / 3, np.minimum(f * (1.5 + 9 * vel * e) + 300, 16000), 0.75) * e
    breath = svf(noise(n, r), f * 3, 2.5, 'bp')
    out += breath / (np.std(breath) + 1e-9) * 0.02 * e
    return fade_tail(_norm(out) * vel)


def synth_pluck(f, vel=0.8, dur=0.6, decay=0.22, sweep=(1.3, 9000, 0.08), seed=0):
    t = tt(dur)
    n = len(t)
    st = supersaw(f, n, 7, 16, rng(seed))
    cutoff = f * sweep[0] + sweep[1] * np.exp(-t / sweep[2]) + 400
    st = svf(st, np.minimum(cutoff, 18000), 0.9) * env_hit(t, 0.001, decay)[:, None]
    return fade_tail(_norm(st) * vel)


def saw_pad(freqs, dur, attack=0.1, rel=0.8, cut0=500, cut1=4000, sweep=0.5, seed=0):
    t = tt(dur + rel)
    n = len(t)
    r = rng(seed)
    st = np.zeros((n, 2))
    for f in freqs:
        st += supersaw(f, n, 5, 14, r)
    st = svf(st, cut0 + (cut1 - cut0) * (1 - np.exp(-t / sweep)), 0.7)
    st *= env_adsr(t, attack, 0.6, 0.8, dur, rel / 3)[:, None]
    return fade_tail(_norm(chorus(st, 0.4, 2.5, 10, 0.35)))


def strings_pad(freqs, dur, attack=0.3, rel=0.8, cutoff=2200, seed=0):
    t = tt(dur + rel)
    n = len(t)
    r = rng(seed)
    st = np.zeros((n, 2))
    for j, f in enumerate(freqs):
        for c, p in ((-8, -0.6), (0, 0.0), (7, 0.6)):
            vib = 2 ** (6 * np.sin(2 * np.pi * (4.8 + 0.3 * j) * t + r.uniform(0, 6.28)) / 1200)
            st += pan(saw(f * 2 ** (c / 1200) * vib, n, r), p)
    st = svf(st, cutoff, 0.6)
    st = eq(st, chain(hp(120, 1), bell_db(3000, -3, 2)))
    st *= env_adsr(t, attack, 0.5, 0.85, dur, rel / 3)[:, None]
    return fade_tail(_norm(chorus(st, 0.35, 3, 11, 0.4)))


def glass_pad(freqs, dur, attack=0.25, rel=0.9):
    t = tt(dur + rel)
    n = len(t)
    st = np.zeros((n, 2))
    for j, f in enumerate(freqs):
        a = sine(f, n) + 0.3 * sine(2 * f, n) + 0.08 * sine(3 * f, n)
        b = sine(f + 0.9, n, 0.25) + 0.3 * sine(2 * f + 1.4, n)
        st += np.stack([a * 0.8 + b * 0.3, a * 0.3 + b * 0.8], 1)
    trem = 1 - 0.25 * (0.5 + 0.5 * np.sin(2 * np.pi * 3.1 * t))
    st *= (env_adsr(t, attack, 0.7, 0.7, dur, rel / 3) * trem)[:, None]
    return fade_tail(_norm(st))


def bass_note(f, dur, cutoff=600, rel=0.06):
    t = tt(dur + rel)
    n = len(t)
    y = saw(f, n) * 0.6 + sine(f, n)
    y = svf(y, cutoff * (0.6 + 0.8 * np.exp(-t / 0.06)), 0.8)
    y *= env_adsr(t, 0.004, 0.12, 0.7, dur, rel / 3)
    return fade_tail(_norm(y))


# ---------------------------------------------------------------- percussion and effects

def kick(vel=1.0, f_end=46, f_start=150, pd=0.03, d=0.22, click=0.25, seed=0):
    t = tt(0.9)
    n = len(t)
    body = sine(f_end + (f_start - f_end) * np.exp(-t / pd), n) * env_hit(t, 0.0006, d)
    c = eq(noise(n, rng(seed)) * np.exp(-t / 0.0015), chain(hp(2000), lp(9000))) * click
    return fade_tail(_norm(warm(body + c, 1.6)) * vel)


def sub_boom(f0=58, f1=34, dur=2.0, d=0.7):
    t = tt(dur)
    n = len(t)
    y = sine(f1 + (f0 - f1) * np.exp(-t / 0.35), n) * env_hit(t, 0.003, d)
    y = warm(y, 1.3) + 0.25 * warm(sine(2 * (f1 + (f0 - f1) * np.exp(-t / 0.35)), n) * env_hit(t, 0.003, d * 0.5), 1.2)
    return fade_tail(_norm(eq(y, lp(220, 2))))


def taiko(f0=78, vel=1.0, seed=0):
    t = tt(1.6)
    n = len(t)
    r = rng(seed)
    bend = 1 + 0.14 * np.exp(-t / 0.05)
    modes = [(1.0, 1.0, 0.45), (1.594, 0.45, 0.22), (2.136, 0.32, 0.16), (2.296, 0.22, 0.13), (2.653, 0.16, 0.1), (2.918, 0.1, 0.08)]
    y = sum(a * sine(f0 * m * bend, n, r.uniform()) * np.exp(-t / d) for m, a, d in modes)
    slap = eq(noise(n, r) * np.exp(-t / 0.02), chain(hp(150), lp(1800, 2)))
    y = warm(_norm(y) * 0.8 + _norm(slap) * 0.45, 1.5) * np.sin(0.5 * np.pi * np.clip(t / 0.0007, 0, 1)) ** 2
    return fade_tail(_norm(y) * vel)


def clap(vel=0.8, seed=0):
    t = tt(0.5)
    n = len(t)
    nz = noise(n, rng(seed))
    e = np.zeros(n)
    for k, d in enumerate((0.0, 0.009, 0.018, 0.027)):
        e += np.where(t >= d, np.exp(-np.maximum(t - d, 0) / 0.005), 0) * (0.8 if k < 3 else 1.0)
    e += np.where(t >= 0.027, 0.5 * np.exp(-np.maximum(t - 0.027, 0) / 0.09), 0)
    y = svf(nz * e, 1150, 1.1, 'bp') + 0.3 * svf(nz * e, 3200, 0.8, 'bp')
    return fade_tail(_norm(y) * vel)


def cymbal(d=1.0, seed=0):
    t = tt(d * 8)
    n = len(t)
    y = eq(noise(n, rng(seed)), chain(hp(5500, 2), bell_db(9000, 4, 1.5), lp(15000, 1))) * env_hit(t, 0.002, d)
    return stereo(fade_tail(_norm(y)))


def crack(seed=0):
    t = tt(0.4)
    n = len(t)
    r = rng(seed)
    st = np.stack([eq(noise(n, r), lp(3500, 2)), eq(noise(n, r), lp(3500, 2))], 1)
    return fade_tail(_norm(warm(_norm(st) * env_hit(t, 0.0005, 0.05)[:, None], 2.0)))


def air_burst(d=0.15, seed=0):
    t = tt(d * 5)
    n = len(t)
    r = rng(seed)
    st = np.stack([eq(noise(n, r), hp(4000, 2)), eq(noise(n, r), hp(4000, 2))], 1) * env_hit(t, 0.003, d)[:, None]
    return fade_tail(_norm(st))


def whoosh(dur, f0, f1, q=1.2, rise=2.0, fall=0.06, seed=0):
    t = tt(dur + fall * 5)
    n = len(t)
    r = rng(seed)
    u = np.clip(t / dur, 0, 1)
    fc = f0 * (f1 / f0) ** u
    e = np.where(t <= dur, u ** rise, np.exp(-np.maximum(t - dur, 0) / fall))
    st = np.stack([svf(noise(n, r, 'pink'), fc, q, 'bp'), svf(noise(n, r, 'pink'), fc, q, 'bp')], 1) * e[:, None]
    return fade_tail(_norm(st))


def riser_tone(f0, f1, dur, seed=0):
    t = tt(dur + 0.05)
    n = len(t)
    u = np.clip(t / dur, 0, 1)
    st = supersaw(f0 * (f1 / f0) ** (u ** 1.3), n, 5, 25, rng(seed))
    st = svf(st, 300 + 3800 * u ** 1.5, 0.9)
    rate = 4 + 14 * u ** 1.5
    trem = 0.5 + 0.5 * np.sin(2 * np.pi * np.cumsum(rate) / SR)
    e = u ** 1.6 * (1 - 0.55 * trem) * np.where(t > dur, np.exp(-np.maximum(t - dur, 0) / 0.01), 1.0)
    return fade_tail(_norm(st * e[:, None]))


def tick(f=3500, d=0.004, seed=0):
    t = tt(0.06)
    n = len(t)
    y = sine(f, n) * env_hit(t, 0.0003, d) + eq(noise(n, rng(seed)) * np.exp(-t / 0.0008), hp(3000)) * 0.3
    return fade_tail(_norm(y))


def wood_tick(f=1800, seed=0):
    return struck(f, 0.12, [(1.0, 1.0, 0.016), (2.71, 0.4, 0.007)], 1.0, bright=0.8, attack=0.0006, knock=0.15, knock_lp=5000, seed=seed)


def thock(f=210, d=0.018):
    t = tt(0.15)
    n = len(t)
    y = sine(f * (1 + 0.4 * np.exp(-t / 0.006)), n) * env_hit(t, 0.0005, d)
    return fade_tail(_norm(y))


def pop(f0=1100, f1=220, glide=0.012, d=0.03, seed=0):
    t = tt(0.25)
    n = len(t)
    y = sine(f1 + (f0 - f1) * np.exp(-t / glide), n) * env_hit(t, 0.0005, d)
    y += eq(noise(n, rng(seed)) * np.exp(-t / 0.002), chain(hp(1000), lp(6000))) * 0.2
    return fade_tail(_norm(y))


def bubble(f0=420, f1=1550, dur=0.035, d=0.03):
    t = tt(0.2)
    n = len(t)
    y = sine(f0 * (f1 / f0) ** np.clip(t / dur, 0, 1), n) * env_hit(t, 0.001, d)
    return fade_tail(_norm(y))


def bubble_note(f, dur=0.3, bend=0.0, bend_at=0.12):
    """A round, slightly vocal tone with a little scoop; an upward bend at the end asks a question."""
    t = tt(dur + 0.4)
    n = len(t)
    up = bend * np.clip((t - bend_at) / 0.16, 0, 1) ** 1.5
    fr = f * 2 ** ((-1.6 * np.exp(-t / 0.012) + up) / 12)
    y = fm(fr, n, 2.0, 0.9 * np.exp(-t / 0.08) + 0.2) * env_adsr(t, 0.004, 0.12, 0.55, dur, 0.06)
    return fade_tail(_norm(y))


def blip(f0, f1, dur, glide=0.02, width=0.3, cutoff=5000):
    t = tt(dur)
    n = len(t)
    y = pulse(f1 + (f0 - f1) * np.exp(-t / glide), n, width)
    e = env_hit(t, 0.001, dur * 0.4)
    return fade_tail(_norm(svf(y, cutoff * (0.5 + 0.5 * e), 1.3) * e), 6)


def zip_up(f0, f1, dur):
    t = tt(dur * 2)
    n = len(t)
    u = np.clip(t / dur, 0, 1)
    y = fm(f0 * (f1 / f0) ** (u ** 0.7), n, 1.5, 1.2 * (1 - u) + 0.2) * env_hit(t, 0.003, dur * 0.5)
    return fade_tail(_norm(y))


def glint(dur, seed=0):
    t = tt(dur)
    n = len(t)
    r = rng(seed)
    trem = 0.5 + 0.5 * np.sin(2 * np.pi * 22 * t)
    st = np.stack([eq(noise(n, r), hp(7000, 2)), eq(noise(n, r), hp(7000, 2))], 1)
    return fade_tail(_norm(st * (trem * env_hit(t, 0.01, dur * 0.35))[:, None]))


def grains(times, notes, length=(0.03, 0.12), amp=(0.4, 1.0), spread=0.9, seed=0):
    """Glitter: tiny bell grains scattered in time and space."""
    r = rng(seed)
    n = n_of(max(times) + 0.6)
    st = np.zeros((n, 2))
    for at in times:
        f = r.choice(notes) * (1 + r.uniform(-0.002, 0.002))
        L = r.uniform(*length)
        t = tt(L * 4)
        g = fm(f, len(t), 2.0, 0.6 * np.exp(-t / 0.01)) * env_hit(t, 0.0015, L / 3) * r.uniform(*amp)
        i = n_of(at)
        st[i:i + len(t)] += pan(g, r.uniform(-spread, spread))[:n - i]
    return st


def confetti(count=70, span=0.18, seed=0):
    """Paper confetti: many short crackles, dense at first, then thinning out."""
    r = rng(seed)
    times = np.minimum(r.exponential(span, count), span * 5)
    n = n_of(span * 5 + 0.05)
    st = np.zeros((n, 2))
    for at in times:
        L = r.uniform(0.002, 0.007)
        t = tt(L * 3)
        g = svf(r.standard_normal(len(t)), r.uniform(2500, 8500), 1.6, 'bp') * env_hit(t, 0.0004, L)
        g *= r.uniform(0.3, 1.0) * np.exp(-at / (span * 2))
        i = n_of(at)
        st[i:i + len(t)] += pan(g, r.uniform(-0.95, 0.95))[:n - i]
    return _norm(st)


def chug(d=0.045, seed=0):
    t = tt(0.3)
    n = len(t)
    r = rng(seed)
    body = svf(noise(n, r, 'pink'), 1000, 0.7, 'bp')
    hiss = eq(noise(n, r), hp(4000, 2))
    y = (body / np.std(body) + 0.35 * hiss) * env_hit(t, 0.003, d)
    return fade_tail(_norm(y))


def whistle(freqs, dur, rel=0.25, seed=0):
    """A steam whistle: breathy tones that scoop up into the chord, with a light vibrato."""
    t = tt(dur + rel)
    n = len(t)
    r = rng(seed)
    e = env_adsr(t, 0.05, 0.3, 0.85, dur, rel / 3)
    y = np.zeros(n)
    for f in freqs:
        scoop = 2 ** (-90 * np.exp(-t / 0.06) / 1200)
        vib = 2 ** (7 * np.sin(2 * np.pi * 5.8 * t + r.uniform(0, 6.28)) / 1200)
        fr = f * scoop * vib
        tone = sine(fr, n, r.uniform()) + 0.12 * sine(2 * fr, n) + 0.04 * sine(3 * fr, n)
        air = svf(noise(n, r), f, 18, 'bp')
        y += tone + 0.3 * air / (np.std(air) + 1e-9)
    hiss = eq(noise(n, r), chain(hp(3000), lp(9000))) * 0.05
    return fade_tail(_norm((y / len(freqs) + hiss) * e))


def heartbeat(f=62, d=0.09):
    t = tt(0.5)
    n = len(t)
    fr = f * (1 + 0.25 * np.exp(-t / 0.02))
    y = sine(fr, n) + 0.45 * sine(2 * fr, n) + 0.12 * sine(3 * fr, n)
    y = warm(_norm(y * env_hit(t, 0.006, d)), 1.4)
    return fade_tail(_norm(eq(y, lp(400, 2))))


def fill_glide(f0, f1, dur, seed=0):
    t = tt(dur + 0.08)
    n = len(t)
    u = np.clip(t / dur, 0, 1)
    fr = f0 * (f1 / f0) ** u
    y = sine(fr, n) + 0.25 * sine(2 * fr, n)
    nz = svf(noise(n, rng(seed)), fr * 2, 2.0, 'bp')
    e = (0.3 + 0.7 * u) * np.where(t > dur, np.exp(-np.maximum(t - dur, 0) / 0.012), 1.0) * np.clip(t / 0.01, 0, 1)
    return fade_tail(_norm((y + 0.4 * nz / (np.std(nz) + 1e-9)) * e))


def reverse_swell(tone, length):
    """A tone played backwards: it swells up and stops right where the next sound begins."""
    st = stereo(tone)[::-1]
    k = n_of(length)
    st = st[-k:].copy()
    st *= np.clip(np.arange(k) / n_of(0.05), 0, 1)[:, None]
    return _norm(st)


def vinyl(dur, seed=0):
    r = rng(seed)
    n = n_of(dur)
    hiss = eq(noise(n, r), chain(hp(1500), lp(7000))) * 0.15
    clicks = np.zeros(n)
    for i in r.integers(0, n, int(dur * 9)):
        clicks[i] += r.uniform(0.3, 1.0) * r.choice([-1, 1])
    clicks = eq(clicks, chain(hp(800), lp(5000)))
    return stereo(_norm(hiss + _norm(clicks) * 0.8))


def lofi(st):
    st = eq(st, chain(hp(110, 1), lp(3800, 2), bell_db(900, 1.5, 2)))
    return chorus(st, 0.6, 0.9, 6, 1.0)


# ================================================================ the sounds

def follow():
    m = Mix(1.4)
    m.add(bubble(420, 1550), 0.0, 0.0, 0.22, send=0.12)
    m.add(marimba(N('G5'), 0.72, seed=1), 0.03, -0.18, 0.8, send=0.2)
    m.add(marimba(N('D6'), 0.95, seed=2), 0.145, 0.16, 1.0, send=0.2)
    m.add(marimba(N('G4'), 0.4, seed=3), 0.145, -0.05, 0.38, send=0.15)
    m.add(marimba(N('D5'), 0.35, seed=4), 0.152, 0.05, 0.3, send=0.15)
    return m.render('room', target=-16.5)


def subscribe():
    m = Mix(2.6)
    m.add(sub_boom(66, 40, 1.4, 0.35), 0.0, 0, 0.5)
    m.add(kick(0.9, 48, 140, 0.03, 0.18, 0.2, seed=1), 0.0, 0, 0.45)
    m.add(clap(0.8, seed=2), 0.0, 0, 0.3, send=0.35)
    m.add(air_burst(0.15, seed=3), 0.0, 0, 0.1, send=0.3)
    m.add(bass_note(N('D2'), 1.1, 420), 0.0, 0, 0.3)
    m.add(saw_pad([N('D3'), N('A3'), N('F#4'), N('A4'), N('E5')], 1.4, 0.08, 0.9, 500, 4200, 0.5, seed=4), 0.0, 0, 0.28, send=0.35)
    for i, name in enumerate(['D5', 'F#5', 'A5', 'D6', 'F#6', 'A6']):
        m.add(synth_pluck(N(name), 0.7 + 0.05 * i, 0.55, 0.2, seed=10 + i), 0.02 + 0.085 * i, 0, 0.42, send=0.3)
    at = 0.02 + 0.085 * 6
    for j, name in enumerate(['D6', 'F#6', 'A6', 'D7']):
        m.add(synth_pluck(N(name), 0.8, 1.3, 0.5, (1.5, 7000, 0.15), seed=20 + j), at, 0, 0.3, send=0.4)
    times = list(at - 0.04 + np.sort(rng(5).random(22)) ** 1.6 * 0.9)
    notes = [N(x) for x in ('D7', 'E7', 'F#7', 'A7', 'B7', 'D8')]
    m.add(grains(times, notes, seed=6), 0.0, 0, 0.16, send=0.5)
    return m.render('hall', target=-16.0)


def resub():
    m = Mix(3.0)
    ep = Mix(3.0)
    for i, name in enumerate(['Eb3', 'Bb3', 'D4', 'F4', 'G4']):
        ep.add(rhodes(N(name), 0.62, 0.5 - 0.016 * i, 0.22), 0.016 * i, -0.3 + 0.15 * i, 0.55)
    ep.add(rhodes(N('G5'), 0.8, 0.26, 0.15), 0.06, 0.15, 0.62)
    ep.add(rhodes(N('A5'), 0.62, 0.14, 0.12), 0.32, 0.2, 0.45)
    for i, name in enumerate(['Bb2', 'F3', 'D4', 'F4', 'C5']):
        ep.add(rhodes(N(name), 0.7, 1.9 - 0.014 * i, 0.5), 0.46 + 0.014 * i, -0.3 + 0.15 * i, 0.58)
    ep.add(rhodes(N('Bb5'), 0.9, 1.8, 0.5), 0.5, 0.1, 0.7)
    m.add(warm(_norm(autopan(ep.dry, 4.2, 0.28)) * 0.9, 1.3), 0.0, 0, 0.85, send=0.3)
    m.add(strings_pad([N('Bb3'), N('F4'), N('D5')], 1.5, 0.4, 0.8, 2000, seed=7), 0.46, 0, 0.13, send=0.35)
    t = tt(2.0)
    m.add(fade_tail(sine(N('Bb1'), len(t)) * env_hit(t, 0.02, 0.9)), 0.46, 0, 0.22)
    return m.render('plate', target=-16.5)


def giftsubs():
    m = Mix(2.4)
    m.add(move(whoosh(0.16, 1500, 7500, 1.4, 1.5, 0.03, seed=1), -0.6, 0.6), 0.0, 0, 0.24, send=0.2)
    m.add(pop(1150, 230, 0.012, 0.035, seed=2), 0.16, 0, 0.5, send=0.2)
    m.add(thock(160, 0.05), 0.16, 0, 0.32)
    m.add(confetti(70, 0.18, seed=3), 0.17, 0, 0.14, send=0.25)
    tune = [('E6', 0.20, -0.3), ('G#6', 0.265, 0.3), ('B6', 0.33, -0.25), ('E7', 0.395, 0.25),
            ('B6', 0.50, -0.2), ('G#6', 0.565, 0.2), ('B6', 0.68, -0.15), ('E7', 0.68, 0.15)]
    for i, (name, at, p) in enumerate(tune):
        m.add(musicbox(N(name), 0.8 if i < 6 else 0.9, seed=10 + i), at, p, 0.55, send=0.35)
    m.add(strings_pad([N('E4'), N('B4'), N('G#5')], 1.1, 0.25, 0.7, 2500, seed=4), 0.2, 0, 0.1, send=0.3)
    return m.render('plate', target=-16.0)


def bits():
    m = Mix(1.8)
    m.add(move(zip_up(900, 3800, 0.07), -0.5, 0.5), 0.0, 0, 0.26, send=0.2)
    m.add(whoosh(0.07, 2000, 10000, 1.0, 1.2, 0.03, seed=1), 0.0, 0, 0.1)
    m.add(crystal(N('G#6'), 0.85, seed=2), 0.065, 0.25, 0.5, send=0.35)
    m.add(crystal(N('C#7'), 0.9, seed=3), 0.075, -0.25, 0.5, send=0.35)
    m.add(crystal(N('F7'), 0.7, seed=4), 0.16, 0.0, 0.3, send=0.4)
    m.add(struck(N('C#5'), 1.2, [(1.0, 1.0, 0.3), (2.0, 0.2, 0.15)], 0.8), 0.065, -0.1, 0.15, send=0.3)
    m.add(struck(N('G#5'), 1.2, [(1.0, 1.0, 0.3), (2.0, 0.2, 0.15)], 0.8), 0.065, 0.1, 0.13, send=0.3)
    m.add(glint(0.6, seed=5), 0.08, 0, 0.06, send=0.4)
    times = list(0.12 + np.sort(rng(6).random(16)) * 0.55)
    notes = [N(x) for x in ('C#7', 'D#7', 'F7', 'G#7', 'A#7', 'C#8')]
    m.add(grains(times, notes, (0.02, 0.08), seed=7), 0.0, 0, 0.12, send=0.4)
    return m.render('plate', target=-16.5)


def raid():
    m = Mix(3.6)
    hit = 0.95
    m.add(whoosh(hit, 180, 7000, 1.1, 2.2, 0.04, seed=1), 0.0, 0, 0.36, send=0.3)
    m.add(riser_tone(N('D3'), N('D4'), hit, seed=2), 0.0, 0, 0.2, send=0.3)
    for k, (at, v, p) in enumerate(((0.56, 0.45, -0.15), (0.71, 0.55, 0.15), (0.83, 0.7, 0.0))):
        m.add(taiko(84, v, seed=3 + k), at, p, 0.55, send=0.3)
    m.add(sub_boom(58, 33, 2.4, 0.8), hit, 0, 0.85)
    m.add(taiko(68, 1.0, seed=7), hit, 0, 0.75, send=0.35)
    m.add(crack(seed=8), hit, 0, 0.32, send=0.5)
    m.add(cymbal(0.8, seed=9), hit, 0, 0.07, send=0.25)
    for name, g, p in (('D2', 0.42, 0), ('A2', 0.36, -0.2), ('D3', 0.36, 0.2), ('A3', 0.3, -0.3), ('D4', 0.28, 0.3), ('F#4', 0.24, 0)):
        m.add(brass(N(name), 1.15, 0.95, 0.03, 0.6, seed=int(N(name))), hit, p, g, send=0.45)
    m.add(taiko(80, 0.5, seed=10), hit + 0.42, -0.2, 0.42, send=0.35)
    m.add(taiko(76, 0.62, seed=11), hit + 0.56, 0.2, 0.5, send=0.35)
    return m.render('epic', target=-15.5)


def channelpoints():
    m = Mix(0.9)
    m.add(tick(3800, 0.003, seed=1), 0.0, 0, 0.35, send=0.1)
    m.add(thock(210, 0.018), 0.0, 0, 0.45)
    b = Mix(0.4)
    b.add(blip(N('F#5'), N('F#6'), 0.13, 0.022, 0.32, 5200), 0.012, -0.1, 0.32)
    b.add(blip(N('C#7'), N('C#7'), 0.09, 0.02, 0.25, 7000), 0.095, 0.15, 0.22)
    m.add(pingpong(b.dry, 0.085, 0.42, 3, 5000, 0.3), 0.0, 0, 1.0, send=0.12)
    return m.render('small', target=-18.0)


def hypetrain():
    m = Mix(3.2)
    beats, iv = [0.0], 0.205
    while beats[-1] + iv < 1.2:
        beats.append(beats[-1] + iv)
        iv *= 0.875
    for i, b in enumerate(beats):
        accent = i % 2 == 0
        m.add(chug(0.045 if i < 4 else 0.032, seed=10 + i), b, -0.22 if i % 2 else 0.22, 0.5 if accent else 0.32, send=0.15)
        if accent:
            m.add(kick(0.7, 50, 120, 0.025, 0.12, 0.1, seed=30 + i), b, 0, 0.3)
    scale = ['A2', 'B2', 'C#3', 'D3', 'E3', 'F#3', 'G#3']
    for k, b in enumerate(beats[::2]):
        m.add(bass_note(N(scale[min(k, 6)]), 0.15, 500 + 160 * k), b, 0, 0.3)
    m.add(whoosh(0.6, 400, 5000, 1.0, 2.0, 0.05, seed=2), 0.62, 0, 0.14, send=0.2)
    chord = [N('E5'), N('F#5'), N('A5'), N('C#6')]
    m.add(whistle(chord, 0.16, 0.05, seed=3), 1.25, 0, 0.42, send=0.35)
    m.add(kick(0.8, 48, 130, 0.03, 0.2, 0.15, seed=4), 1.25, 0, 0.35)
    m.add(whistle(chord, 0.85, 0.35, seed=5), 1.47, 0, 0.48, send=0.4)
    m.add(cymbal(0.5, seed=6), 1.47, 0, 0.05, send=0.2)
    return m.render('hall', target=-16.0)


def poll():
    m = Mix(1.4)
    for i, (f, p) in enumerate(((1760, -0.55), (1980, 0.0), (2220, 0.55))):
        m.add(wood_tick(f, seed=1 + i), 0.07 * i, p, 0.3, send=0.15)
    m.add(bubble_note(N('C6'), 0.1), 0.25, -0.1, 0.4, send=0.25)
    m.add(bubble_note(N('G6'), 0.34, bend=0.7, bend_at=0.1), 0.37, 0.1, 0.45, send=0.25)
    return m.render('room', target=-17.0)


def prediction():
    m = Mix(2.6)
    m.add(reverse_swell(crystal(N('G#5'), 0.8, seed=1), 0.42), 0.0, 0, 0.35, send=0.45)
    for i, name in enumerate(['C5', 'D5', 'E5', 'F#5', 'G#5', 'A#5', 'C6']):
        m.add(pluck_string(N(name), 0.6, 2.0, 0.5, 0.55, 1.3, seed=10 + i), 0.42 + 0.045 * i, -0.6 + 0.2 * i, 0.32, send=0.45)
    m.add(glass_pad([N('C4'), N('E4'), N('G#4'), N('C5')], 1.3, 0.25, 0.9), 0.42, 0, 0.16, send=0.5)
    m.add(crystal(N('G#6'), 0.5, seed=2), 0.78, -0.35, 0.22, send=0.5)
    m.add(crystal(N('D7'), 0.5, seed=3), 0.86, 0.35, 0.2, send=0.5)
    return m.render('hall', target=-17.0)


def goal():
    m = Mix(2.2)
    m.add(fill_glide(300, 1400, 0.48, seed=1), 0.0, 0, 0.18, send=0.2)
    for k in range(8):
        m.add(tick(1500 + 2000 * k / 7, 0.006, seed=10 + k), 0.48 * (k / 8) ** 0.7, -0.4 + 0.8 * k / 7, 0.16, send=0.1)
    hit = 0.5
    m.add(kick(0.6, 52, 130, 0.03, 0.15, 0.1, seed=2), hit, 0, 0.3)
    for i, (name, v, p) in enumerate((('F5', 0.7, -0.3), ('A5', 0.75, -0.1), ('C6', 0.8, 0.1), ('E6', 0.85, 0.3), ('G6', 0.7, 0.0))):
        m.add(vibraphone(N(name), v, seed=20 + i), hit + 0.008 * i, p, 0.42, send=0.3)
    m.add(vibraphone(N('F4'), 0.5, seed=30), hit, 0, 0.26, send=0.2)
    return m.render('plate', target=-16.5)


def charity():
    m = Mix(3.0)
    m.add(heartbeat(62, 0.09), 0.0, 0, 0.5)
    m.add(heartbeat(54, 0.11), 0.17, 0, 0.38)
    for i, name in enumerate(['F3', 'C4', 'F4', 'A4', 'C5', 'E5']):
        m.add(pluck_string(N(name), 0.65, 2.6, 0.13, 0.35, 1.8, seed=10 + i), 0.34 + 0.075 * i, -0.35 + 0.14 * i, 0.36, send=0.3)
    m.add(pluck_string(N('A5'), 0.8, 2.2, 0.13, 0.45, 1.6, seed=20), 0.34 + 0.075 * 6 + 0.06, 0.2, 0.34, send=0.32)
    m.add(strings_pad([N('F3'), N('C4'), N('A4'), N('E5')], 1.8, 0.45, 0.9, 1800, seed=21), 0.34, 0, 0.13, send=0.35)
    return m.render('room', target=-16.5)


def shoutout():
    m = Mix(1.8)
    m.add(move(whoosh(0.13, 700, 5500, 1.2, 1.6, 0.03, seed=1), -0.7, 0.0), 0.0, 0, 0.2, send=0.2)
    for name, p in (('Bb4', -0.25), ('D5', 0.0), ('F5', 0.25)):
        m.add(brass(N(name), 0.11, 0.95, 0.018, 0.08, seed=int(N(name))), 0.11, p, 0.3, send=0.25)
    m.add(brass(N('Bb3'), 0.11, 0.9, 0.02, 0.08, seed=2), 0.11, 0, 0.3)
    for name, p in (('Eb5', -0.25), ('G5', 0.0), ('Bb5', 0.25)):
        m.add(brass(N(name), 0.55, 1.0, 0.02, 0.35, seed=int(N(name)) + 1), 0.27, p, 0.3, send=0.3)
    m.add(brass(N('Eb3'), 0.55, 0.9, 0.025, 0.35, seed=3), 0.27, 0, 0.32)
    m.add(clap(0.8, seed=4), 0.27, 0, 0.2, send=0.3)
    m.add(kick(0.8, 50, 140, 0.03, 0.16, 0.15, seed=5), 0.27, 0, 0.28)
    return m.render('room', target=-16.0)


def soft_note(f, d=0.08):
    t = tt(d * 7)
    n = len(t)
    y = fm(f, n, 2.0, 0.5 * np.exp(-t / 0.015)) * env_hit(t, 0.002, d)
    y += 0.12 * sine(2 * f, n) * env_hit(t, 0.002, d * 0.4)
    return fade_tail(_norm(y))


def chat():
    # Plays often, so it stays small, soft and falling: noticed, never in the way
    m = Mix(0.6)
    m.add(bubble(700, 1300, 0.02, 0.016), 0.0, 0, 0.16, send=0.08)
    m.add(soft_note(N('A6'), 0.07), 0.004, -0.1, 0.5, send=0.12)
    m.add(soft_note(N('F#6'), 0.1), 0.068, 0.1, 0.45, send=0.12)
    return m.render('small', target=-22.0)


def adbreak():
    m = Mix(2.4)
    for i, name in enumerate(['A2', 'E3', 'B3', 'C4', 'G4']):
        m.add(felt_piano(N(name), 0.55, 1.2, 0.3, seed=1 + i), 0.02 * i, -0.3 + 0.15 * i, 0.32, send=0.25)
    for i, name in enumerate(['F2', 'C3', 'E3', 'A3', 'E4']):
        m.add(felt_piano(N(name), 0.5, 1.3, 0.4, seed=10 + i), 0.42 + 0.02 * i, -0.3 + 0.15 * i, 0.3, send=0.25)
    m.add(vinyl(1.6, seed=20), 0.0, 0, 0.05)
    return m.render('room', post=lambda st: tape_stop(lofi(st), 0.8, 0.75), target=-17.0)


SOUNDS = {
    'follow': follow, 'subscribe': subscribe, 'resub': resub, 'giftsubs': giftsubs, 'bits': bits,
    'raid': raid, 'channelpoints': channelpoints, 'hypetrain': hypetrain, 'poll': poll,
    'prediction': prediction, 'goal': goal, 'charity': charity, 'shoutout': shoutout, 'adbreak': adbreak,
    'chat': chat,
}


def write_wav24(path, y):
    pcm = np.round(np.clip(y, -1, 1 - 2 ** -23) * 2 ** 23).astype('<i4')
    raw = np.frombuffer(pcm.tobytes(), np.uint8).reshape(-1, 4)[:, :3].tobytes()
    with wave.open(path, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(3)
        w.setframerate(SR)
        w.writeframes(raw)


if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else '.'
    names = sys.argv[2:] or list(SOUNDS)
    os.makedirs(out, exist_ok=True)
    for name in names:
        y, gr = SOUNDS[name]()
        write_wav24(os.path.join(out, name + '.wav'), y)
        print(f'{name:14s} {len(y) / SR:5.2f} s  {lufs(y):6.1f} LUFS  peak {20 * np.log10(np.max(np.abs(y))):5.1f} dBFS  '
              f'limiter {gr:4.1f} dB  dc {np.mean(y):+.4f}', flush=True)
