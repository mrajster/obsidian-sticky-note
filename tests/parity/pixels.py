#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Paint-level parity: are the decorations on the device pixels Obsidian paints them on?

    pixels.py <obsidian-rects.json> <render.png> [--origin X,Y] [--bg auto|R,G,B]

compare.py checks item RECTS. Rects cannot show how a renderer rasterises
them: Qt Quick's software renderer draws a fractional size ceil'd (a 114.06 px
rule is 115 px), the GPU renderer fills pixel centres, Chromium snaps every box
edge to the pixel it rounds to. This script predicts, from the reference rects
alone, the exact pixel rows/columns Chromium paints each decoration on:

    first = round(edge + origin), last = round(far edge + origin) - 1

and measures where the render actually has that decoration's colour. It runs
unchanged on Obsidian's own screenshot (calibration) and on a harness PNG, in
any colour scheme: each element's colour is sampled from the render at a point
the prediction says is inside it.

--origin is where the rects' content-box origin (0,0) lies in the PNG, e.g.
"32,32" for obsnote_qmlharness --png, "32,31.89" for an Obsidian window crop
whose sizer origin was at a fractional y.

Checked: hr, blockquote bar, callout box, code block, inline-code and tag pills,
highlight (mark), table grid lines, indentation guides (derived from nested
ul/ol as in compare.py), unchecked checkbox frames, bullet dots.
Exit 0 when every measured edge is within --tol px (default 0 = exact), else 1.
Needs python3 + Pillow + numpy.
"""

import argparse
import json
import sys

import numpy as np
from PIL import Image

GUIDE_INDENT_EM = 0.85


def rnd(v):
    """Chromium/Skia pixel snapping: round half up."""
    return int(np.floor(v + 0.5))


class Render:
    def __init__(self, path, origin):
        self.a = np.asarray(Image.open(path).convert("RGB")).astype(int)
        self.ox, self.oy = origin
        h, w, _ = self.a.shape
        vals, counts = np.unique(self.a.reshape(-1, 3), axis=0, return_counts=True)
        self.bg = tuple(int(v) for v in vals[np.argmax(counts)])
        self.h, self.w = h, w

    def px(self, x, y):
        """Pixel containing content-box point (x, y)."""
        return int(np.floor(x + self.ox)), int(np.floor(y + self.oy))

    def color(self, col, row):
        return tuple(int(v) for v in self.a[row, col])

    def like(self, rgb, tol):
        return np.abs(self.a - np.array(rgb)).max(axis=2) <= tol

    def run_extent(self, mask_line, start):
        """[first, last] of the contiguous True run through index start (None if start is False)."""
        if start < 0 or start >= len(mask_line) or not mask_line[start]:
            return None
        lo = start
        while lo > 0 and mask_line[lo - 1]:
            lo -= 1
        hi = start
        while hi + 1 < len(mask_line) and mask_line[hi + 1]:
            hi += 1
        return lo, hi

    def extremes(self, mask_line, lo, hi):
        """First and last True index inside [lo, hi]."""
        lo, hi = max(0, lo), min(len(mask_line) - 1, hi)
        idx = np.nonzero(mask_line[lo:hi + 1])[0]
        if len(idx) == 0:
            return None
        return lo + int(idx[0]), lo + int(idx[-1])


def derive_guides(doc, blocks):
    em = doc.get("baseFontPx", 16)
    out = []
    for b in blocks:
        if b.get("kind") in ("ul", "ol") and isinstance(b.get("depth"), int) and b["depth"] >= 2:
            out.append({"kind": "guide", "sourceLine": b.get("sourceLine"),
                        "x": b["x"] - GUIDE_INDENT_EM * em, "y": b["y"], "w": 1, "h": b["h"]})
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("reference")
    ap.add_argument("png")
    ap.add_argument("--origin", default="32,32")
    ap.add_argument("--tol", type=int, default=0)
    ap.add_argument("--color-tol", type=int, default=6, help="max channel distance to count as the sampled colour")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    doc = json.load(open(args.reference, encoding="utf-8"))
    blocks = doc["blocks"]
    ox, oy = (float(v) for v in args.origin.split(","))
    r = Render(args.png, (ox, oy))
    ctol = args.color_tol

    rows = []  # (element, axis, expected (first,last), measured (first,last) | None)

    def expect_rows(y, h):
        return rnd(y + oy), rnd(y + h + oy) - 1

    def expect_cols(x, w):
        return rnd(x + ox), rnd(x + w + ox) - 1

    def check(label, axis, expected, measured):
        rows.append((label, axis, expected, measured))

    def dominant(x, y, w, h):
        """Most common colour of the pixels strictly inside the predicted box (text is the minority)."""
        c0, c1 = rnd(x + ox) + 1, rnd(x + w + ox) - 2
        r0, r1 = rnd(y + oy) + 1, rnd(y + h + oy) - 2
        if c1 < c0 or r1 < r0:
            c0, c1, r0, r1 = rnd(x + ox), rnd(x + ox), rnd(y + oy), rnd(y + oy)
        patch = r.a[r0:r1 + 1, c0:c1 + 1].reshape(-1, 3)
        vals, counts = np.unique(patch, axis=0, return_counts=True)
        return tuple(int(v) for v in vals[np.argmax(counts)])

    def box_edges(label, b):
        """A filled box: its first/last painted row and column, over the predicted box +-3 px.

        Glyphs inside the box only remove pixels from single rows/columns, so the
        union over every line of the box still finds the true edges; rounded
        corners are covered by the lines through the box's middle.
        """
        mask = r.like(dominant(b["x"], b["y"], b["w"], b["h"]), ctol)
        ecol = expect_rows(b["y"], b["h"])
        erow = expect_cols(b["x"], b["w"])
        sub = mask[max(0, ecol[0] - 3):ecol[1] + 4, max(0, erow[0] - 3):erow[1] + 4]
        ys = np.nonzero(sub.any(axis=1))[0]
        xs = np.nonzero(sub.any(axis=0))[0]
        y0, x0 = max(0, ecol[0] - 3), max(0, erow[0] - 3)
        check(label, "rows", ecol, (y0 + int(ys[0]), y0 + int(ys[-1])) if len(ys) else None)
        check(label, "cols", erow, (x0 + int(xs[0]), x0 + int(xs[-1])) if len(xs) else None)

    for b in blocks:
        k = b.get("kind")
        line = b.get("sourceLine")
        lab = f"{k}@{line}"
        if k == "hr":
            c0, r0 = r.px(b["x"] + b["w"] / 2, b["y"] + b["h"] / 2)
            mask = r.like(r.color(c0, r0), ctol)
            check(lab, "rows", expect_rows(b["y"], b["h"]), r.run_extent(mask[:, c0], r0))
            check(lab, "cols", expect_cols(b["x"], b["w"]), r.run_extent(mask[r0, :], c0))
        elif k == "blockquote":
            c0, r0 = r.px(b["x"] + 1, b["y"] + b["h"] / 2)
            mask = r.like(r.color(c0, r0), ctol)
            check(lab + " bar", "rows", expect_rows(b["y"], b["h"]), r.run_extent(mask[:, c0], r0))
            check(lab + " bar", "cols", expect_cols(b["x"], 2), r.run_extent(mask[r0, :], c0))
        elif k in ("callout", "pre", "code-inline", "tag", "mark"):
            box_edges(lab, b)
        elif k == "checkbox" and not b.get("checked"):
            # frame: the 1 px left border column, and the top border row
            c0, r0 = r.px(b["x"] + 0.5, b["y"] + b["h"] / 2)
            mask = r.like(r.color(c0, r0), ctol)
            ecol = expect_rows(b["y"] + 4, b["h"] - 8)  # the straight part, inside the 4 px corners
            m = r.run_extent(mask[:, c0], r0)
            check(lab + " left border", "rows(straight part)", ecol, None if m is None else (max(m[0], ecol[0]), min(m[1], ecol[1])) if m[0] <= ecol[0] and m[1] >= ecol[1] else m)
            check(lab + " left border", "col", (rnd(b["x"] + ox), rnd(b["x"] + ox)), (c0, c0) if mask[r0, c0] else None)
        elif k == "bullet":
            cx, cy = b["x"] + b["w"] / 2, b["y"] + b["h"] / 2
            c0, r0 = r.px(cx, cy)
            win = r.a[r0 - 5:r0 + 6, c0 - 5:c0 + 6].astype(float)
            d = np.abs(win - np.array(r.bg)).max(axis=2)
            wsum = d.sum()
            if wsum > 0:
                my = (d.sum(axis=1) * np.arange(-5, 6)).sum() / wsum + r0 + 0.5 - oy
                mx = (d.sum(axis=0) * np.arange(-5, 6)).sum() / wsum + c0 + 0.5 - ox
                rows.append((lab, "centre(x,y)", (round(cx, 2), round(cy, 2)), (round(mx, 2), round(my, 2))))

    # table grid: horizontal rules at each row's top border and the table bottom,
    # vertical rules at each header cell's left border and the table's right edge.
    tables = [b for b in blocks if b.get("kind") == "table"]
    for t in tables:
        trs = [b for b in blocks if b.get("kind") == "tr" and t["y"] - 1 <= b["y"] <= t["y"] + t["h"]]
        ths = [b for b in blocks if b.get("kind") == "th" and abs(b["y"] - trs[0]["y"]) < 0.5] if trs else []
        if not trs or not ths:
            continue
        hrows = [rnd(tr["y"] - 0.5 + oy) for tr in trs] + [rnd(t["y"] + t["h"] - 1 + oy)]
        vcols = [rnd(th["x"] - 0.5 + ox) for th in ths] + [rnd(t["x"] + t["w"] - 1 + ox)]
        ecols = (rnd(t["x"] + ox), rnd(t["x"] + t["w"] + ox) - 1)
        erows = (rnd(t["y"] + oy), rnd(t["y"] + t["h"] + oy) - 1)
        c_mid = vcols[0]
        grid_rgb = r.color(c_mid, (erows[0] + erows[1]) // 2)
        mask = r.like(grid_rgb, ctol)
        for i, hr in enumerate(hrows):
            m = r.run_extent(mask[hr, :], ecols[0] + 3)
            check(f"table@{t.get('sourceLine')} rule{i}", f"row {hr} cols", ecols, m)
        for i, vc in enumerate(vcols):
            m = r.run_extent(mask[:, vc], (erows[0] + erows[1]) // 2)
            check(f"table@{t.get('sourceLine')} col{i}", f"col {vc} rows", erows, m)

    for g in derive_guides(doc, blocks):
        col = rnd(g["x"] + ox)
        erows = expect_rows(g["y"], g["h"])
        mid = (erows[0] + erows[1]) // 2
        mask = r.like(r.color(col, mid), ctol)
        check(f"guide@{g['sourceLine']}", f"col {col} rows", erows, r.run_extent(mask[:, col], mid))

    bad = 0
    for label, axis, exp, got in rows:
        if axis.startswith("centre"):
            ok = got is not None and abs(got[0] - exp[0]) <= 0.5 + args.tol and abs(got[1] - exp[1]) <= 0.5 + args.tol
        else:
            ok = got is not None and abs(got[0] - exp[0]) <= args.tol and abs(got[1] - exp[1]) <= args.tol
        bad += not ok
        if not ok or not args.quiet:
            print(f"{'ok  ' if ok else 'FAIL'} {label:<28} {axis:<22} expected {exp}  measured {got}")
    print(f"\n{len(rows) - bad}/{len(rows)} paint checks exact (tol {args.tol} px); page background {r.bg}")
    print("PASS" if bad == 0 else "FAIL")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
