# SPDX-License-Identifier: GPL-3.0-or-later
"""Stadium select menu pack of a new stadium: env_stadium_menu_<code>.pack.

The game's env_stadium_menuNN.pack (NN = stadium number) holds two C8 textures with their own
256-colour palettes: the photo (512x256) and the «Home of the ...» line (512x64). A new stadium
otherwise shows its base stadium's. This writes a copy of the base pack with both replaced. Numpy
only (Blender has no Pillow): k-means palette, C8 tiles through packwii.
"""
import numpy as np

from . import packwii

# Stadium numbers (1-based), as the runtime's mods.cpp kStadiumCodes.
CODES = ["bal", "bos", "nyy", "tb", "tor", "cws", "cle", "det", "kc", "min", "laa", "oka", "sea", "tex",
         "atl", "fla", "nym", "phi", "wsh", "chc", "cin", "hou", "mil", "pit", "st", "ari", "col", "lad",
         "sd", "sf"]
PHOTO = (256, 512)
LINE = (64, 512)


def base_menu_name(code):
    return f"env_stadium_menu{CODES.index(code) + 1:02d}.pack"


def palette_for(rgba, entries=256, iters=8, seed=1):
    """`entries` RGBA colours for the image: k-means on a sample, fully transparent kept apart."""
    flat = rgba.reshape(-1, 4).astype(np.float32)
    opaque = flat[flat[:, 3] > 8]
    rng = np.random.default_rng(seed)
    if len(opaque) == 0:
        return np.zeros((entries, 4), np.uint8)
    sample = opaque[rng.choice(len(opaque), min(len(opaque), 20000), replace=False)]
    k = entries - 1                                    # entry 0 stays transparent
    centres = sample[rng.choice(len(sample), min(k, len(sample)), replace=False)]
    for _ in range(iters):
        idx = nearest(sample, centres)
        for c in range(len(centres)):
            members = sample[idx == c]
            if len(members):
                centres[c] = members.mean(0)
    pal = np.zeros((entries, 4), np.float32)
    pal[1:1 + len(centres)] = centres
    return np.clip(pal + 0.5, 0, 255).astype(np.uint8)


def nearest(pixels, colours, chunk=8192):
    out = np.empty(len(pixels), np.int32)
    c = colours.astype(np.float32)
    for i in range(0, len(pixels), chunk):
        p = pixels[i:i + chunk].astype(np.float32)
        out[i:i + chunk] = ((p[:, None, :] - c[None, :, :]) ** 2).sum(-1).argmin(1)
    return out


def encode_c8(rgba):
    """(RGB5A3 palette u16[256], tiled C8 index bytes) for an (h, w, 4) uint8 image."""
    pal = packwii.to_rgb5a3(palette_for(rgba))
    colours = packwii.from_rgb5a3(pal)                 # what the game will show, after RGB5A3
    idx = nearest(rgba.reshape(-1, 4), colours).astype(np.uint8).reshape(rgba.shape[:2])
    idx[rgba[..., 3] <= 8] = 0
    return pal.astype(">u2"), packwii.tile(idx, 9).astype(np.uint8).tobytes()


def build(base, photo, line):
    """Copy of the base menu pack (bytes) with the photo and the line (uint8 (h, w, 4), top row
    first) written over its two textures."""
    d = bytearray(base)
    texs = sorted(packwii.find_textures(bytes(d)), key=lambda t: -t["h"])
    if len(texs) != 2 or (texs[0]["h"], texs[0]["w"]) != PHOTO or (texs[1]["h"], texs[1]["w"]) != LINE:
        raise ValueError("unexpected stadium menu pack layout")
    for t, img in zip(texs, (photo, line)):
        pal, pixels = encode_c8(img)
        n, p = t["pal"]
        d[p:p + 2 * n] = pal[:n].tobytes()
        d[t["data"]:t["data"] + len(pixels)] = pixels
    return bytes(d)
