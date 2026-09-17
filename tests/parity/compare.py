#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare Obsidian's reading-view rects dump with obsnote_qmlharness --dump.

    compare.py <obsidian-rects.json> <ours.json> [--y-tol 2] [--x-tol 1.5]

Both files hold {"blocks": [{"kind", "sourceLine", "x", "y", "w", "h",
"textStartX", "firstLineBaselineY", "lineCount", ...}]} in content-box px.

Matching: blocks are paired IN ORDER by kind (and by sourceLine when both
sides carry one). Kinds that only exist as DOM wrappers or inline marks are
ignored: ul, ol, pre-code, strong, em, del, internal-link. "mark" (the
==highlight== background box) is compared.

Indentation guides: Obsidian draws a nested list's guide as that list's
::before -- a 1 px border 0.85em left of the nested ul/ol box, spanning its full
height (app.css `li > ul::before`). A reference "guide" is derived from every
ul/ol at depth >= 2 (x = ul.x - 0.85 * baseFontPx, y = ul.y, w = 1, h = ul.h)
and paired with the harness's measured "guide" entries.

Tolerances:
  * vertical offsets are compared RELATIVE TO THE PREVIOUS MATCHED BLOCK, so a
    font-metric drift in one block does not cascade down the page:
        dy = (ours.y - oursPrev.y) - (ref.y - refPrev.y)       |dy| <= --y-tol
    plus dh and the first-line baseline inside the block        <= --y-tol
  * horizontal geometry (x, w, textStartX) and every checkbox/bullet rect
    component (x, y-relative, w, h)                             <= --x-tol
    (w of table/tr/th/td/code-inline/tag uses the contract's 2.0 px)
  * lineCount must be equal when both sides report it.

Exit status: 0 all pass, 1 any failure / missing block / unexpected extra
block, 2 unreadable input. Pure python3 stdlib.
"""

import argparse
import json
import sys

IGNORED = {"ul", "ol", "pre-code", "strong", "em", "del", "internal-link"}
MARKER_KINDS = {"checkbox", "bullet", "guide"}
GUIDE_INDENT_EM = 0.85  # --indentation-guide-reading-indent: -0.85em
WIDE_W_KINDS = {"table", "tr", "th", "td", "code-inline", "tag"}
WIDE_W_TOL = 2.0


def load(path):
    try:
        with open(path, encoding="utf-8") as fh:
            doc = json.load(fh)
    except (OSError, ValueError) as exc:
        print(f"ERROR: cannot read {path}: {exc}", file=sys.stderr)
        sys.exit(2)
    blocks = doc.get("blocks") if isinstance(doc, dict) else None
    if not isinstance(blocks, list):
        print(f"ERROR: {path} has no 'blocks' list", file=sys.stderr)
        sys.exit(2)
    return doc, [b for b in with_guides(doc, blocks) if b.get("kind") not in IGNORED]


def with_guides(doc, blocks):
    """Insert a derived "guide" after every nested ul/ol of a browser dump (in place of the wrapper)."""
    em = doc.get("baseFontPx") if isinstance(doc.get("baseFontPx"), (int, float)) else 16
    out = []
    for b in blocks:
        out.append(b)
        if b.get("kind") in ("ul", "ol") and isinstance(b.get("depth"), int) and b["depth"] >= 2 \
                and all(isinstance(b.get(k), (int, float)) for k in ("x", "y", "h")):
            out.append({"kind": "guide", "sourceLine": b.get("sourceLine"), "text": "",
                        "x": round(b["x"] - GUIDE_INDENT_EM * em, 2), "y": b["y"], "w": 1, "h": b["h"]})
    return out


def num(b, key):
    v = b.get(key)
    return float(v) if isinstance(v, (int, float)) and not isinstance(v, bool) else None


def same_line(r, o):
    rl, ol = r.get("sourceLine"), o.get("sourceLine")
    return rl is None or ol is None or rl == ol


def next_line_after(ref, idx):
    """Smallest sourceLine greater than ref[idx]'s among the later reference blocks."""
    base = ref[idx].get("sourceLine")
    later = [b.get("sourceLine") for b in ref[idx + 1:]
             if isinstance(b.get("sourceLine"), int) and isinstance(base, int) and b.get("sourceLine") > base]
    return min(later) if later else None


def match(ref, ours):
    """In-order pairing. Returns (pairs, missing_ref, extra_ours).

    Exact (kind, sourceLine) first. Obsidian stamps children of a blockquote or
    callout with the CONTAINER's data-line, while ours carry their own raw line,
    so a second pass accepts the same kind whose line lies inside the reference
    block's line range [sourceLine, next larger reference sourceLine).
    """
    pairs, missing, extra = [], [], []
    ptr = 0
    for idx, r in enumerate(ref):
        found = None
        for j in range(ptr, len(ours)):
            if ours[j].get("kind") == r.get("kind") and same_line(r, ours[j]):
                found = j
                break
        if found is None and isinstance(r.get("sourceLine"), int):
            lo, hi = r["sourceLine"], next_line_after(ref, idx)
            for j in range(ptr, len(ours)):
                ol = ours[j].get("sourceLine")
                if ours[j].get("kind") == r.get("kind") and isinstance(ol, int) and ol >= lo and (hi is None or ol < hi):
                    found = j
                    break
        if found is None:
            missing.append(r)
            continue
        extra.extend(ours[ptr:found])
        pairs.append((r, ours[found]))
        ptr = found + 1
    extra.extend(ours[ptr:])
    return pairs, missing, extra


def fmt(v, bad):
    if v is None:
        return ""
    s = f"{v:+.2f}"
    return s + ("*" if bad else "")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("reference")
    ap.add_argument("ours")
    ap.add_argument("--y-tol", type=float, default=2.0)
    ap.add_argument("--x-tol", type=float, default=1.5)
    ap.add_argument("--quiet", action="store_true", help="print only failing rows and the summary")
    args = ap.parse_args()

    ref_doc, ref = load(args.reference)
    our_doc, ours = load(args.ours)

    for key in ("sizerWidth", "baseFontPx", "showInlineTitle"):
        if key in ref_doc and key in our_doc and ref_doc[key] != our_doc[key]:
            print(f"WARNING: {key} differs: reference {ref_doc[key]!r} vs ours {our_doc[key]!r}")

    pairs, missing, extra = match(ref, ours)

    header = (f"{'#':>3} {'kind':<16} {'line':>4} {'dy(rel)':>9} {'dx':>8} {'dw':>8} {'dh':>8} "
              f"{'dTextX':>8} {'dBase':>8} {'lines':>6}  status  text")
    print(header)
    print("-" * len(header))

    failures = 0
    worst = {"dy": 0.0, "dx": 0.0, "dw": 0.0, "dh": 0.0, "dTextX": 0.0, "dBase": 0.0}
    prev = None
    for n, (r, o) in enumerate(pairs):
        kind = r.get("kind")
        marker = kind in MARKER_KINDS
        ytol = args.x_tol if marker else args.y_tol
        wtol = WIDE_W_TOL if kind in WIDE_W_KINDS else args.x_tol
        bad = {}

        ry, oy = num(r, "y"), num(o, "y")
        dy = None
        if ry is not None and oy is not None:
            if prev is None:
                dy = oy - ry
            else:
                pry, poy = num(prev[0], "y"), num(prev[1], "y")
                dy = (oy - poy) - (ry - pry) if pry is not None and poy is not None else oy - ry
            bad["dy"] = abs(dy) > ytol

        def delta(key, tol):
            a, b = num(r, key), num(o, key)
            if a is None or b is None:
                return None
            d = b - a
            bad[key] = abs(d) > tol
            return d

        dx = delta("x", args.x_tol)
        dw = delta("w", wtol)
        dh = delta("h", args.x_tol if marker else args.y_tol)
        dtx = delta("textStartX", args.x_tol)

        dbase = None
        rb, ob = num(r, "firstLineBaselineY"), num(o, "firstLineBaselineY")
        if rb is not None and ob is not None and ry is not None and oy is not None:
            dbase = (ob - oy) - (rb - ry)
            bad["dBase"] = abs(dbase) > args.y_tol

        lines = ""
        rl, ol = r.get("lineCount"), o.get("lineCount")
        if isinstance(rl, int) and isinstance(ol, int):
            lines = f"{ol}/{rl}"
            if rl != ol:
                bad["lines"] = True
                lines += "*"

        failed = any(bad.values())
        failures += failed
        for name, val in (("dy", dy), ("dx", dx), ("dw", dw), ("dh", dh), ("dTextX", dtx), ("dBase", dbase)):
            if val is not None:
                worst[name] = max(worst[name], abs(val))

        status = "FAIL" if failed else "ok"
        if o.get("derived"):
            status += "~"
        if failed or not args.quiet:
            text = str(r.get("text") or "")[:28]
            print(f"{n:>3} {kind:<16} {str(r.get('sourceLine', '')):>4} "
                  f"{fmt(dy, bad.get('dy')):>9} {fmt(dx, bad.get('x')):>8} {fmt(dw, bad.get('w')):>8} "
                  f"{fmt(dh, bad.get('h')):>8} {fmt(dtx, bad.get('textStartX')):>8} "
                  f"{fmt(dbase, bad.get('dBase')):>8} {lines:>6}  {status:<6}  {text}")
        prev = (r, o)

    ref_kinds = {b.get("kind") for b in ref}
    counted_extra = [b for b in extra if b.get("kind") in ref_kinds]
    info_extra = [b for b in extra if b.get("kind") not in ref_kinds]

    for b in missing:
        print(f"MISSING in ours: {b.get('kind')} line {b.get('sourceLine')} {str(b.get('text') or '')[:40]!r}")
    for b in counted_extra:
        print(f"EXTRA in ours:   {b.get('kind')} line {b.get('sourceLine')} {str(b.get('text') or '')[:40]!r}")
    for b in info_extra:
        print(f"info: ours has kind the reference never emits: {b.get('kind')} line {b.get('sourceLine')}")

    print()
    print(f"matched {len(pairs)}/{len(ref)} reference blocks; {failures} out of tolerance; "
          f"{len(missing)} missing; {len(counted_extra)} extra")
    print("worst |delta| px: " + ", ".join(f"{k} {v:.2f}" for k, v in worst.items()))
    print(f"tolerances: y-relative/h/baseline {args.y_tol}px, x/w/textStartX/checkbox/bullet {args.x_tol}px"
          f" (w of {'/'.join(sorted(WIDE_W_KINDS))} {WIDE_W_TOL}px); '~' = derived, '*' = out of tolerance")

    ok = failures == 0 and not missing and not counted_extra
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
