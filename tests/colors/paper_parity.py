#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""
Paper and text/icon colour parity with the stock Plasma sticky note (org.kde.plasma.notes).

For every colour, from the renders tests/colors/render-all.sh makes of BOTH widgets at the
same size (ours-<c>-620-view, stock-<c>-620-view, ours-<c>-edit-edit, stock-<c>-edit-edit):

  svg     the paper item of each widget: imagePath / elementId must be identical
          (widgets/notes, <colour>-notes) and fill the widget.
  paper   the screenshots must be pixel-identical wherever neither widget draws
          content (outside the 7 % margins): same SVG element, same size, same pixels.
  rule    upstream's textIconColor: #dfdfdf on black and translucent-light, #202020 on
          every other paper (theme text colour only without a paper, i.e. in a panel).
          Checked on the toolbar icons (probe colour and rendered pixels, next to the
          stock widget's own toolbar icons) and on the body text. Body text may be pushed
          past the rule ONLY where the rule colour itself misses 4.5 : 1 on that paper;
          that is reported as a justified deviation with the numbers.

Exit status 1 on any mismatch.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import contrast as C  # noqa: E402


def rule_for(colour):
    return "#dfdfdf" if colour in ("black", "translucent-light") else "#202020"


def paper_item(scene):
    svgs = [it for it in scene.items if it["cls"].startswith("KSvg::SvgItem") and it.get("imagePath") == "widgets/notes"]
    return max(svgs, key=lambda it: it["r"][2] * it["r"][3]) if svgs else None


def content_rect(scene):
    """The widget's content box: our mainColumn / upstream's focusScope (both inset by the 7 % margins)."""
    for base in ("QQuickColumnLayout", "QQuickFocusScope"):
        for it in scene.items:
            if C.Scene.base(it) == base and it["vis"] and it["r"][2] > 100:
                return it["r"]
    return None


def icon_colours(scene, shot, stock):
    out = []
    for ic in scene.items:
        if C.Scene.base(ic) not in ("Icon", "KSvg::SvgItem") or not scene.visible(ic):
            continue
        if not scene.has_ancestor(ic, "ToolButton") or ic["r"][2] < 12:
            continue
        if not stock and scene.has_ancestor(ic, "NoteView"):
            continue
        x, y, w, h = C.rect_of(ic)
        pts = C.box_pixels(x, y, x + w, y + h)
        bg = C.mode_colour(shot, pts)
        fg = C.extreme_colour(shot, pts, bg)
        out.append((ic.get("source") or ic.get("elementId") or C.Scene.base(ic), ic.get("color"), C.hexc(fg), C.hexc(bg)))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", default="/tmp/colors/r")
    ap.add_argument("--colors", default=",".join(C.COLOURS))
    args = ap.parse_args()
    bad = 0
    for colour in [c for c in args.colors.split(",") if c]:
        f = lambda n: os.path.join(args.dir, n)
        need = ["ours-%s-620-view" % colour, "stock-%s-620-view" % colour, "ours-%s-edit-edit" % colour, "stock-%s-edit-edit" % colour]
        if not all(os.path.exists(f(n + ".json")) and os.path.exists(f(n + ".png")) for n in need):
            print("%-17s MISSING renders" % colour)
            bad += 1
            continue
        ours, stock = C.Scene(f(need[0] + ".json")), C.Scene(f(need[1] + ".json"))
        ours_shot, stock_shot = C.Shot(f(need[0] + ".png")), C.Shot(f(need[1] + ".png"))
        rule = rule_for(colour)
        problems = []

        # ---- svg element
        po, ps = paper_item(ours), paper_item(stock)
        so = (po["imagePath"], po["elementId"], [round(v) for v in po["r"]]) if po else None
        ss = (ps["imagePath"], ps["elementId"], [round(v) for v in ps["r"]]) if ps else None
        if so is None or so != ss:
            problems.append("svg ours %s != stock %s" % (so, ss))

        # ---- paper pixels outside both content boxes
        co, cs = content_rect(ours), content_rect(stock)
        x0 = int(min(co[0], cs[0])); y0 = int(min(co[1], cs[1]))
        x1 = int(max(co[0] + co[2], cs[0] + cs[2])); y1 = int(max(co[1] + co[3], cs[1] + cs[3]))
        diff = maxd = total = 0
        for y in range(ours_shot.h):
            for x in range(ours_shot.w):
                if x0 <= x < x1 and y0 <= y < y1:
                    continue
                total += 1
                a, b = ours_shot.get(x, y), stock_shot.get(x, y)
                if a != b:
                    diff += 1
                    maxd = max(maxd, max(abs(a[i] - b[i]) for i in range(3)))
        if diff:
            problems.append("paper differs in %d/%d px (max channel delta %d)" % (diff, total, maxd))

        # ---- stock text colour (probe + pixels)
        stock_text = [it for it in stock.items if it["cls"].startswith("TextArea") and it["vis"]]
        stock_probe = stock_text[0]["color"][3:] if stock_text else "?"
        st = C.text_span(stock, "Bodyword", cls_prefixes=("TextArea",))
        stock_px = "?"
        if st:
            r, fg, bgc = C.measure(stock_shot, st[0])
            stock_px = "%s (%.2f:1 on %s)" % (C.hexc(fg), r, C.hexc(bgc))
        if "#" + stock_probe != rule:
            problems.append("stock text %s is not the rule %s (harness?)" % (stock_probe, rule))

        # ---- icons: ours (view + edit) vs stock (focused: its toolbar is shown)
        ours_icons = icon_colours(ours, ours_shot, False)
        oe = C.Scene(f(need[2] + ".json"))
        ours_icons += icon_colours(oe, C.Shot(f(need[2] + ".png")), False)
        se = C.Scene(f(need[3] + ".json"))
        stock_icons = icon_colours(se, C.Shot(f(need[3] + ".png")), True)
        for name, probe, fg, bgc in ours_icons:
            if probe is None or probe[3:] != rule[1:]:
                problems.append("icon %s probe colour %s != %s" % (name, probe, rule))
            elif fg != rule:
                problems.append("icon %s renders %s on %s, not the rule %s" % (name, fg, bgc, rule))
        stock_icon_px = sorted({fg for _, _, fg, _ in stock_icons})

        # ---- body text
        body = C.text_span(ours, "Bodyword paragraph with")
        body_probe = body_px = "?"
        note = ""
        if body:
            r, fg, bgc = C.measure(ours_shot, body[0])
            body_px = "%s (%.2f:1 on %s)" % (C.hexc(fg), r, C.hexc(bgc))
            rule_rgb = tuple(int(rule[i:i + 2], 16) for i in (1, 3, 5))
            rule_ratio = C.ratio(rule_rgb, bgc)
            if C.hexc(fg) != rule:
                if rule_ratio < C.TEXT:
                    note = "justified deviation: rule %s is only %.2f:1 on %s" % (rule, rule_ratio, C.hexc(bgc))
                else:
                    problems.append("body text %s deviates from rule %s although the rule reaches %.2f:1" % (C.hexc(fg), rule, rule_ratio))
        print("%-17s rule %s | svg %s | paper diff %d px | stock text %s | stock icons %s | our icons %s | our body %s %s"
              % (colour, rule, "same" if so == ss else "DIFF", diff, stock_px, ",".join(stock_icon_px),
                 ",".join(sorted({fg for _, _, fg, _ in ours_icons})), body_px, note))
        for p in problems:
            print("   FAIL %s" % p)
        bad += bool(problems)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
