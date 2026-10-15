"""Procedural "wise guru" eye animation for the JC1060P470C EmoteDisplay.

Everything is drawn analytically in numpy (almond lenses, parabolic lids,
circular iris bands), so the output is flat-colour and free of background
anti-aliasing - which is what the .eaf palette/RLE format wants.
Transparency is palette index 0.

Usage:
    python guru_gen.py            # PNG frames + GIF + .eaf for every clip
    python guru_gen.py --no-eaf   # previews only, faster
"""

import argparse
import json
import math
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import eaf_encode as E  # noqa: E402

W, H = 640, 288
FPS = 20
BLOCK_H = 16

# ---------------------------------------------------------------- palette ----
C = {
    'sclera':    (228, 222, 208),
    'sclera_sh': (198, 190, 174),
    'iris_dark': (126, 54, 10),
    'iris_mid':  (196, 112, 24),
    'iris_lit':  (238, 172, 66),
    'iris_spk':  (252, 228, 160),
    'pupil':     (14, 12, 11),
    'glint':     (255, 255, 255),
    'lash':      (34, 28, 25),
    'crease':    (110, 96, 86),
    'rim':       (86, 72, 64),
    'brow':      (206, 202, 196),
    'brow_lo':   (126, 122, 116),
    'angry':     (176, 44, 22),
}
COLORS = list(C.values())
IDX = {name: i + 1 for i, name in enumerate(C)}

# ------------------------------------------------------------ geometry ------
EYE_L = (186.0, 178.0)
EYE_R = (450.0, 178.0)
EW, EH = 92.0, 78.0

_XX, _YY = np.meshgrid(np.arange(W, dtype=np.float64),
                       np.arange(H, dtype=np.float64))


def base_params():
    return dict(
        open=0.60,        # lid opening, 0 = shut
        hood=0.55,        # >0.5 puts more of the lid over the iris
        shear=0.14,       # outer-corner droop
        gx=0.0, gy=0.0,   # gaze, -1..1
        brow=0.0,         # brow raise in px (negative = lower)
        brow_tilt=0.0,    # + = inner ends down (angry), - = inner up (sad)
        brow_arch=0.18,
        brow_th=30.0,
        lash_t=10.0,
        pupil=1.0,
        iris=1.0,
        lid=1.0,          # weight of the upper-lid shadow / crease
        tint=None,
    )


def smooth(a, b, u):
    return a + (b - a) * u


def eye_mask(cx, cy, p, side, out):
    """Paint one almond eye. side=+1 for the right eye, -1 for the left."""
    X = (_XX - cx) / EW
    span = 1.0 - X * X
    in_lid = (np.abs(X) <= 1.0) & (span > 0)
    o = max(0.02, min(1.0, p['open']))

    yc = cy - p['shear'] * EH * X * side          # outer end sits lower
    a = EH * o * 1.7 * (1.0 - p['hood'])          # upper lid height
    b = EH * o * 1.7 * p['hood']                  # lower lid height
    # rounded, heavy upper lid / flatter lower lid = almond, not a circle
    top = yc - a * np.power(np.maximum(span, 1e-9), 0.70)
    bot = yc + b * np.power(np.maximum(span, 1e-9), 1.45)
    sclera = in_lid & (_YY >= top) & (_YY <= bot)
    aperture = bot - top

    out[sclera] = IDX['sclera']
    # shadow cast by the hooded upper lid - this is what makes the gaze "wise"
    shadow = sclera & (_YY <= top + aperture * 0.30 * p['lid'])
    out[shadow] = IDX['sclera_sh']

    ix = cx + p['gx'] * EW * 0.38
    iy = yc - 0.10 * EH * o + p['gy'] * EH * o * 0.40
    r = 47.0 * p['iris']
    d2 = (_XX - ix) ** 2 + (_YY - iy) ** 2
    dark = p['tint'] or 'iris_dark'
    ring = sclera & (d2 <= (r * 1.03) ** 2)
    out[ring] = IDX['lash']                       # limbal ring
    out[sclera & (d2 <= r * r)] = IDX[dark]
    out[sclera & (d2 <= (r * 0.82) ** 2)] = IDX['iris_mid']
    out[sclera & (d2 <= (r * 0.54) ** 2)] = IDX['iris_lit']
    out[sclera & (d2 <= (r * 0.28) ** 2)] = IDX['iris_spk']
    pr = 14.0 * p['pupil']
    out[sclera & (d2 <= pr * pr)] = IDX['pupil']
    g1 = ((_XX - (ix - r * 0.38)) ** 2 + (_YY - (iy - r * 0.44)) ** 2) <= (r * 0.20) ** 2
    g2 = ((_XX - (ix + r * 0.40)) ** 2 + (_YY - (iy + r * 0.36)) ** 2) <= (r * 0.09) ** 2
    out[sclera & g1] = IDX['glint']
    out[sclera & g2] = IDX['glint']

    # upper lash line: thick over the pupil, tapering into the corner
    taper = np.power(np.maximum(span, 1e-9), 0.55)
    lt = p['lash_t'] * taper
    lash = in_lid & (_YY >= top - lt) & (_YY < top)
    out[lash] = IDX['lash']
    # lid crease above the lash line - the heavy-lidded guru signature
    cre = in_lid & (_YY >= top - lt - 9.0 * p['lid'] * taper) & (_YY < top - lt - 3.0 * p['lid'] * taper)
    out[cre & ~lash] = IDX['crease']
    # a second, thinner line under the eye to seat it in the face
    rim = in_lid & (_YY > bot) & (_YY <= bot + 4.0 * taper)
    out[rim & ~lash] = IDX['rim']
    return sclera


def brow_mask(cx, cy, p, side, out):
    bx0 = cx - side * 4.0                          # brows lean slightly toward the nose
    X = (_XX - bx0) / (EW * 1.26)
    inside = np.abs(X) <= 1.0
    u = np.clip((1.0 + X * side) * 0.5, 0.0, 1.0)   # 0 at the nose end, 1 at the tail
    arch = p['brow_arch'] * EH * (1.0 - u * u)
    tilt = p['brow_tilt'] * EH * X * side
    hook = 4.0 * (1.0 - u) ** 2                     # inner ends sit a touch lower
    by = (cy - EH * 0.76 - p['brow'] - arch + tilt + hook
          - 0.35 * p['shear'] * EH * X * side)
    # bushy: thick and blunt over the nose, tapering to a thin tail, both tips rounded
    edge = np.minimum(np.clip((1.0 + X * side) / 0.26, 0.0, 1.0),
                      np.clip((1.0 - X * side) / 0.10, 0.0, 1.0))
    th = p['brow_th'] * (1.0 - 0.80 * u ** 1.5) * edge
    band = inside & (np.abs(_YY - by) <= th * 0.5)
    out[band] = IDX['brow']
    # soft shadow strip under the brow so it sits in a face instead of floating
    sh = 5.0 * np.sqrt(np.maximum(th, 1e-9) / 30.0)
    under = inside & (_YY > by + th * 0.5) & (_YY <= by + th * 0.5 + sh)
    out[under & ~band] = IDX['brow_lo']


def render(p_l, p_r, dy=0.0):
    out = np.zeros((H, W), dtype=np.uint8)
    shift = int(round(dy))
    for (cx, cy), p, side in ((EYE_L, p_l, -1), (EYE_R, p_r, 1)):
        cy2 = cy + shift
        brow_mask(cx, cy2, p, side, out)
        eye_mask(cx, cy2, p, side, out)
    return out


def to_rgba(idx):
    rgba = np.zeros((H, W, 4), dtype=np.uint8)
    for name, ci in IDX.items():
        m = idx == ci
        rgba[m, 0], rgba[m, 1], rgba[m, 2] = C[name]
        rgba[m, 3] = 255
    return rgba


# ------------------------------------------------------------- animation ----
def ease(u):
    return 0.5 - 0.5 * math.cos(math.pi * min(1.0, max(0.0, u)))


def apply_blink(p, t, at, dur=0.10):
    h = dur / 2
    if abs(t - at) > h:
        return
    u = 1.0 - abs(t - at) / h
    p['open'] = max(0.02, p['open'] * (1.0 - 0.98 * ease(u)))


def saccade(t, stops):
    cur = stops[0]
    nxt = stops[0]
    for i, s in enumerate(stops):
        if t >= s[0]:
            cur = s
            nxt = stops[min(i + 1, len(stops) - 1)]
    dt = t - cur[0]
    if nxt is cur or dt > 0.12:
        return cur[1], cur[2]
    u = ease(dt / 0.12)
    return smooth(cur[1], nxt[1], u), smooth(cur[2], nxt[2], u)


def clip_idle(t):
    p = base_params()
    p['open'] = 0.56
    p['gx'], p['gy'] = saccade(t, [(0.0, -0.15, 0.05), (0.35, 0.25, -0.05),
                                   (0.62, -0.30, 0.0), (0.85, 0.05, 0.05)])
    p['brow'] = 2.0 * math.sin(2 * math.pi * t)
    apply_blink(p, t, 0.48, 0.11)
    return p, p, 3.0 * math.sin(2 * math.pi * t)


def clip_listen(t):
    p = base_params()
    p['open'] = 0.82
    p['hood'] = 0.52
    p['brow'] = 10.0
    p['gx'], p['gy'] = saccade(t, [(0.0, -0.55, 0.0), (0.22, 0.55, 0.0),
                                   (0.45, -0.35, 0.05), (0.68, 0.40, -0.05),
                                   (0.86, 0.0, 0.0)])
    apply_blink(p, t, 0.30, 0.08)
    return p, p, 1.5 * math.sin(4 * math.pi * t)


def clip_think(t):
    l = base_params()
    r = base_params()
    for p in (l, r):
        p['open'] = 0.44
        p['hood'] = 0.72
        p['gy'] = -0.55
        p['gx'] = -0.40 + 0.10 * math.sin(2 * math.pi * t)
        p['pupil'] = 0.92
    l['brow'] = 11.0
    l['brow_arch'] = 0.30
    r['brow'] = -2.0
    apply_blink(l, t, 0.72, 0.13)
    apply_blink(r, t, 0.72, 0.13)
    return l, r, 2.0 * math.sin(2 * math.pi * t)


def clip_speak(t):
    p = base_params()
    p['open'] = 0.64
    p['brow'] = 5.0 + 7.0 * math.sin(4 * math.pi * t)
    p['gx'] = 0.18 * math.sin(6 * math.pi * t)
    p['gy'] = 0.10 * math.cos(4 * math.pi * t)
    apply_blink(p, t, 0.55, 0.09)
    return p, p, 7.0 * math.sin(4 * math.pi * t)


def clip_happy(t):
    p = base_params()
    p['open'] = 0.36
    p['hood'] = 0.40
    p['brow'] = 9.0
    p['brow_arch'] = 0.32
    p['gy'] = 0.10
    return p, p, 6.0 * abs(math.sin(3 * math.pi * t))


def clip_surprised(t):
    p = base_params()
    u = ease(t / 0.22) if t < 0.22 else 1.0
    settle = 0.25 * ease(max(0.0, (t - 0.55)) / 0.45)
    p['open'] = smooth(0.56, 0.98, u) - 0.20 * settle
    p['hood'] = 0.50
    p['brow'] = smooth(0.0, 20.0, u) - 8.0 * settle
    p['pupil'] = smooth(1.0, 0.62, u) + 0.2 * settle
    p['lash_t'] = 8.0
    return p, p, 1.6 * math.sin(6 * math.pi * t) * (1.0 - min(1.0, t * 2))


def clip_sad(t):
    l = base_params()
    r = base_params()
    for p in (l, r):
        p['open'] = 0.42
        p['hood'] = 0.68
        p['gy'] = 0.42
        p['pupil'] = 1.06
        p['brow_tilt'] = -0.26
        p['brow'] = -6.0
    l['gx'] = -0.30 + 0.06 * math.sin(2 * math.pi * t)
    r['gx'] = -0.18 + 0.06 * math.sin(2 * math.pi * t)
    apply_blink(l, t, 0.60, 0.14)
    apply_blink(r, t, 0.60, 0.14)
    return l, r, 2.5 * math.sin(2 * math.pi * t)


def clip_sleep(t):
    p = base_params()
    p['open'] = 0.28 - 0.18 * (0.5 + 0.5 * math.sin(2 * math.pi * t - math.pi / 2))
    p['hood'] = 0.78
    p['gy'] = 0.55
    p['brow'] = -11.0
    p['pupil'] = 1.10
    p['lash_t'] = 15.0
    return p, p, 4.0 * math.sin(2 * math.pi * t)


def clip_angry(t):
    p = base_params()
    p['open'] = 0.50
    p['hood'] = 0.66
    p['brow_tilt'] = 0.32
    p['brow'] = -10.0
    p['brow_arch'] = 0.10
    p['brow_th'] = 32.0
    p['tint'] = 'angry'
    p['pupil'] = 0.76
    p['gy'] = -0.10
    p['gx'] = 0.10 * math.sin(4 * math.pi * t)
    return p, p, 1.2 * math.sin(12 * math.pi * t)


def clip_wink(t):
    l = base_params()
    r = base_params()
    for p in (l, r):
        p['open'] = 0.60
        p['brow'] = 5.0
        p['gx'] = 0.20
    u = ease(min(1.0, t / 0.30)) if t < 0.30 else (1.0 - ease(min(1.0, (t - 0.62) / 0.30)))
    l['open'] = max(0.02, l['open'] * (1.0 - 0.98 * u))
    return l, r, 1.5 * math.sin(2 * math.pi * t)


def clip_confused(t):
    l = base_params()
    r = base_params()
    for p in (l, r):
        p['open'] = 0.62
        p['gy'] = -0.15
        p['gx'] = 0.45 * math.sin(2 * math.pi * t + 0.6)
    l['brow'] = 14.0
    l['brow_arch'] = 0.32
    r['brow'] = -6.0
    return l, r, 6.0 * math.sin(2 * math.pi * t)


CLIPS = {
    'guru_idle':      (clip_idle, 30, True),
    'guru_listen':    (clip_listen, 26, True),
    'guru_think':     (clip_think, 28, True),
    'guru_speak':     (clip_speak, 24, True),
    'guru_happy':     (clip_happy, 24, True),
    'guru_surprised': (clip_surprised, 20, True),
    'guru_sad':       (clip_sad, 26, True),
    'guru_sleep':     (clip_sleep, 24, True),
    'guru_angry':     (clip_angry, 20, True),
    'guru_wink':      (clip_wink, 20, True),
    'guru_confused':  (clip_confused, 24, True),
}

EMOTES = [
    ('idle', 'guru_idle'), ('neutral', 'guru_idle'),
    ('happy', 'guru_happy'), ('laughing', 'guru_happy'), ('funny', 'guru_happy'),
    ('delicious', 'guru_happy'), ('relaxed', 'guru_speak'),
    ('loving', 'guru_wink'), ('winking', 'guru_wink'), ('silly', 'guru_wink'),
    ('sad', 'guru_sad'), ('crying', 'guru_sad'), ('embarrassed', 'guru_sad'),
    ('angry', 'guru_angry'),
    ('surprised', 'guru_surprised'), ('shocked', 'guru_surprised'),
    ('thinking', 'guru_think'), ('confused', 'guru_confused'),
    ('sleepy', 'guru_sleep'), ('confident', 'guru_listen'),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'guru'))
    ap.add_argument('--only', default=None)
    ap.add_argument('--no-eaf', action='store_true')
    ap.add_argument('--no-preview', action='store_true',
                    help='skip PNG/GIF output (firmware asset build)')
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    frames_dir = os.path.join(args.out, 'frames')
    os.makedirs(frames_dir, exist_ok=True)

    pal_bytes, lookup = E.build_palette(COLORS)
    total = 0
    for name, (fn, n, loop) in CLIPS.items():
        if args.only and name != args.only:
            continue
        d = os.path.join(frames_dir, name)
        if not args.no_preview:
            os.makedirs(d, exist_ok=True)
        idxs = []
        gif_frames = []
        for i in range(n):
            t = i / n
            p_l, p_r, dy = fn(t)
            idx = render(p_l, p_r, dy)
            idxs.append(idx)
            if not args.no_preview:
                im = Image.fromarray(to_rgba(idx))
                im.save(os.path.join(d, 'f%02d.png' % i))
                gif_frames.append(im.convert('P', palette=Image.ADAPTIVE, colors=64))
        if not args.no_preview:
            gif_frames[0].save(os.path.join(args.out, name + '.gif'), save_all=True,
                               append_images=gif_frames[1:], duration=1000 // FPS,
                               loop=0, transparency=0, disposal=2)
        size = 0
        if not args.no_eaf:
            blob = E.encode_eaf(idxs, pal_bytes, BLOCK_H)
            open(os.path.join(args.out, name + '.eaf'), 'wb').write(blob)
            size = len(blob)
            total += size
        print('%-18s %2d frames  %8d B' % (name, n, size))

    mapping = [{'emote': e, 'src': c + '.eaf', 'loop': True, 'fps': FPS}
               for e, c in EMOTES]
    json.dump(mapping, open(os.path.join(args.out, 'emote.json'), 'w'),
              ensure_ascii=False)
    print('total .eaf payload: %d B (%.2f MiB)' % (total, total / 1048576))


if __name__ == '__main__':
    main()
