#!/usr/bin/env python3
"""
The sound of the intro film (gui/src/qml/intro): its music, and what is heard when
something happens in the picture, as one piece that starts with the film.

Synthesized from scratch with the engine of the alert sounds (make_alert_sounds.py
next to this file): no samples, nothing borrowed. Two of the app's own sounds play
where the film shows what they are for: the chat sound for the chat messages, the
follow alert for the alert on the stream.

The film runs to this music: D major, 100 beats a minute, the title two bars, every
other chapter three. The numbers come from the film itself (IntroFilm.qml: when each
chapter starts, when each ring is caught and by whom), so picture and sound cannot
drift apart; the smaller cues here (a card arrives, the link is copied) repeat the
offsets written in the film and have to be changed with them.

  the rings caught   a bell on the beat: the game plays the tune
  the bed            a soft pad, an electric piano, a marimba figure, light drums

    python3 make_intro_sound.py OUT_DIR

Writes intro.wav: 48 kHz stereo 24 bit, -18 LUFS, peaks below -1.5 dBFS. The app
ships it as 16-bit FLAC (gui/res/sounds/intro.flac):

    ffmpeg -i intro.wav -af aresample=osf=s16:dither_method=triangular -c:a flac -compression_level 12 intro.flac
"""
import os
import re
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_alert_sounds as E  # noqa: E402

SR = E.SR
N = E.N
FILM = os.path.join(HERE, '..', '..', 'gui', 'src', 'qml', 'intro', 'IntroFilm.qml')


def film_numbers():
    """The film's own timing: chapter starts and so on by name, and the rings as (time, player)."""
    src = open(FILM, encoding='utf-8').read()
    t = {}
    for name in ('beat', 'tPlay', 'tShare', 'tInvite', 'tModes', 'tCouch', 'tStream', 'tEnd', 'tLink', 'catchLead'):
        found = re.search(r'readonly property real %s: ([0-9.]+)' % name, src)
        if not found:
            raise SystemExit('IntroFilm.qml no longer says %s' % name)
        t[name] = float(found.group(1))
    catches = [(float(a), int(b)) for a, b in re.findall(r'\{ at: ([0-9.]+), by: (\d) \}', src)]
    if not catches:
        raise SystemExit('IntroFilm.qml no longer lists the rings caught')
    return t, catches


_cache = {}


def cached(key, make):
    if key not in _cache:
        _cache[key] = make()
    return _cache[key]


# Where each of the four sits in the stereo picture: you and Leo on the left, Mika and Ava on the right
SIDE = [-0.45, 0.45, -0.6, 0.6]

# One chord for each bar, as (bass, the pad's notes): I V vi, IV I V, vi IV V, ii IV V, I iii IV, ii IV V, I
BARS = [
    ('D2', ['D3', 'A3', 'F#4', 'E5']), ('D2', ['D3', 'A3', 'F#4', 'E5']),          # the title
    ('D2', ['D3', 'A3', 'F#4', 'A4']), ('C#2', ['C#3', 'A3', 'E4', 'A4']), ('B1', ['B2', 'A3', 'D4', 'F#4']),   # play together
    ('G1', ['G2', 'B3', 'D4', 'F#4']), ('F#1', ['F#2', 'A3', 'D4', 'F#4']), ('A1', ['A2', 'A3', 'C#4', 'E4']),  # what you share
    ('B1', ['B2', 'A3', 'D4', 'F#4']), ('G1', ['G2', 'B3', 'D4', 'G4']), ('A1', ['A2', 'A3', 'C#4', 'E4']),     # invite
    ('E2', ['E3', 'B3', 'D4', 'G4']), ('G1', ['G2', 'B3', 'D4', 'G4']), ('A1', ['A2', 'A3', 'C#4', 'E4']),      # who plays
    ('D2', ['D3', 'A3', 'F#4', 'A4']), ('F#1', ['F#2', 'A3', 'C#4', 'E4']), ('G1', ['G2', 'B3', 'D4', 'F#4']),  # talk and see
    ('E2', ['E3', 'B3', 'D4', 'G4']), ('G1', ['G2', 'B3', 'D4', 'G4']), ('A1', ['A2', 'G3', 'D4', 'E4']),       # streaming
    ('D2', ['D3', 'A3', 'F#4', 'A4']),                                                                           # the end
]

# The tune the rings play, one note for each ring in the film's order
TUNE = [
    'F#5', 'A5', 'B5', 'D6', 'F#5', 'A5', 'E6',                 # play together
    'B5', 'D6', 'C#6',                                          # what you share
    'F#5', 'B5', 'C#6', 'E6', 'A5',                             # invite
    'A5', 'C#6', 'A5', 'B5', 'D6',                              # talk and see
    'G5', 'B5', 'D6', 'B5', 'C#6', 'E6', 'E6', 'C#6',           # streaming, up to the end
]


def bell(name, vel=0.8):
    return cached(('bell', name, vel), lambda: E.glock(N(name), vel, seed=3))


def wood(name, vel=0.6):
    return cached(('wood', name, vel), lambda: E.marimba(N(name), vel, seed=5))


def keys(name, vel, dur):
    return cached(('keys', name, vel, dur), lambda: E.rhodes(N(name), vel, dur, 0.4))


def build():
    t, catches = film_numbers()
    beat = t['beat']
    bar = 4 * beat
    if len(catches) != len(TUNE):
        raise SystemExit('the film catches %d rings, the tune has %d notes' % (len(catches), len(TUNE)))
    if abs(t['tPlay'] - 2 * bar) > 1e-6 or abs(t['tEnd'] - 20 * bar) > 1e-6:
        raise SystemExit('the film no longer has two bars of title and three for each chapter')
    m = E.Mix(t['tEnd'] + 5.0)
    r = E.rng(48)

    def chapter_of(at):
        names = ['tPlay', 'tShare', 'tInvite', 'tModes', 'tCouch', 'tStream', 'tEnd']
        now = 'title'
        for name in names:
            if at >= t[name] - 1e-6:
                now = name
        return now

    # ------------------------------------------------------------ the bed

    for i, (bass, chord) in enumerate(BARS):
        at = i * bar
        chapter = chapter_of(at)
        if chapter == 'title':
            continue
        last = chapter == 'tEnd'
        thin = chapter == 'tModes'
        # A soft pad under everything; the last chord is held and left to ring
        pad = E.glass_pad([N(n) for n in chord], bar * (1.5 if last else 1.02), attack=0.2 if not last else 0.03, rel=1.2 if not last else 2.6)
        m.add(pad, at, 0.0, 0.085 if not last else 0.12, send=0.3)
        # The bass: the bar's first beat, and a shorter push on the third
        m.add(E.bass_note(N(bass) * 2, beat * (3.0 if last else 1.5), cutoff=330), at, 0.0, 0.19, send=0.02)
        if not last and not thin:
            m.add(E.bass_note(N(bass) * 2, beat * 0.8, cutoff=300), at + 2.5 * beat, 0.0, 0.12, send=0.02)
        # An electric piano lays the chord down, rolled a little
        for k, name in enumerate(chord[1:]):
            m.add(keys(name, 0.42, 1.9 if not last else 3.0), at + 0.012 * k, -0.2 + 0.2 * k, 0.085 if not thin else 0.06, send=0.22)
        if last:
            continue
        # A marimba figure in eighths over the chord, quiet: it keeps the film moving
        figure = [chord[1], chord[2], chord[3], chord[2], chord[1], chord[3], chord[2], chord[3]]
        for k, name in enumerate(figure):
            if thin and k % 2:
                continue
            f = N(name) * 2
            note = cached(('fig', round(f, 2)), lambda: E.marimba(f, 0.42, seed=7))
            m.add(note, at + k * beat / 2 + r.uniform(-0.004, 0.004), 0.3 if k % 2 else -0.3, 0.05 * (1.0 if k % 2 == 0 else 0.72), send=0.25)
        # Light drums: a soft kick on one, wood on three, a brush of air between the beats
        if not thin:
            m.add(E.kick(0.8, f_end=48, d=0.2, click=0.12, seed=i), at, 0.0, 0.2)
            if chapter == 'tStream' and i == len(BARS) - 2:
                # The last bar before the end pushes
                m.add(E.kick(0.7, f_end=48, d=0.18, click=0.1, seed=i + 50), at + 2 * beat, 0.0, 0.16)
        m.add(E.wood_tick(1300, seed=i), at + 2 * beat, 0.12, 0.075, send=0.2)
        for k in range(8):
            if k % 2:
                m.add(E.tick(7200, 0.003, seed=i * 8 + k), at + k * beat / 2, 0.25 if k % 4 == 1 else -0.25, 0.018 if not thin else 0.012, send=0.1)

    # ------------------------------------------------------------ the title

    # Two rings fly in from the sides and link
    for side in (-1, 1):
        air = E.whoosh(0.82, 260, 2600, q=1.0, rise=2.2, fall=0.05, seed=10 + side)[:, 0]
        m.add(E.move(air, 0.9 * side, 0.1 * side), 0.34, 0.0, 0.085, send=0.2)
    link = t['tLink']
    m.add(E.crystal(N('D6'), 0.7, seed=1), link, -0.15, 0.2, send=0.4)
    m.add(E.crystal(N('A6'), 0.5, seed=2), link + 0.012, 0.15, 0.13, send=0.4)
    m.add(E.glint(0.9, seed=3), link, 0.0, 0.028, send=0.3)
    m.add(E.sub_boom(62, 40, 1.4, 0.45), link, 0.0, 0.16)
    # The controller comes up under them, the plate closes behind
    m.add(E.bubble_note(N('D4'), 0.18), link + 0.1, 0.0, 0.09, send=0.2)
    m.add(E.thock(170, 0.03), link + 0.38, 0.0, 0.1, send=0.1)
    # The light the icon stands in: the first chord, slowly
    m.add(E.glass_pad([N(n) for n in BARS[0][1]], 3.2, attack=1.1, rel=1.6), link + 0.1, 0.0, 0.11, send=0.35)
    m.add(E.bass_note(N('D3'), 2.6, cutoff=260), 2 * bar - 2.4, 0.0, 0.12)
    # The name, letter by letter: a harp runs up
    for k, name in enumerate(['D4', 'A4', 'D5', 'F#5', 'A5', 'D6', 'F#6', 'A6']):
        m.add(E.pluck_string(N(name), 0.6, 1.8, bright=0.55, seed=20 + k), 2 * beat * 2 + 0.085 * k, -0.5 + k / 7.0, 0.085, send=0.4)
    # What it is for: a chord on the piano
    for k, name in enumerate(['D3', 'A3', 'F#4', 'E5']):
        m.add(E.felt_piano(N(name), 0.42, 1.6, 0.5, seed=30 + k), 3.3 + 0.016 * k, -0.25 + 0.17 * k, 0.11, send=0.3)
    # The icon goes to its corner
    away = E.whoosh(0.7, 1900, 320, q=0.9, rise=1.1, fall=0.12, seed=40)[:, 0]
    m.add(E.move(away, 0.0, -0.8), 4.25, 0.0, 0.05, send=0.2)

    # ------------------------------------------------------------ play together

    play = t['tPlay']
    m.add(E.pop(900, 190, 0.016, 0.04, seed=1), play + 0.05, 0.0, 0.11, send=0.15)
    for who, at in enumerate([play + 0.4, play + 2.1, play + 2.4, play + 2.7]):
        m.add(E.bubble(360 + 70 * who, 1250 + 180 * who, 0.04, 0.035), at, SIDE[who], 0.085, send=0.2)

    # Every ring: the button goes down, the explorer jumps, the ring sounds on the beat
    lead = t['catchLead']
    for (at, who), name in zip(catches, TUNE):
        side = SIDE[who] * 0.7
        m.add(E.thock(250 + 30 * who, 0.012), at - lead - 0.36, SIDE[who], 0.05)
        m.add(E.bubble(300, 640, 0.07, 0.05), at - lead, side * 0.5, 0.05, send=0.1)
        m.add(bell(name, 0.8), at, side, 0.2, send=0.34)
        m.add(E.pop(1500, 700, 0.008, 0.02, seed=who), at, side, 0.03)

    # ------------------------------------------------------------ what you share

    share = t['tShare']
    for k in range(3):
        m.add(E.wood_tick(1100 + 180 * k, seed=60 + k), share + 0.3 + 0.15 * k, -0.45, 0.055, send=0.2)
    # Another world for each place: air, and a glass tone of the new chord
    for at, name in [(share + 0.6, 'B5'), (share + bar, 'D6'), (share + 2 * bar, 'E6')]:
        turn = E.whoosh(0.3, 500, 3200, q=1.1, rise=1.8, fall=0.05, seed=int(at * 10))
        m.add(turn, at - 0.3, 0.0, 0.04, send=0.15)
        m.add(E.crystal(N(name), 0.55, seed=int(at)), at, 0.2, 0.1, send=0.4)

    # ------------------------------------------------------------ invite

    invite = t['tInvite']
    m.add(E.pop(1000, 300, 0.014, 0.035, seed=4), invite + 0.7, -0.4, 0.085, send=0.15)
    # Copy, and it says so
    m.add(E.tick(3000, 0.004, seed=5), invite + 1.2, -0.35, 0.09)
    m.add(E.musicbox(N('E6'), 0.6, seed=6), invite + 1.22, -0.35, 0.06, send=0.3)
    m.add(E.musicbox(N('A6'), 0.6, seed=7), invite + 1.32, -0.3, 0.06, send=0.3)
    # The link flies to each friend, and one after the other they are in
    for k, name in enumerate(['F#5', 'A5', 'D6']):
        m.add(E.zip_up(420 + 80 * k, 1500 + 250 * k, 0.6), invite + 1.8 + 0.3 * k, 0.15 + 0.2 * k, 0.04, send=0.2)
        m.add(bell(name, 0.9), invite + 2.7 + 0.3 * k, 0.3 + 0.15 * k, 0.16, send=0.4)
        m.add(E.bubble(500 + 120 * k, 1500 + 200 * k, 0.03, 0.03), invite + 2.7 + 0.3 * k, 0.3 + 0.15 * k, 0.06, send=0.2)
    m.add(E.wood_tick(1500, seed=70), invite + 3.9, 0.0, 0.05, send=0.2)

    # ------------------------------------------------------------ who plays

    modes = t['tModes']
    for k in range(3):
        m.add(E.pop(800 + 150 * k, 260, 0.014, 0.035, seed=80 + k), modes + 0.25 + 0.2 * k, -0.5 + 0.5 * k, 0.075, send=0.15)

    def lights(first, last, every, phases):
        """When the small pictures' lights arrive, as the film counts them from its start."""
        out = []
        k = int(first / every) - 1
        while k * every < last:
            for i, phase in enumerate(phases):
                at = k * every + phase
                if first <= at < last:
                    out.append((at, i))
            k += 1
        return sorted(out)

    # Together: one after the other into the same game, the same wood
    for at, i in lights(modes + 0.6, modes + 2.55, 2 * beat, [0, beat / 2, beat]):
        m.add(wood(['E5', 'G5', 'B5'][i], 0.7), at, -0.55 + 0.12 * i, 0.12, send=0.3)
    # One at a time: whoever's turn it is, on every beat
    for at, _ in lights(modes + 2.25, modes + 4.95, beat, [0]):
        turn = int(at / (2 * beat) + 1e-6) % 3
        m.add(wood(['G5', 'B5', 'D6'][turn], 0.75), at, -0.15 + 0.15 * turn, 0.12, send=0.3)
    # Each their own: three players, three instruments
    for at, i in lights(modes + 4.65, t['tCouch'] - 0.25, 2 * beat, [0, beat / 2, beat]):
        note = [wood('A5', 0.75), bell('C#6', 0.7), cached('own3', lambda: E.musicbox(N('E6'), 0.7, seed=9))][i]
        m.add(note, at, 0.35 + 0.15 * i, [0.12, 0.1, 0.08][i], send=0.3)

    # ------------------------------------------------------------ talk and see

    couch = t['tCouch']
    for k in range(4):
        m.add(E.bubble(440 + 60 * k, 1300 + 150 * k, 0.035, 0.03), couch + 0.45 + 0.15 * k, 0.4 + 0.08 * k, 0.065, send=0.2)
    # Their voices: no words, only the tune of talking, each at their own height
    talks = [(couch + 4.2, couch + 5.1), (couch + 1.2, couch + 2.5), (couch + 5.4, couch + 6.5), (couch + 2.8, couch + 3.9)]
    voice = [196.0, 330.0, 165.0, 392.0]
    for who, (a, b) in enumerate(talks):
        at = a + 0.04
        while at < b - 0.1:
            length = r.uniform(0.07, 0.15)
            f = voice[who] * 2 ** (r.choice([-2, 0, 0, 2, 3, 5]) / 12)
            m.add(E.bubble_note(f, length, bend=r.uniform(-1.5, 2.0), bend_at=length * 0.5), at, 0.5 + 0.1 * (who % 2), 0.05, send=0.12)
            at += length + r.uniform(0.04, 0.11)
    # The chat: the app's own chat sound, as it will be heard
    said, _ = E.chat()
    for at in (couch + 2.7, couch + 4.2, couch + 6.3):
        m.add(said, at, 0.0, 0.55, send=0.05)

    # ------------------------------------------------------------ streaming

    stream = t['tStream']
    live = stream + 1.2
    m.add(E.pop(900, 240, 0.014, 0.035, seed=90), stream + 0.6, -0.6, 0.075, send=0.15)
    # Go live: air into the press, the button, and a bright two-note "on"
    m.add(E.whoosh(0.4, 400, 3600, q=1.0, rise=2.0, fall=0.05, seed=91), live - 0.4, -0.3, 0.05, send=0.2)
    m.add(E.thock(190, 0.03), live, -0.5, 0.12)
    m.add(E.crystal(N('B5'), 0.6, seed=92), live + 0.01, -0.3, 0.1, send=0.4)
    m.add(E.crystal(N('E6'), 0.7, seed=93), live + 0.13, -0.2, 0.11, send=0.4)
    # Everybody's controller appears on the picture
    for k in range(4):
        m.add(E.pop(700 + 120 * k, 300, 0.012, 0.03, seed=94 + k), stream + 2.1 + 0.15 * k, -0.3 + 0.2 * k, 0.045, send=0.12)
    # An alert: the app's own sound for a new follower
    hello, _ = E.follow()
    m.add(hello, stream + 3.0, 0.0, 0.5, send=0.05)
    for k in range(4):
        m.add(E.wood_tick(1400 + 150 * k, seed=100 + k), stream + 3.6 + 0.3 * k, 0.6, 0.045, send=0.2)

    # ------------------------------------------------------------ the end

    end = t['tEnd']
    # Air gathers into the last chord
    m.add(E.whoosh(1.2, 300, 5200, q=0.9, rise=2.6, fall=0.07, seed=110), end - 1.2, 0.0, 0.07, send=0.25)
    m.add(E.kick(0.9, f_end=46, d=0.26, click=0.12, seed=111), end, 0.0, 0.22)
    m.add(E.sub_boom(60, 37, 2.2, 0.8), end, 0.0, 0.14)
    for k, name in enumerate(['D3', 'A3', 'D4', 'F#4', 'A4', 'E5']):
        m.add(E.felt_piano(N(name), 0.5, 3.2, 0.9, seed=120 + k), end + 0.014 * k, -0.4 + 0.16 * k, 0.1, send=0.35)
    high = E.vibraphone(N('D6'), 0.7, seed=130)
    m.add(high, end, 0.0, 0.15, send=0.4)
    m.add(E.glint(1.4, seed=131), end, 0.0, 0.03, send=0.3)
    # "Ready when you are": the harp once more, shorter
    for k, name in enumerate(['A4', 'D5', 'F#5', 'A5', 'D6']):
        m.add(E.pluck_string(N(name), 0.55, 1.8, bright=0.5, seed=140 + k), end + 0.75 + 0.09 * k, -0.3 + 0.15 * k, 0.075, send=0.4)
    # The two doors, and the way back to the start
    m.add(wood('D5', 0.7), end + 1.25, -0.3, 0.12, send=0.3)
    m.add(wood('A5', 0.7), end + 1.4, 0.3, 0.12, send=0.3)
    m.add(E.wood_tick(1500, seed=150), end + 1.9, 0.0, 0.04, send=0.2)

    return m.render('room', ret=0.9, target=-18.0)


if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else '.'
    os.makedirs(out, exist_ok=True)
    y, gr = build()
    E.write_wav24(os.path.join(out, 'intro.wav'), y)
    print(f'intro  {len(y) / SR:5.2f} s  {E.lufs(y):6.1f} LUFS  peak {20 * np.log10(np.max(np.abs(y))):5.1f} dBFS  '
          f'limiter {gr:4.1f} dB  dc {np.mean(y):+.4f}', flush=True)
