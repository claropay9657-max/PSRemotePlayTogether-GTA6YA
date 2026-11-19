#!/usr/bin/env python3
"""
Makes the DualSense colour skins of the stream overlay.

CryptoPad brings three hand-made DualSense skins: white, black and cosmic red.
The other colours Sony sells are derived here from the white one by colouring
its shell and its buttons. They are approximations of the real controllers,
not photographs: the shading of the white skin with another colour on it.

    python3 scripts/make-dualsense-skins.py

Needs Pillow and NumPy. Writes, per colour:
    gui/res/overlay/cryptopad/static/images/PS5 (<name>)/base.png, button.png
    gui/res/overlay/cryptopad/static/ps5-<name>-controller.css
    gui/res/overlay/cryptopad/templates/PS5 (<name>)/index.html
and lists them in gui/res/overlay/overlay.qrc. Everything else (sticks,
triggers, touch pad ...) is shared with the white skin.
"""

import os
import re
import sys

import numpy as np
from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "gui", "res", "overlay")
PAD = os.path.join(ROOT, "cryptopad")
IMAGES = os.path.join(PAD, "static", "images")
QRC = os.path.join(ROOT, "overlay.qrc")


def rgb(hex_colour):
    h = hex_colour.lstrip("#")
    return np.array([int(h[i:i + 2], 16) for i in (0, 2, 4)], dtype=np.float32) / 255.0


def solid(colour):
    c = rgb(colour)
    return lambda h, w: np.broadcast_to(c, (h, w, 3))


def metallic(colour):
    """A solid colour with a soft sheen running across it."""
    c = rgb(colour)

    def make(h, w):
        y, x = np.mgrid[0:h, 0:w].astype(np.float32)
        band = np.sin((x / w * 1.6 + y / h * 0.9) * np.pi) * 0.5 + 0.5
        return np.clip(c[None, None, :] * (0.86 + 0.26 * band[..., None]), 0, 1)
    return make


def shifting(*colours):
    """Several colours flowing into each other, for the iridescent finishes."""
    stops = [rgb(c) for c in colours]

    def make(h, w):
        y, x = np.mgrid[0:h, 0:w].astype(np.float32)
        t = np.clip(x / w * 0.75 + y / h * 0.25, 0, 1) * (len(stops) - 1)
        i = np.clip(np.floor(t).astype(int), 0, len(stops) - 2)
        f = (t - i)[..., None]
        a = np.stack(stops)[i]
        b = np.stack(stops)[i + 1]
        return a * (1 - f) + b * f
    return make


def camouflage(*colours):
    """Blotches of a few tones. Always the same ones: the pattern is seeded."""
    tones = [rgb(c) for c in colours]

    def make(h, w):
        rng = np.random.default_rng(5)
        coarse = Image.fromarray((rng.random((14, 18)) * 255).astype(np.uint8))
        noise = np.asarray(coarse.resize((w, h), Image.BICUBIC)).astype(np.float32) / 255.0
        fine = Image.fromarray((rng.random((40, 52)) * 255).astype(np.uint8))
        noise = noise * 0.7 + np.asarray(fine.resize((w, h), Image.BICUBIC)).astype(np.float32) / 255.0 * 0.3
        edges = np.quantile(noise, np.linspace(0, 1, len(tones) + 1)[1:-1])
        index = np.digitize(noise, edges)
        return np.stack(tones)[index]
    return make


# Name in the app, and what goes onto the shell
SKINS = [
    ("nova pink", solid("#ec5fae")),
    ("starlight blue", solid("#7dbcec")),
    ("galactic purple", solid("#6d50a6")),
    ("gray camouflage", camouflage("#5a5d5b", "#84877f", "#aeb0a8", "#6f726c")),
    ("volcanic red", metallic("#b1262f")),
    ("cobalt blue", metallic("#2557bd")),
    ("sterling silver", metallic("#a3a8b3")),
    ("chroma pearl", shifting("#f6e6f1", "#e9f1fb", "#f7f0e2", "#e4f4ee")),
    ("chroma indigo", shifting("#4a2a96", "#2a3fb4", "#1b6cb8", "#5a2f9e")),
    ("chroma teal", shifting("#0e8f8a", "#2fc39a", "#1aa3b8", "#12907a")),
    ("30th anniversary", solid("#b3b5bb")),
]


def smoothstep(lo, hi, v):
    t = np.clip((v - lo) / (hi - lo), 0, 1)
    return t * t * (3 - 2 * t)


def recolour(image, colour_map, light_details=False):
    """
    Puts a colour on everything that is light and colourless: the white plastic.
    With light_details, what is drawn onto the plastic (the symbols on the
    buttons) comes out lighter than the colour instead of darker, so that it
    stays readable on a dark colour.
    """
    a = np.asarray(image.convert("RGBA")).astype(np.float32) / 255.0
    pixels = a[..., :3]
    brightest = pixels.max(-1)
    darkest = pixels.min(-1)
    lightness = (brightest + darkest) / 2
    saturation = np.where(brightest > 0, (brightest - darkest) / np.maximum(brightest, 1e-6), 0)
    # Light bar, glyphs of pressed buttons and so on keep their own colour
    weight = smoothstep(0.42, 0.62, lightness) * (1 - smoothstep(0.14, 0.32, saturation))
    # The white skin's shading carries over: its brightest plastic gets the full colour
    shade = np.clip(lightness / 0.90, 0, 1.08)
    h, w = lightness.shape
    colours = colour_map(h, w)
    if light_details:
        towards_white = np.clip((0.90 - lightness) * 3.2, 0, 0.85)[..., None]
        tinted = np.clip(colours + (1 - colours) * towards_white, 0, 1)
    else:
        tinted = np.clip(colours * shade[..., None], 0, 1)
    out = pixels * (1 - weight[..., None]) + tinted * weight[..., None]
    result = np.concatenate([out, a[..., 3:]], axis=-1)
    return Image.fromarray((np.clip(result, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA")


def main():
    with open(os.path.join(PAD, "static", "ps5-controller.css"), encoding="utf-8") as f:
        css = f.read()
    with open(os.path.join(PAD, "templates", "PS5", "index.html"), encoding="utf-8") as f:
        template = f.read()
    for needle in ("url(./images/PS5/base.png)", "url(./images/PS5/button.png)"):
        if needle not in css:
            sys.exit("ps5-controller.css does not look as expected: no " + needle)
    if "./static/ps5-controller.css" not in template:
        sys.exit("templates/PS5/index.html does not look as expected")

    base = Image.open(os.path.join(IMAGES, "PS5", "base.png"))
    button = Image.open(os.path.join(IMAGES, "PS5", "button.png"))
    files = []

    for name, colour_map in SKINS:
        skin = "PS5 (%s)" % name
        slug = "ps5-%s-controller.css" % name.replace(" ", "-")
        directory = os.path.join(IMAGES, skin)
        os.makedirs(directory, exist_ok=True)
        recolour(base, colour_map).save(os.path.join(directory, "base.png"), optimize=True)
        # The buttons sit in the middle of the pad: give them the colour found there
        middle = colour_map(base.height, base.width)[base.height // 2, base.width // 2]
        dark = float(middle @ np.array([0.299, 0.587, 0.114], dtype=np.float32)) < 0.5
        recolour(button, lambda h, w: np.broadcast_to(middle, (h, w, 3)), light_details=dark).save(
            os.path.join(directory, "button.png"), optimize=True)

        skin_css = css.replace("url(./images/PS5/base.png)", 'url("./images/%s/base.png")' % skin)
        skin_css = skin_css.replace("url(./images/PS5/button.png)", 'url("./images/%s/button.png")' % skin)
        with open(os.path.join(PAD, "static", slug), "w", encoding="utf-8") as f:
            f.write(skin_css)

        os.makedirs(os.path.join(PAD, "templates", skin), exist_ok=True)
        page = template.replace("./static/ps5-controller.css", "./static/" + slug)
        page = re.sub(r"<title>[^<]*</title>", "<title>%s Controller Visualizer</title>" % skin, page, count=1)
        with open(os.path.join(PAD, "templates", skin, "index.html"), "w", encoding="utf-8") as f:
            f.write(page)

        files += [
            "cryptopad/static/images/%s/base.png" % skin,
            "cryptopad/static/images/%s/button.png" % skin,
            "cryptopad/static/" + slug,
            "cryptopad/templates/%s/index.html" % skin,
        ]

    with open(QRC, encoding="utf-8") as f:
        qrc = f.read()
    missing = [p for p in files if "<file>%s</file>" % p not in qrc]
    if missing:
        lines = "".join("    <file>%s</file>\n" % p for p in missing)
        end = qrc.rindex("  </qresource>")
        qrc = qrc[:end] + lines + qrc[end:]
        with open(QRC, "w", encoding="utf-8") as f:
            f.write(qrc)
    print("%d skins written, %d new entries in overlay.qrc" % (len(SKINS), len(missing)))


if __name__ == "__main__":
    main()
