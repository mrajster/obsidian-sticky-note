#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""
WCAG 2.x contrast of every coloured element of the rendered note, per paper colour.

Input: the renders written by tests/colors/render.sh (via render-all.sh): for each
screenshot  <dir>/ours-<colour>-<size>-<shot>.png  there is a scene dump
<dir>/ours-<colour>-<size>-<shot>.json  taken by probe.cpp at the same moment. The dump
holds every Qt Quick item's scene rectangle (screenshot pixels: the window sits at 0,0)
and, for text edits, the caret rectangle of every character, so an element is located
exactly -- by its unique demo string (tests/colors/demo.md) or by its item type -- and
never guessed.

For each element the LOCAL background is the most common colour of the pixels the
element sits on (the paper, a callout tint, a pill, the code background, the mark),
and the foreground is the most extreme rendered colour against it that at least 3
pixels have (glyph cores and 1 px lines are drawn in their exact colour; a lone
antialiasing outlier cannot win). An element seen in several renders (the 620 px
pages and the 1700 px full-length render) keeps its worst ratio.

FAIL below 4.5 : 1 for text and 3 : 1 for non-text UI (WCAG 1.4.3 / 1.4.11).
Exit status 1 when anything fails, 2 when a required element was never found.

usage: contrast.py [--dir /tmp/colors/r] [--colors white,black,...] [--json out.json]
"""

import argparse
import glob
import json
import os
import re
import sys
from collections import Counter

try:
    from PIL import Image
except ImportError:  # pragma: no cover - PIL is the only non-stdlib dependency
    Image = None

COLOURS = ["white", "black", "red", "orange", "yellow", "green", "blue", "pink",
           "translucent", "translucent-light"]
TEXT, UI = 4.5, 3.0
# render variants measured (render-all.sh): 620 px pages, the full-length render, edit mode,
# the placeholder state and the dark colour scheme
VARIANTS = "620|tall|edit|missing|dark620|banner"


# ---------------------------------------------------------------- colour maths
def _channel(v):
    v /= 255.0
    return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4


def luminance(c):
    return 0.2126 * _channel(c[0]) + 0.7152 * _channel(c[1]) + 0.0722 * _channel(c[2])


def ratio(a, b):
    la, lb = luminance(a), luminance(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def hexc(c):
    return "#%02x%02x%02x" % tuple(c[:3])


# ---------------------------------------------------------------- image access
class Shot:
    def __init__(self, png):
        if Image is not None:
            im = Image.open(png).convert("RGB")
            self.w, self.h = im.size
            self.px = im.load()
        else:  # ImageMagick txt: fallback
            import subprocess
            out = subprocess.run(["magick", png, "-depth", "8", "txt:-"], capture_output=True,
                                 text=True, check=True).stdout.splitlines()
            w, h = out[0].split(":")[1].strip().split(",")[0:2]
            self.w, self.h = int(w), int(h)
            data = {}
            for line in out[1:]:
                xy, rest = line.split(":", 1)
                x, y = map(int, xy.split(","))
                hx = rest.split("#", 1)[1][:6]
                data[(x, y)] = (int(hx[0:2], 16), int(hx[2:4], 16), int(hx[4:6], 16))
            self.px = data

    def get(self, x, y):
        return self.px[x, y] if isinstance(self.px, dict) is False else self.px[(x, y)]


def box_pixels(x0, y0, x1, y1):
    """Integer pixels whose centres lie inside [x0, x1) x [y0, y1)."""
    return [(x, y) for y in range(int(round(y0)), int(round(y1)))
            for x in range(int(round(x0)), int(round(x1)))]


def mode_colour(shot, pts):
    cnt = Counter(shot.get(x, y) for x, y in pts)
    return cnt.most_common(1)[0][0] if cnt else None


def extreme_colour(shot, pts, bg, support=3):
    """The most contrasting colour (vs bg) that at least `support` pixels reach or exceed."""
    cnt = Counter(shot.get(x, y) for x, y in pts)
    ranked = sorted(cnt.items(), key=lambda kv: -ratio(kv[0], bg))
    seen = 0
    for colour, n in ranked:
        seen += n
        if seen >= support:
            return colour
    return ranked[-1][0] if ranked else None


# ---------------------------------------------------------------- scene dump
class Scene:
    def __init__(self, path):
        doc = json.load(open(path))
        wins = [w for w in doc["windows"] if w["visible"] and w["cls"] == "PlasmaWindowedView"]
        if not wins:
            wins = [w for w in doc["windows"] if w["visible"]]
        self.win = wins[0]
        self.items = self.win["items"]
        stack = []
        for i, it in enumerate(self.items):
            while stack and stack[-1]["d"] >= it["d"]:
                stack.pop()
            it["parent"] = stack[-1] if stack else None
            it["i"] = i
            stack.append(it)

    @staticmethod
    def base(it):
        return it["cls"].split("_QML")[0]

    def ancestors(self, it):
        p = it["parent"]
        while p is not None:
            yield p
            p = p["parent"]

    def has_ancestor(self, it, base):
        return any(self.base(a) == base for a in self.ancestors(it))

    def descendants(self, it):
        out = []
        for j in range(it["i"] + 1, len(self.items)):
            if self.items[j]["d"] <= it["d"]:
                break
            out.append(self.items[j])
        return out

    def clip(self, it):
        """Visible scene rect for the item: its nearest Flickable, else the window."""
        x0, y0 = 0, 0
        x1, y1 = self.win["geom"][2], self.win["geom"][3]
        for a in self.ancestors(it):
            if self.base(a) == "QQuickFlickable" and a["r"][2] > 0:
                ax, ay, aw, ah = a["r"]
                return max(x0, ax), max(y0, ay), min(x1, ax + aw), min(y1, ay + ah)
        return x0, y0, x1, y1

    def visible(self, it):
        return it["vis"] and all(a["op"] > 0 for a in self.ancestors(it)) and it["op"] > 0

    def find(self, base=None, pred=None):
        return [it for it in self.items if (base is None or self.base(it) == base)
                and self.visible(it) and (pred is None or pred(it))]


def inside(box, clip, pad=0):
    x0, y0, x1, y1 = box
    return x0 >= clip[0] - pad and y0 >= clip[1] - pad and x1 <= clip[2] + pad and y1 <= clip[3] + pad


# ---------------------------------------------------------------- samples
class Sample:
    def __init__(self, fg_pts, bg_pts, box, where):
        self.fg_pts, self.bg_pts, self.box, self.where = fg_pts, bg_pts, box, where


def text_span(scene, needle, cls_prefixes=("QQuickTextEdit", "TextArea")):
    out = []
    for it in scene.items:
        if not it["cls"].startswith(cls_prefixes) or not scene.visible(it):
            continue
        t = it.get("text") or ""
        carets = it.get("carets") or []
        start = t.find(needle)
        if start < 0 or len(carets) < start + len(needle) + 1:
            continue
        c0, c1 = carets[start], carets[start + len(needle)]
        if abs(c0[1] - c1[1]) > 0.5:  # wrapped: measure the first line up to its end
            continue
        box = (c0[0], c0[1], c1[0], c0[1] + c0[3])
        if not inside(box, scene.clip(it)):
            continue
        pts = box_pixels(*box)
        out.append(Sample(pts, pts, box, needle))
    return out


def qtext(scene, exact):
    out = []
    for it in scene.find("QQuickText", lambda i: (i.get("text") or "") == exact):
        x, y, w, h = it["r"]
        box = (x, y, x + w, y + h)
        if not inside(box, scene.clip(it)):
            continue
        pts = box_pixels(*box)
        out.append(Sample(pts, pts, box, exact))
    return out


def ring(x0, y0, w, h, inset, skip):
    """Pixels of the rectangle outline `inset` px inside (negative: outside), sides only (corners skipped)."""
    xs = range(x0 + skip, x0 + w - skip)
    ys = range(y0 + skip, y0 + h - skip)
    left, right = x0 + inset, x0 + w - 1 - inset
    top, bottom = y0 + inset, y0 + h - 1 - inset
    return ([(left, y) for y in ys] + [(right, y) for y in ys]
            + [(x, top) for x in xs] + [(x, bottom) for x in xs])


def rect_of(it):
    x, y, w, h = it["r"]
    return int(round(x)), int(round(y)), int(round(w)), int(round(h))


def checkbox_border(scene):
    out = []
    for cb in scene.find("TaskCheckbox"):
        for r in scene.descendants(cb):
            if scene.base(r) == "QQuickRectangle" and r.get("bwidth", 0) > 0 and r["color"].startswith("#00"):
                x, y, w, h = rect_of(r)
                box = (x - 3, y - 3, x + w + 3, y + h + 3)
                if inside(box, scene.clip(r)):
                    out.append(Sample(ring(x, y, w, h, 0, 5), ring(x, y, w, h, -3, 2), box, "unchecked box"))
    return out


def checkbox_fill(scene):
    out = []
    for cb in scene.find("TaskCheckbox"):
        for r in scene.descendants(cb):
            if scene.base(r) == "QQuickRectangle" and r.get("bwidth", 0) == 0 and r["color"].startswith("#ff"):
                x, y, w, h = rect_of(r)
                box = (x - 3, y - 3, x + w + 3, y + h + 3)
                if inside(box, scene.clip(r)):
                    fill = [(x + 1, yy) for yy in range(y + 5, y + h - 5)] + [(x + 2, yy) for yy in range(y + 5, y + h - 5)]
                    out.append(Sample(fill, ring(x, y, w, h, -3, 2), box, "checked box"))
    return out


def check_mark(scene):
    out = []
    for cb in scene.find("TaskCheckbox"):
        for r in scene.descendants(cb):
            if scene.base(r) == "QQuickRectangle" and r.get("bwidth", 0) == 0 and r["color"].startswith("#ff"):
                x, y, w, h = rect_of(r)
                box = (x, y, x + w, y + h)
                if inside(box, scene.clip(r)):
                    glyph = box_pixels(x + 3, y + 3, x + w - 3, y + h - 3)
                    fill = [(x + 1, yy) for yy in range(y + 5, y + h - 5)] + [(x + 2, yy) for yy in range(y + 5, y + h - 5)]
                    out.append(Sample(glyph, fill, box, "check mark"))
    return out


def vline(scene, it, width=None, off=3):
    x, y, w, h = rect_of(it)
    w = width or max(1, w)
    if h < 8:
        return None
    fg = [(xx, yy) for xx in range(x, x + w) for yy in range(y + 2, y + h - 2)]
    bg = [(x - off, yy) for yy in range(y + 2, y + h - 2)] + [(x + w - 1 + off, yy) for yy in range(y + 2, y + h - 2)]
    box = (x - off, y, x + w + off, y + h)
    return Sample(fg, bg, box, "%s@%d,%d" % (scene.base(it), x, y)) if inside(box, scene.clip(it)) else None


def hline(scene, it, off=3):
    x, y, w, h = rect_of(it)
    h = max(1, h)
    if w < 8:
        return None
    fg = [(xx, yy) for yy in range(y, y + h) for xx in range(x + 2, x + w - 2)]
    bg = [(xx, y - off) for xx in range(x + 2, x + w - 2)] + [(xx, y + h - 1 + off) for xx in range(x + 2, x + w - 2)]
    box = (x, y - off, x + w, y + h + off)
    return Sample(fg, bg, box, "%s@%d,%d" % (scene.base(it), x, y)) if inside(box, scene.clip(it)) else None


def guides(scene):
    return [s for s in (vline(scene, g) for g in scene.find("QQuickRectangle", lambda i: i["obj"] == "guide")) if s]


def rule(scene):
    out = []
    for rb in scene.find("RuleBlock"):
        for r in scene.descendants(rb):
            if scene.base(r) == "QQuickRectangle":
                s = hline(scene, r, off=4)
                if s:
                    out.append(s)
    return out


def table_border(scene):
    out = []
    for tb in scene.find("TableBlock"):
        for r in scene.descendants(tb):
            if scene.base(r) != "QQuickRectangle" or not scene.visible(r):
                continue
            x, y, w, h = r["r"]
            s = vline(scene, r) if w <= 1.5 and h > 8 else (hline(scene, r) if h <= 1.5 and w > 8 else None)
            if s:
                out.append(s)
    return out


def quote_bar(scene):
    out = []
    for bq in scene.find("BlockquoteBlock"):
        for r in scene.descendants(bq):
            if scene.base(r) == "QQuickRectangle" and r["r"][2] <= 3 and r["r"][3] > 8:
                s = vline(scene, r, off=3)
                if s:
                    out.append(s)
    return out


def bullets(scene):
    out = []
    for li in scene.find("ListItemBlock"):
        for r in scene.descendants(li):
            x, y, w, h = r["r"]
            if scene.base(r) == "QQuickRectangle" and 3 <= w <= 8 and abs(w - h) < 0.5 and scene.visible(r):
                xi, yi, wi, hi = rect_of(r)
                box = (xi - 3, yi - 3, xi + wi + 3, yi + hi + 3)
                if inside(box, scene.clip(r)):
                    core = box_pixels(xi + 1, yi + 1, xi + wi - 1, yi + hi - 1)
                    out.append(Sample(core, ring(xi, yi, wi, hi, -3, 0), box, "bullet"))
    return out


def icon_boxes(scene, base, pred=lambda it: True):
    out = []
    for ic in scene.find(base, pred):
        x, y, w, h = rect_of(ic)
        box = (x, y, x + w, y + h)
        if w > 4 and inside(box, scene.clip(ic)):
            pts = box_pixels(*box)
            out.append(Sample(pts, pts, box, "%s %s" % (base, ic.get("source") or ic.get("label") or "")))
    return out


def toolbar_icons(scene):
    return icon_boxes(scene, "Icon", lambda it: scene.has_ancestor(it, "ToolButton")
                      and not scene.has_ancestor(it, "NoteView") and it["r"][2] >= 16)


def callout_icons(scene):
    return icon_boxes(scene, "LucideIcon", lambda it: scene.has_ancestor(it, "CalloutBlock"))


def footer_rule(scene):
    out = []
    for r in scene.find("QQuickRectangle", lambda i: i["r"][3] == 1 and i["r"][2] > 200
                        and not scene.has_ancestor(i, "NoteView")):
        s = hline(scene, r, off=3)
        if s:
            out.append(s)
    return out


def inline_title(scene):
    out = []
    for nv in scene.find("NoteView"):
        for it in scene.descendants(nv):
            if it["cls"].startswith("QQuickTextEdit") and scene.visible(it) and it.get("text"):
                out += text_span(scene, it["text"])
                return out
    return out


def file_label(scene):
    out = []
    for it in scene.find(None, lambda i: (i.get("text") or "").endswith(".md")
                         and not i["cls"].startswith(("QQuickTextEdit", "TextArea"))):
        x, y, w, h = it["r"]
        box = (x, y, x + w, y + h)
        if inside(box, scene.clip(it)):
            pts = box_pixels(*box)
            out.append(Sample(pts, pts, box, it["text"]))
    return out


def editor_text(scene):
    for needle in ["Quoteword blockquote line", "Bodyword paragraph with", "Afterrule paragraph.",
                   "Dangerbody text", "Cellbody", "Opentask unchecked"]:
        s = text_span(scene, needle, cls_prefixes=("TextArea",))
        if s:
            return s
    return []


def spans(*needles):
    return lambda scene: [s for n in needles for s in text_span(scene, n)]


def qtext_prefix(prefix, within=None):
    def locate(scene):
        out = []
        for it in scene.find(None, lambda i: (i.get("text") or "").startswith(prefix)
                             and not i["cls"].startswith(("QQuickTextEdit", "TextArea"))):
            if within and not scene.has_ancestor(it, within):
                continue
            x, y, w, h = it["r"]
            box = (x, y, x + w, y + h)
            if w > 0 and h > 0 and inside(box, scene.clip(it)):
                pts = box_pixels(*box)
                out.append(Sample(pts, pts, box, prefix))
        return out
    return locate


def selection(scene):
    """The selected run in the editor: its first line is what edit.sh selected (Home, Shift+End)."""
    return text_span(scene, "Bodyword paragraph with", cls_prefixes=("TextArea",))


VIEW, EDIT, EDITSEL, MISSING, BANNER = "view", "edit", "editsel", "missing", "banner"
CHROME = (VIEW, EDIT, EDITSEL, MISSING, BANNER)

# (key, label, threshold, locator, render kinds it is measured in)
ELEMENTS = [
    ("body", "body text", TEXT, spans("Bodyword paragraph with"), (VIEW,)),
    ("bold", "bold / italic", TEXT, spans("Boldword", "Italicword"), (VIEW,)),
    ("muted", "muted (checked task)", TEXT, spans("Donetask checked"), (VIEW,)),
    ("link", "link", TEXT, spans("Linkword"), (VIEW,)),
    ("wikilink", "wikilink", TEXT, spans("Wikiword"), (VIEW,)),
    ("tag", "tag text on pill", TEXT, spans("#tagword"), (VIEW,)),
    ("code", "inline code on bg", TEXT, spans("codeword"), (VIEW,)),
    ("codeblock", "code block text", TEXT, lambda s: qtext(s, "blockcode = 42"), (VIEW,)),
    ("callout-note", "callout title: note", TEXT, spans("Notetitle"), (VIEW,)),
    ("callout-tip", "callout title: tip", TEXT, spans("Tiptitle"), (VIEW,)),
    ("callout-warning", "callout title: warning", TEXT, spans("Warntitle"), (VIEW,)),
    ("callout-danger", "callout title: danger", TEXT, spans("Dangertitle"), (VIEW,)),
    ("callout-body", "callout body on tint", TEXT, spans("Notebody text", "Tipbody text", "Warnbody text", "Dangerbody text"), (VIEW,)),
    ("mark", "highlight text on mark", TEXT, spans("Markword"), (VIEW,)),
    ("checkbox", "checkbox border", UI, checkbox_border, (VIEW,)),
    ("guide", "indentation guide", UI, guides, (VIEW,)),
    ("hr", "hr", UI, rule, (VIEW,)),
    ("table", "table border", UI, table_border, (VIEW,)),
    # beyond the required list
    ("title", "inline title", TEXT, inline_title, (VIEW,)),
    ("propkey", "property key", TEXT, lambda s: qtext(s, "status"), (VIEW,)),
    ("propval", "property value pill", TEXT, lambda s: qtext(s, "Propvalue"), (VIEW,)),
    ("olnum", "ordered-list number", TEXT, lambda s: qtext(s, "1."), (VIEW,)),
    ("checkfill", "checkbox fill (checked)", UI, checkbox_fill, (VIEW,)),
    ("checkmark", "check mark on fill", UI, check_mark, (VIEW,)),
    ("quotebar", "blockquote bar", UI, quote_bar, (VIEW,)),
    ("bullet", "bullet dot", UI, bullets, (VIEW,)),
    ("callout-icon", "callout icons", UI, callout_icons, (VIEW,)),
    ("filename", "footer file name", TEXT, file_label, CHROME),
    ("icons", "toolbar icons", UI, toolbar_icons, CHROME),
    ("footer-rule", "footer separator", UI, footer_rule, CHROME),
    ("editor", "editor text", TEXT, editor_text, (EDIT,)),
    ("editor-sel", "editor selected text", TEXT, selection, (EDITSEL,)),
    ("ph-title", "placeholder title", TEXT, qtext_prefix("No Markdown file selected"), (MISSING,)),
    ("ph-text", "placeholder explanation", TEXT, qtext_prefix("Pick a note from your Obsidian vault"), (MISSING,)),
    ("ph-button", "placeholder button label", TEXT, qtext_prefix("Choose Markdown File"), (MISSING,)),
    ("banner", "banner text", TEXT, lambda s: [x for x in text_span(s, "This file contains bytes")], (BANNER,)),
]


def render_kind(name, colour):
    """ours-<colour>-<variant>-<shot>  ->  a render kind, or None when not measured."""
    variant, shot = name[len("ours-%s-" % colour):].rsplit("-", 1)
    if variant == "dark620" and colour.startswith("translucent"):
        return None  # no paper: the other scheme's window is not the background this colour is meant for
    if variant in ("620", "tall", "dark620") and shot.startswith("view"):
        return VIEW
    if variant == "edit":
        return {"edit": EDIT, "editsel": EDITSEL}.get(shot)
    if variant == "missing":
        return MISSING
    if variant == "banner":
        return BANNER
    return None


def measure(shot, sample):
    pts = [(x, y) for x, y in sample.bg_pts if 0 <= x < shot.w and 0 <= y < shot.h]
    fpts = [(x, y) for x, y in sample.fg_pts if 0 <= x < shot.w and 0 <= y < shot.h]
    if not pts or not fpts:
        return None
    bg = mode_colour(shot, pts)
    fg = extreme_colour(shot, fpts, bg)
    return ratio(fg, bg), fg, bg


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", default="/tmp/colors/r")
    ap.add_argument("--colors", default=",".join(COLOURS))
    ap.add_argument("--json")
    args = ap.parse_args()
    colours = [c for c in args.colors.split(",") if c]

    results = {}  # colour -> key -> (ratio, fg, bg, render, where)
    missing = []
    for colour in colours:
        # ours-<colour>-<variant>-<shot>.json. The variant is matched exactly: a bare
        # "ours-translucent-*" glob would also take translucent-light's renders.
        dumps = sorted(d for d in glob.glob(os.path.join(args.dir, "ours-%s-*.json" % colour))
                       if re.fullmatch(r"ours-%s-(%s)-[a-z0-9]+\.json" % (re.escape(colour), VARIANTS),
                                       os.path.basename(d)))
        # skip helper dumps with no screenshot
        pairs = [(d, d[:-5] + ".png") for d in dumps if os.path.exists(d[:-5] + ".png")]
        if not pairs:
            missing.append((colour, "no renders"))
            continue
        res = results.setdefault(colour, {})
        for dump, png in pairs:
            kind = render_kind(os.path.basename(dump)[:-5], colour)
            if kind is None:
                continue
            scene, shot = Scene(dump), Shot(png)
            for key, label, thr, locate, kinds in ELEMENTS:
                if kind not in kinds:
                    continue
                for s in locate(scene):
                    m = measure(shot, s)
                    if m is None:
                        continue
                    r, fg, bg = m
                    cur = res.get(key)
                    if cur is None or r < cur[0]:
                        res[key] = (r, fg, bg, os.path.basename(png), s.where)
        for key, label, thr, locate, kinds in ELEMENTS:
            if key not in res:
                missing.append((colour, key))

    # ---- table: element x colour
    short = {"white": "white", "black": "black", "red": "red", "orange": "orange", "yellow": "yellow",
             "green": "green", "blue": "blue", "pink": "pink", "translucent": "transl", "translucent-light": "transl-lt"}
    head = "%-26s %4s " % ("element", "min") + " ".join("%9s" % short.get(c, c)[:9] for c in colours)
    print(head)
    print("-" * len(head))
    fails = []
    for key, label, thr, locate, _ in ELEMENTS:
        cells = []
        for c in colours:
            v = results.get(c, {}).get(key)
            if v is None:
                cells.append("%9s" % "--")
                continue
            bad = v[0] < thr
            cells.append("%8.2f%s" % (v[0], "!" if bad else " "))
            if bad:
                fails.append((c, key, label, thr, v))
        print("%-26s %4.1f " % (label[:26], thr) + " ".join(cells))
    print()
    print("fg / bg actually measured (hex) per colour:")
    for c in colours:
        row = results.get(c, {})
        print("  %-17s " % c + "  ".join("%s %s/%s" % (k, hexc(v[1]), hexc(v[2])) for k, v in row.items()))
    print()
    for c, key, label, thr, v in fails:
        print("FAIL %-17s %-24s %.2f < %.1f  fg %s on bg %s  (%s, %s)" % (c, label, v[0], thr, hexc(v[1]), hexc(v[2]), v[3], v[4]))
    for c, key in missing:
        print("MISSING %-17s %s" % (c, key))
    if args.json:
        json.dump({c: {k: {"ratio": round(v[0], 3), "fg": hexc(v[1]), "bg": hexc(v[2]), "render": v[3], "where": v[4]}
                       for k, v in row.items()} for c, row in results.items()}, open(args.json, "w"), indent=1)
    print("\n%d measurements, %d FAIL, %d missing" % (sum(len(r) for r in results.values()), len(fails), len(missing)))
    return 1 if fails else (2 if missing else 0)


if __name__ == "__main__":
    sys.exit(main())
