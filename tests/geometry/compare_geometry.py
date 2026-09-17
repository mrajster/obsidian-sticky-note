#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Strict geometry parity check: Obsidian reading-view rects vs obsnote_qmlharness.

    compare_geometry.py <obsidian-rects.json> <ours.json>

Pairing: blocks are keyed by (kind, sourceLine, occurrence). Obsidian stamps
the children of a callout/blockquote with the CONTAINER's data-line while ours
carry their own raw line, so a reference block with no exact partner is paired
with the first unused block of the same kind whose sourceLine lies in
[ref.sourceLine, next larger reference sourceLine).

Ignored kinds (DOM wrappers / inline marks): ul, ol, pre-code, strong, em,
del, internal-link. "mark" (the ==highlight== background box) is compared.
A reference "guide" (indentation guide) is derived from every ul/ol at depth
>= 2: Obsidian draws it as that list's ::before, a 1 px border 0.85em left of
the list box spanning its height (x = ul.x - 0.85em, y = ul.y, w = 1, h = ul.h).

Tolerances (ABSOLUTE, content-box px):
  x, y, h, textStartX, firstLineBaselineY   1.0
  w                                          1.0 (2.0 for table/th/td/tr/code-inline/tag)
  lineCount                                  exact, when both sides report it

Exit 0 on full pass; 1 on any out-of-tolerance value, missing block or extra
block of a kind the reference emits; 2 on unreadable input.
"""

import json
import sys
from collections import Counter

IGNORED = {"ul", "ol", "pre-code", "strong", "em", "del", "internal-link"}
GUIDE_INDENT_EM = 0.85  # --indentation-guide-reading-indent: -0.85em
WIDE_W = {"table", "th", "td", "tr", "code-inline", "tag"}
TOL = 1.0
WIDE_TOL = 2.0
FIELDS = ("x", "y", "w", "h", "textStartX", "firstLineBaselineY")


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
    """Add a derived "guide" for every nested ul/ol of a browser dump."""
    em = doc.get("baseFontPx") if isinstance(doc.get("baseFontPx"), (int, float)) else 16
    out = []
    for b in blocks:
        out.append(b)
        if b.get("kind") in ("ul", "ol") and isinstance(b.get("depth"), int) and b["depth"] >= 2 \
                and all(isinstance(b.get(k), (int, float)) for k in ("x", "y", "h")):
            out.append({"kind": "guide", "sourceLine": b.get("sourceLine"), "text": "",
                        "x": round(b["x"] - GUIDE_INDENT_EM * em, 2), "y": b["y"], "w": 1, "h": b["h"]})
    return out


def keyed(blocks):
    seen = Counter()
    out = []
    for b in blocks:
        k = (b.get("kind"), b.get("sourceLine"))
        out.append((k + (seen[k],), b))
        seen[k] += 1
    return out


def num(b, key):
    v = b.get(key)
    return float(v) if isinstance(v, (int, float)) and not isinstance(v, bool) else None


def main(argv):
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    ref_doc, ref = load(argv[1])
    our_doc, ours = load(argv[2])
    for key in ("sizerWidth", "baseFontPx", "showInlineTitle"):
        if ref_doc.get(key) != our_doc.get(key):
            print(f"FAIL: header {key} differs: reference {ref_doc.get(key)!r} vs ours {our_doc.get(key)!r}")
            return 1

    ref_k = keyed(ref)
    our_k = keyed(ours)
    our_index = {k: i for i, (k, _) in enumerate(our_k)}
    used = set()
    ref_lines = sorted({b.get("sourceLine") for b in ref if isinstance(b.get("sourceLine"), int)})

    pairs, missing = [], []
    for k, r in ref_k:
        j = our_index.get(k)
        if j is None or j in used:
            j = None
            line = r.get("sourceLine")
            if isinstance(line, int):
                later = [ln for ln in ref_lines if ln > line]
                hi = later[0] if later else None
                for i, (ok, o) in enumerate(our_k):
                    ol = o.get("sourceLine")
                    if (i not in used and ok[0] == k[0] and isinstance(ol, int)
                            and ol >= line and (hi is None or ol < hi) and (ok not in dict(ref_k))):
                        j = i
                        break
        if j is None:
            missing.append(r)
            continue
        used.add(j)
        pairs.append((r, our_k[j][1]))

    ref_kinds = {b.get("kind") for b in ref}
    extra = [o for i, (_, o) in enumerate(our_k) if i not in used and o.get("kind") in ref_kinds]

    print(f"{'kind':<16}{'line':>5}" + "".join(f"{'d' + f:>12}" for f in FIELDS) + f"{'lines':>8}  status")
    failures = 0
    worst = {f: 0.0 for f in FIELDS}
    for r, o in pairs:
        kind = r.get("kind")
        cells, bad = [], False
        for f in FIELDS:
            a, b = num(r, f), num(o, f)
            if a is None or b is None:
                cells.append(f"{'':>12}")
                continue
            d = b - a
            tol = WIDE_TOL if (f == "w" and kind in WIDE_W) else TOL
            miss = abs(d) > tol
            bad |= miss
            worst[f] = max(worst[f], abs(d))
            cells.append(f"{(f'{d:+.2f}' + ('*' if miss else '')):>12}")
        lines = ""
        rl, ol = r.get("lineCount"), o.get("lineCount")
        if isinstance(rl, int) and isinstance(ol, int):
            lines = f"{ol}/{rl}"
            if rl != ol:
                bad = True
                lines += "*"
        failures += bad
        print(f"{kind:<16}{str(r.get('sourceLine')):>5}" + "".join(cells) + f"{lines:>8}  {'FAIL' if bad else 'ok'}")

    for b in missing:
        print(f"MISSING: {b.get('kind')} line {b.get('sourceLine')} {str(b.get('text') or '')[:40]!r}")
    for b in extra:
        print(f"EXTRA:   {b.get('kind')} line {b.get('sourceLine')} {str(b.get('text') or '')[:40]!r}")
    print(f"\nmatched {len(pairs)}/{len(ref)}; {failures} out of tolerance; {len(missing)} missing; {len(extra)} extra")
    print("worst |delta| px: " + ", ".join(f"{f} {v:.2f}" for f, v in worst.items()))
    ok = failures == 0 and not missing and not extra
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
