#!/usr/bin/env python3
"""
Cozy background music for the stream scenes, synthesized from scratch with the
same engine as the alert sounds (make_alert_sounds.py next to this file): no
samples, nothing borrowed, so no copyright claims on stream.

  starting   "Starting soon"       D major, 78 bpm: warm, a little expectant, a glockenspiel sparkle
  brb        "Be right back"       C major, 72 bpm: laid-back electric piano, half-time drums
  ending     "Thanks for watching" A-flat major, 66 bpm: felt piano, strings, a gentle goodbye

Every track is 16 bars and loops seamlessly: each note is placed in three
copies of the loop, the room and the tape effects run over all three, and the
middle copy is kept, so the ringing tails of the end land in the beginning.
All filters on the result are circular, and every wobble repeats exactly once
per loop.

    python3 make_scene_music.py OUT_DIR [starting brb ending]
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_alert_sounds as E  # noqa: E402

SR = E.SR
N = E.N


class Loop:
    def __init__(self, bpm, bars=16, swing=0.58, seed=0):
        self.bpm = bpm
        self.beat = 60.0 / bpm
        self.L = bars * 4 * self.beat
        self.n = E.n_of(self.L)
        self.swing = swing
        self.r = E.rng(seed)
        self.dry = np.zeros((3 * self.n, 2))
        self.send = np.zeros((3 * self.n, 2))

    def t(self, bar, beat):
        """Time of a beat in a bar; off-beat eighths are swung."""
        whole, frac = divmod(beat, 1.0)
        if abs(frac - 0.5) < 1e-6:
            frac = self.swing
        return ((bar - 1) * 4 + whole + frac) * self.beat

    def add(self, sig, at, p=0.0, gain=1.0, send=0.0, human=0.006):
        sig = np.asarray(sig, float)
        st = (E.pan(sig, p) if sig.ndim == 1 or p else sig) * gain
        at = (at + self.r.uniform(-human, human)) % self.L
        i0 = E.n_of(at) if at > 0 else 0
        for k in range(3):
            i = i0 + k * self.n
            e = min(3 * self.n, i + len(st))
            if e > i:
                self.dry[i:e] += st[:e - i]
                if send:
                    self.send[i:e] += st[:e - i] * send

    def periodic(self, rate):
        """The nearest rate that repeats a whole number of times per loop."""
        return max(1, round(rate * self.L)) / self.L

    def render(self, room, ret=1.0, bus=None):
        out = self.dry.copy()
        s = E.eq(self.send, E.chain(E.hp(200, 2), E.lp(9000, 1)))
        s = np.stack([0.72 * s[:, 0] + 0.28 * s[:, 1], 0.28 * s[:, 0] + 0.72 * s[:, 1]], 1)
        out += E.convolve(s, E.ir(room))[:3 * self.n] * ret
        if bus:
            out = bus(out, self)
        return out[self.n:2 * self.n]


def circular(seg, curve):
    n = len(seg)
    X = np.fft.rfft(seg, axis=0)
    H = curve(np.maximum(np.fft.rfftfreq(n, 1 / SR), 1e-3))
    return np.fft.irfft(X * H[:, None], n, axis=0)


def loop_noise(n, curve, seed):
    """Noise that is itself a perfect loop."""
    r = E.rng(seed)
    st = np.stack([r.standard_normal(n), r.standard_normal(n)], 1)
    return E._norm(circular(st, curve))


def loop_master(seg, target=-20.0, ceiling=-1.5):
    n = len(seg)
    seg = circular(seg, E.hp(32, 2))
    gr = 0.0
    for _ in range(3):
        seg = seg * 10 ** ((target - E.lufs(np.tile(seg, (2, 1)))) / 20)
        lim, gr = E.limiter(np.tile(seg, (3, 1)), ceiling)
        seg = lim[n:2 * n]
    return seg, gr


def lofi_bus(warmth=1.2, cutoff=6500, wow_ms=0.5):
    def bus(out, lp_):
        out = E.eq(out, E.chain(E.hp(35, 2), E.lp(cutoff, 1), E.bell_db(220, 1.5, 1.5)))
        out = E.warm(out / (np.max(np.abs(out)) + 1e-9) * 0.9, warmth)
        n = len(out)
        idx = np.arange(n)
        t = idx / SR
        rate = lp_.periodic(0.55)
        res = np.empty_like(out)
        for c, ph in ((0, 0.0), (1, 0.6)):
            d = (6.0 + wow_ms * np.sin(2 * np.pi * rate * t + ph)) * SR / 1000
            res[:, c] = np.interp(idx - d, idx, out[:, c], left=0.0, right=0.0)
        return res
    return bus


# ---------------------------------------------------------------- voices

_cache = {}


def cached(key, make):
    if key not in _cache:
        _cache[key] = make()
    return _cache[key]


def piano(f, vel, dur, seed=0):
    vel = round(vel * 20) / 20
    return cached(('p', round(f, 2), vel, round(dur, 2), seed % 3), lambda: E.felt_piano(f, vel, dur, 0.5, seed % 3))


def ep(f, vel, dur):
    vel = round(vel * 20) / 20
    return cached(('e', round(f, 2), vel, round(dur, 2)), lambda: E.rhodes(f, vel, dur, 0.3))


def soft_bass(f, dur, vel=0.8):
    def make():
        t = E.tt(dur + 0.25)
        n = len(t)
        y = E.sine(f, n) + 0.28 * E.sine(2 * f, n) + 0.07 * E.sine(3 * f, n)
        y *= E.env_adsr(t, 0.008, 0.35, 0.55, dur, 0.06)
        y += E.eq(E.noise(n, E.rng(3)) * np.exp(-t / 0.004), E.chain(E.hp(200), E.lp(1200))) * 0.04
        return E.fade_tail(E._norm(E.eq(y, E.lp(900, 2))) * vel)
    return cached(('b', round(f, 2), round(dur, 2), round(vel, 2)), make)


def lofi_kick(vel=0.8):
    return cached(('k', vel), lambda: E.eq(E.kick(vel, 48, 115, 0.03, 0.16, 0.04), E.lp(2600, 2)))


def rim(vel=0.6, seed=0):
    def make():
        t = E.tt(0.35)
        n = len(t)
        r = E.rng(seed)
        nz = E.svf(E.noise(n, r), 1900, 0.8, 'bp') * E.env_hit(t, 0.001, 0.05)
        body = E.sine(185 * (1 + 0.3 * np.exp(-t / 0.008)), n) * E.env_hit(t, 0.0008, 0.04)
        return E.fade_tail(E._norm(E.eq(0.8 * E._norm(nz) + 0.6 * body, E.lp(6500, 2))) * vel)
    return cached(('s', vel, seed), make)


def brush(vel=0.5, seed=0):
    def make():
        t = E.tt(0.5)
        n = len(t)
        nz = E.eq(E.noise(n, E.rng(seed)), E.chain(E.hp(1500), E.lp(7000)))
        return E.fade_tail(E._norm(nz * E.env_hit(t, 0.025, 0.09)) * vel)
    return cached(('br', vel, seed), make)


def hat(vel=0.3, seed=0):
    def make():
        t = E.tt(0.12)
        n = len(t)
        nz = E.eq(E.noise(n, E.rng(seed)), E.chain(E.hp(7000, 2), E.lp(12000, 1)))
        return E.fade_tail(E._norm(nz * E.env_hit(t, 0.0008, 0.016)) * vel)
    return cached(('h', vel, seed), make)


def chord_hits(lp_, bar, beat, notes, vel, dur_beats, inst, spread=0.012, p0=-0.3, send=0.25, gain=0.3):
    at = lp_.t(bar, beat)
    for i, name in enumerate(notes):
        f = N(name)
        sig = inst(f, vel * (0.9 + 0.1 * (i == len(notes) - 1)), dur_beats * lp_.beat)
        lp_.add(sig, at + spread * i, p0 + 0.6 * i / max(1, len(notes) - 1), gain, send)


def melody(lp_, line, inst, gain, p=0.1, send=0.3, vel=0.7):
    for bar, notes in line.items():
        for beat, name, dur in notes:
            lp_.add(inst(N(name), vel + lp_.r.uniform(-0.05, 0.05), dur * lp_.beat), lp_.t(bar, beat), p, gain, send)


def drums(lp_, bars, kicks, snares, hats_on=True, snare=rim, hat_gain=0.09, kick_gain=0.42, snare_gain=0.26):
    for bar in range(1, bars + 1):
        for b in kicks(bar):
            lp_.add(lofi_kick(0.8), lp_.t(bar, b), 0, kick_gain, 0.05)
        for b in snares(bar):
            lp_.add(snare(0.6, seed=bar % 4), lp_.t(bar, b), 0.05, snare_gain, 0.25)
        if hats_on:
            for k in range(8):
                v = 0.32 if k % 2 == 0 else 0.22
                lp_.add(hat(v, seed=(bar * 8 + k) % 6), lp_.t(bar, k * 0.5), 0.25, hat_gain, 0.08)


def vinyl_bed(lp_, level=0.035, seed=7):
    hiss = loop_noise(lp_.n, E.chain(E.hp(1500), E.lp(7000)), seed) * 0.25
    r = E.rng(seed + 1)
    clicks = np.zeros((lp_.n, 2))
    for i in r.integers(0, lp_.n, int(lp_.L * 6)):
        clicks[i, r.integers(0, 2)] += r.uniform(0.3, 1.0) * r.choice([-1, 1])
    clicks = E._norm(circular(clicks, E.chain(E.hp(900), E.lp(5000))))
    for k in range(3):
        lp_.dry[k * lp_.n:(k + 1) * lp_.n] += (hiss + 0.8 * clicks) * level


# ---------------------------------------------------------------- the tracks

def starting():
    lp_ = Loop(78, swing=0.58, seed=1)
    prog = {1: 'Dmaj9', 3: 'Bm9', 5: 'Gmaj9', 7: 'A13sus', 9: 'Gmaj9', 10: 'F#m7', 11: 'Em9', 12: 'A13',
            13: 'Dmaj9', 14: 'Bm9', 15: 'Gmaj9', 16: 'A13sus'}
    voicing = {'Dmaj9': ['A3', 'C#4', 'E4', 'F#4'], 'Bm9': ['A3', 'C#4', 'D4', 'F#4'], 'Gmaj9': ['B3', 'D4', 'F#4', 'A4'],
               'A13sus': ['G3', 'B3', 'D4', 'F#4'], 'F#m7': ['E3', 'A3', 'C#4', 'E4'], 'Em9': ['D4', 'F#4', 'G4', 'B4'],
               'A13': ['G3', 'C#4', 'F#4', 'B4']}
    roots = {'Dmaj9': 'D2', 'Bm9': 'B1', 'Gmaj9': 'G1', 'A13sus': 'A1', 'F#m7': 'F#1', 'Em9': 'E2', 'A13': 'A1'}
    fifths = {'Dmaj9': 'A2', 'Bm9': 'F#2', 'Gmaj9': 'D2', 'A13sus': 'E2', 'F#m7': 'C#2', 'Em9': 'B1', 'A13': 'E2'}
    cur = None
    for bar in range(1, 17):
        cur = prog.get(bar, cur)
        v = voicing[cur]
        chord_hits(lp_, bar, 0, v, 0.55, 1.6, piano, gain=0.26)
        chord_hits(lp_, bar, 2.5, v, 0.42, 1.3, piano, gain=0.2)
        lp_.add(soft_bass(N(roots[cur]), 1.5 * lp_.beat), lp_.t(bar, 0), 0, 0.42, 0.04)
        lp_.add(soft_bass(N(fifths[cur]), 0.9 * lp_.beat, 0.7), lp_.t(bar, 2.5), 0, 0.36, 0.04)
    line = {
        1: [(0, 'F#5', 1), (1.5, 'A5', 0.5), (2, 'B5', 1.5)], 2: [(0.5, 'A5', 0.5), (1, 'F#5', 1), (2.5, 'E5', 1.5)],
        3: [(0, 'D5', 1.5), (2, 'E5', 0.5), (2.5, 'F#5', 1.5)], 4: [(1, 'A5', 2.5)],
        5: [(0, 'B5', 1), (1, 'A5', 0.5), (1.5, 'F#5', 1), (2.5, 'A5', 1.5)], 6: [(0.5, 'B5', 0.5), (1, 'D6', 2.5)],
        7: [(0, 'C#6', 1), (1, 'B5', 1), (2, 'A5', 1.5)], 8: [(0, 'E5', 3)],
        9: [(0, 'D6', 1), (1, 'B5', 1), (2, 'A5', 2)], 10: [(1, 'F#5', 1), (2, 'A5', 1), (3, 'C#6', 1)],
        11: [(0, 'B5', 2), (2.5, 'G5', 1)], 12: [(0, 'A5', 3)],
        13: [(0, 'F#5', 1), (1, 'A5', 1), (2, 'D6', 1.5)], 14: [(0.5, 'C#6', 0.5), (1, 'B5', 2)],
        15: [(0, 'D6', 1), (1, 'E6', 1), (2, 'F#6', 1.5)], 16: [(0, 'E6', 2), (2.5, 'C#6', 1)],
    }
    melody(lp_, line, ep, 0.34, 0.15, 0.32, 0.62)
    for bar in (4, 8, 12, 16):
        lp_.add(E.glock(N('A6'), 0.5, seed=bar), lp_.t(bar, 3), -0.35, 0.09, 0.45)
        lp_.add(E.glock(N('D7'), 0.45, seed=bar + 1), lp_.t(bar, 3.5), 0.35, 0.08, 0.45)
    drums(lp_, 16, lambda b: (0, 2.5) if b % 2 else (0, 1.5, 2.5), lambda b: (1, 3))
    vinyl_bed(lp_, 0.03, 7)
    return lp_.render('room', 1.0, lofi_bus(1.25, 6800, 0.5)), lp_


def brb():
    lp_ = Loop(72, swing=0.6, seed=2)
    prog = {1: 'Dm9', 3: 'G13', 5: 'Cmaj9', 7: 'Am9', 9: 'Fmaj9', 11: 'Em9', 13: 'Dm9', 15: 'G13'}
    voicing = {'Dm9': ['F3', 'A3', 'C4', 'E4'], 'G13': ['F3', 'B3', 'E4', 'A4'], 'Cmaj9': ['E3', 'G3', 'B3', 'D4'],
               'Am9': ['G3', 'B3', 'C4', 'E4'], 'Fmaj9': ['E3', 'A3', 'C4', 'G4'], 'Em9': ['D3', 'G3', 'B3', 'F#4']}
    roots = {'Dm9': 'D2', 'G13': 'G1', 'Cmaj9': 'C2', 'Am9': 'A1', 'Fmaj9': 'F1', 'Em9': 'E2'}
    fifths = {'Dm9': 'A1', 'G13': 'D2', 'Cmaj9': 'G1', 'Am9': 'E2', 'Fmaj9': 'C2', 'Em9': 'B1'}
    keys = Loop(72, swing=0.6, seed=3)
    keys.L, keys.n = lp_.L, lp_.n
    cur = None
    for bar in range(1, 17):
        cur = prog.get(bar, cur)
        v = voicing[cur]
        chord_hits(keys, bar, 0, v, 0.6, 2.6, ep, spread=0.016, gain=0.3)
        if bar % 2 == 0:
            nxt = voicing[prog.get(bar + 1, cur)] if bar < 16 else voicing['Dm9']
            chord_hits(keys, bar, 3.5, nxt, 0.48, 0.45, ep, spread=0.012, gain=0.22)
        lp_.add(soft_bass(N(roots[cur]), 2.4 * lp_.beat), lp_.t(bar, 0), 0, 0.44, 0.04)
        lp_.add(soft_bass(N(fifths[cur]), 0.9 * lp_.beat, 0.7), lp_.t(bar, 3), 0, 0.34, 0.04)
    rate = lp_.periodic(0.25)
    lp_.dry += E.autopan(keys.dry, rate, 0.35)
    lp_.send += keys.send
    line = {
        1: [(0.5, 'A5', 1), (1.5, 'G5', 0.5), (2, 'E5', 2)],
        3: [(0.5, 'F5', 1), (1.5, 'E5', 0.5), (2, 'D5', 2)], 4: [(2, 'B4', 1), (3, 'D5', 1)],
        5: [(0, 'E5', 1.5), (2, 'G5', 1), (3, 'B5', 1)], 6: [(0, 'A5', 3)],
        7: [(0.5, 'G5', 0.5), (1, 'E5', 1), (2, 'C5', 1), (3, 'D5', 1)], 8: [(0, 'E5', 3)],
        9: [(0.5, 'A5', 1), (1.5, 'G5', 0.5), (2, 'F5', 1), (3, 'E5', 1)], 10: [(0, 'C5', 3)],
        11: [(0.5, 'B5', 1), (1.5, 'A5', 0.5), (2, 'G5', 2)], 12: [(2, 'E5', 1), (3, 'F#5', 1)],
        13: [(0, 'F5', 1.5), (2, 'E5', 1), (3, 'D5', 1)], 14: [(0, 'C5', 2), (2, 'E5', 2)],
        15: [(0, 'D5', 1), (1, 'F5', 1), (2, 'A5', 1), (3, 'C6', 1)], 16: [(0, 'B5', 2), (2.5, 'G5', 1)],
    }
    melody(lp_, line, lambda f, v, d: piano(f, v, d, 5), 0.3, 0.2, 0.3, 0.6)
    drums(lp_, 16, lambda b: (0, 2.5) if b % 4 else (0, 1.5, 2.5), lambda b: (2,), snare_gain=0.28, hat_gain=0.08)
    vinyl_bed(lp_, 0.035, 9)
    return lp_.render('room', 1.0, lofi_bus(1.3, 6000, 0.6)), lp_


def ending():
    lp_ = Loop(66, swing=0.56, seed=4)
    prog = {1: 'Abmaj9', 3: 'Fm9', 5: 'Dbmaj9', 7: 'Eb13sus', 9: 'Dbmaj9', 11: 'Cm9', 13: 'Bbm9', 15: 'Eb13sus'}
    voicing = {'Abmaj9': ['Eb3', 'G3', 'Bb3', 'C4'], 'Fm9': ['Ab3', 'C4', 'Eb4', 'G4'], 'Dbmaj9': ['F3', 'Ab3', 'C4', 'Eb4'],
               'Eb13sus': ['Db3', 'F3', 'Ab3', 'C4'], 'Cm9': ['Eb3', 'G3', 'Bb3', 'D4'], 'Bbm9': ['Db3', 'F3', 'Ab3', 'C4']}
    roots = {'Abmaj9': 'Ab1', 'Fm9': 'F1', 'Dbmaj9': 'Db2', 'Eb13sus': 'Eb1', 'Cm9': 'C2', 'Bbm9': 'Bb1'}
    pattern = [0, 1, 2, 3, 2, 1, 2, 3]
    cur = None
    for bar in range(1, 17):
        new = bar in prog
        cur = prog.get(bar, cur)
        v = voicing[cur]
        for k, idx in enumerate(pattern):
            lp_.add(piano(N(v[idx]), 0.42 + (0.08 if k == 0 else 0), 2.6 * lp_.beat, k), lp_.t(bar, k * 0.5), -0.35 + 0.2 * idx, 0.2, 0.32)
        if new:
            lp_.add(E.strings_pad([N(x) for x in v], 2 * 4 * lp_.beat - 0.4, 0.7, 1.2, 1700, seed=bar), lp_.t(bar, 0), 0, 0.14, 0.4)
            lp_.add(soft_bass(N(roots[cur]), 3.6 * lp_.beat, 0.75), lp_.t(bar, 0), 0, 0.4, 0.05)
        else:
            lp_.add(soft_bass(N(roots[cur]), 1.8 * lp_.beat, 0.6), lp_.t(bar, 0), 0, 0.32, 0.05)
    line = {
        1: [(0, 'C5', 2), (2, 'Bb4', 1), (3, 'Ab4', 1)], 2: [(0, 'G4', 3)],
        3: [(0, 'Ab4', 1), (1, 'C5', 1), (2, 'Eb5', 2)], 4: [(0, 'Db5', 3)],
        5: [(0, 'C5', 1.5), (1.5, 'Ab4', 0.5), (2, 'F4', 2)], 6: [(1, 'Ab4', 1), (2, 'Bb4', 1), (3, 'C5', 1)],
        7: [(0, 'Eb5', 2), (2, 'Db5', 1), (3, 'C5', 1)], 8: [(0, 'Bb4', 3)],
        9: [(0, 'Ab4', 1.5), (1.5, 'Bb4', 0.5), (2, 'C5', 2)], 10: [(0, 'F5', 3)],
        11: [(0, 'Eb5', 1), (1, 'Db5', 1), (2, 'C5', 2)], 12: [(0, 'Bb4', 3)],
        13: [(0, 'Db5', 1.5), (1.5, 'C5', 0.5), (2, 'Bb4', 2)], 14: [(0, 'Ab4', 2), (2, 'C5', 2)],
        15: [(0, 'Bb4', 1), (1, 'Db5', 1), (2, 'F5', 2)], 16: [(0, 'Eb5', 3)],
    }
    melody(lp_, line, lambda f, v, d: piano(f, v, d, 7), 0.3, 0.15, 0.38, 0.6)
    drums(lp_, 16, lambda b: (0,), lambda b: (2,), snare=brush, kick_gain=0.3, snare_gain=0.22, hat_gain=0.06)
    vinyl_bed(lp_, 0.03, 11)
    return lp_.render('hall', 0.9, lofi_bus(1.15, 6200, 0.45)), lp_


TRACKS = {'starting': starting, 'brb': brb, 'ending': ending}


def seam_check(seg):
    """How the jump across the loop point compares to ordinary sample-to-sample steps."""
    d = np.abs(np.diff(seg, axis=0)).max(axis=1)
    joint = np.abs(seg[0] - seg[-1]).max()
    return joint / (np.percentile(d, 99.9) + 1e-12)


if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else '.'
    names = sys.argv[2:] or list(TRACKS)
    os.makedirs(out, exist_ok=True)
    for name in names:
        seg, lp_ = TRACKS[name]()
        seg, gr = loop_master(seg)
        E.write_wav24(os.path.join(out, f'music-{name}.wav'), seg)
        print(f'{name:9s} {lp_.bpm} bpm  {len(seg) / SR:5.1f} s  {E.lufs(np.tile(seg, (2, 1))):6.1f} LUFS  '
              f'peak {20 * np.log10(np.max(np.abs(seg))):5.1f} dBFS  limiter {gr:4.1f} dB  seam {seam_check(seg):.2f}', flush=True)
