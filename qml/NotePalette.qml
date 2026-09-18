/*
    SPDX-FileCopyrightText: 2014 David Edmundson <davidedmundson@kde.org>
    SPDX-FileCopyrightText: 2014, 2015 Kai Uwe Broulik <kde@privat.broulik.de>
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors

    The paper colour names and textIconColor rule are those of the Plasma
    "notes" applet (kdeplasma-addons, applets/notes).

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import org.kde.kirigami as Kirigami

/**
 * THE single source of every colour the rendered note, the raw editor and the
 * in-paper chrome use. main.qml owns one instance and hands it to NoteView and
 * NoteEditor; NoteView feeds ObsidianMetrics' colour properties from it, which
 * is how every block component (and the %LINK% / %CODEFG% / %TAGFG%
 * placeholders of the C++ inline HTML) receives them.
 *
 * Why not Kirigami.Theme: the note is drawn over a KNOWN paper colour (the
 * widgets/notes SVG element), and "an unknown colour over a known colour is a
 * bad move as you end up with white on yellow" (upstream). Only when there is
 * no paper (translucent in a panel: noBackground) are theme colours used.
 *
 * Paper colour source: the Breeze widgets/notes.svgz table below (top, middle
 * and bottom of each element's vertical gradient, rendered with rsvg-convert),
 * overridden at run time by paperSample when main.qml managed to sample the
 * element of the ACTIVE Plasma theme. Every derived colour is then pushed
 * toward black or white until it clears its WCAG contrast floor against every
 * background it can sit on (all three gradient stops and the callout tints):
 *   text, mutedText, link, tagText, codeText, callout titles  >= 4.5 : 1
 *   border (hr, tables, indent guides), accent (checkbox fill,
 *   blockquote bar), checkboxCheck against accent             >= 3   : 1
 *
 * Translucent papers draw no opaque paper (Breeze has no "translucent-notes"
 * element at all), so the background is whatever is behind the widget; the
 * table assumes a light desktop for "translucent" and a dark one for
 * "translucent-light", matching the text colour upstream picks for each.
 */
QtObject {
    id: p

    /** Plasmoid.configuration.color: white, black, red, orange, yellow, green, blue, pink, translucent, translucent-light. */
    property string color: "yellow"
    /** In a panel with a translucent colour there is no paper: use theme colours. */
    property bool noBackground: false
    /** root.textIconColor (upstream rule); the start point for body text. */
    property color textIconColor: p.noBackground
        ? Kirigami.Theme.textColor
        : (p.color === "black" || p.color === "translucent-light" ? "#dfdfdf" : "#202020")
    /**
     * [top, middle, bottom] colours sampled from the active theme's paper
     * element, or null to use the Breeze table.
     */
    property var paperSample: null

    // ---- Breeze widgets/notes.svgz, rendered at 400x400: rows 30 / mean of the centre 300x300 / 370 ----
    readonly property var breezePaper: ({
        "white": ["#f1f2f5", "#edeff3", "#e8ebf0"],
        "black": ["#212428", "#1f2225", "#1c1f22"],
        "red": ["#f54253", "#f43a4f", "#f2314b"],
        "orange": ["#e97656", "#e97252", "#e96e4d"],
        "yellow": ["#f9eecc", "#f8ecc6", "#f7eac0"],
        "green": ["#3dd37c", "#36d077", "#2fcc72"],
        "blue": ["#2ea4da", "#28a1d9", "#219dd7"],
        "pink": ["#ff5c7b", "#ff5676", "#ff4f71"],
        "translucent": ["#d8d8d8", "#d8d8d8", "#d8d8d8"],
        "translucent-light": ["#303030", "#303030", "#303030"]
    })

    /** Every background body content can sit on, before callout tints. */
    readonly property var paperStops: {
        if (p.noBackground) {
            return [Qt.color(Kirigami.Theme.backgroundColor)];
        }
        const s = p.paperSample;
        if (s && s.length === 3) {
            return [Qt.color(s[0]), Qt.color(s[1]), Qt.color(s[2])];
        }
        const t = p.breezePaper[p.color] || p.breezePaper["yellow"];
        return [Qt.color(t[0]), Qt.color(t[1]), Qt.color(t[2])];
    }

    /** The representative (middle) paper colour. */
    readonly property color background: p.paperStops[p.paperStops.length > 1 ? 1 : 0]
    readonly property bool lightText: p.luminance(p.textIconColor) > p.luminance(p.background)

    // ---- body colours against the bare paper (inputs to the callout tint rule) ----
    readonly property color textOnPaper: p.ensure(p.textIconColor, p.paperStops, 4.5)
    readonly property color mutedOnPaper: p.ensure(p.noBackground ? Kirigami.Theme.disabledTextColor
        : p.mix(p.textIconColor, p.background, 0.4), p.paperStops, 4.5)
    readonly property color linkOnPaper: p.ensure(p.noBackground ? Kirigami.Theme.linkColor
        : (p.lightText ? "#8ab4f8" : "#1d4ed8"), p.paperStops, 4.5)

    // ---- callout families (title text + icon colour; CalloutBlock tints its box with calloutTint()) ----
    readonly property color accent: p.typeColor(p.noBackground ? Kirigami.Theme.highlightColor
        : (p.lightText ? "#a88bfa" : "#5b3fc4"))
    readonly property color positive: p.typeColor(p.noBackground ? Kirigami.Theme.positiveTextColor
        : (p.lightText ? "#6fdc8c" : "#17692f"))
    readonly property color neutral: p.typeColor(p.noBackground ? Kirigami.Theme.neutralTextColor
        : (p.lightText ? "#f5b94a" : "#7a4a00"))
    readonly property color negative: p.typeColor(p.noBackground ? Kirigami.Theme.negativeTextColor
        : (p.lightText ? "#ff8a80" : "#9f1d14"))

    /** Every background any text can sit on: the paper stops plus each callout tint over them. */
    readonly property var allBackgrounds: {
        const out = p.paperStops.slice();
        const types = [p.accent, p.positive, p.neutral, p.negative, p.mutedOnPaper];
        for (let j = 0; j < types.length; ++j) {
            const hue = p.hueTints(types[j]);
            // A hue tint is kept whenever SOME text colour can still clear 4.5 : 1 on it.
            const src = p.reachable(p.paperStops.concat(hue), 4.5) ? types[j] : p.liftColor;
            for (let i = 0; i < p.paperStops.length; ++i) {
                out.push(p.mix(p.paperStops[i], src, p.calloutTintAlpha));
            }
        }
        return out;
    }

    // ---- text ----
    readonly property color text: p.ensure(p.textOnPaper, p.allBackgrounds, 4.5)
    /** Checked tasks, list markers, callout quote/example titles, checkbox border. */
    readonly property color mutedText: p.ensure(p.mutedOnPaper, p.allBackgrounds, 4.5)
    readonly property color link: p.ensure(p.linkOnPaper, p.allBackgrounds, 4.5)
    /** Obsidian draws [[wikilinks]] and [links]() alike; the C++ HTML has one %LINK% placeholder. */
    readonly property color wikilink: p.link

    // ---- callout box ----
    /** Obsidian: rgba(var(--callout-color), 0.1). */
    readonly property real calloutTintAlpha: 0.1

    /** What a callout box is tinted with when its own colour cannot be: away from the text. */
    readonly property color liftColor: p.lightText ? "#000000" : "#ffffff"

    /**
     * The colour CalloutBlock fills its box with for a callout drawn in
     * @p typeColor: that colour at 10 % (Obsidian) -- unless that tint would
     * pull the body text, muted text, links or the title itself under 4.5 : 1
     * somewhere on the paper (dark text on red paper: every legal title colour
     * is near-black and darkens the box past what even pure black text can
     * clear), in which case the box is lifted AWAY from the text instead
     * (10 % white under dark text, 10 % black under light text).
     */
    function calloutTint(typeColor: color): color {
        const c = Qt.color(typeColor);
        const fgs = [p.text, p.mutedText, p.link, c];
        const hue = p.hueTints(c);
        const src = p.worstOf(fgs, hue) >= 4.5 ? c : Qt.color(p.liftColor);
        return Qt.rgba(src.r, src.g, src.b, p.calloutTintAlpha);
    }
    /** @p c at calloutTintAlpha over every paper stop. */
    function hueTints(c: color): var {
        const out = [];
        for (let i = 0; i < p.paperStops.length; ++i) {
            out.push(p.mix(p.paperStops[i], c, p.calloutTintAlpha));
        }
        return out;
    }
    function worstOf(fgs: var, bgs: var): real {
        let w = 21;
        for (let i = 0; i < fgs.length; ++i) {
            w = Math.min(w, p.worstContrast(fgs[i], bgs));
        }
        return w;
    }
    /** Can pure black or pure white reach @p ratio against all of @p bgs? */
    function reachable(bgs: var, ratio: real): bool {
        return Math.max(p.worstContrast(Qt.color("#000000"), bgs), p.worstContrast(Qt.color("#ffffff"), bgs)) >= ratio;
    }
    /**
     * A callout title colour: @p base pushed to 4.5 : 1 against the paper and,
     * when that is reachable, against its own tint too.
     */
    function typeColor(base: color): color {
        let c = p.ensure(base, p.paperStops, 4.5);
        for (let pass = 0; pass < 4; ++pass) {
            const bgs = p.paperStops.concat(p.hueTints(c));
            if (!p.reachable(bgs, 4.5) || p.worstContrast(c, bgs) >= 4.5) {
                return c;
            }
            c = p.ensure(c, bgs, 4.5);
        }
        return c;
    }

    // ---- lines ----
    /** hr, table grid, indent guides. */
    readonly property color border: p.ensure(p.mix(p.text, p.background, 0.55), p.allBackgrounds, 3)

    // ---- code ----
    readonly property color codeBackground: p.mix(p.background, p.text, p.lightText ? 0.1 : 0.08)
    readonly property color inlineCodeBackground: p.codeBackground
    readonly property color codeText: p.ensure(p.text, [p.codeBackground], 4.5)

    // ---- tags ----
    readonly property color tagBackground: p.mix(p.background, p.link, 0.14)
    readonly property color tagText: p.ensure(p.link, [p.tagBackground], 4.5)

    // ---- ==highlight== ----
    /** The mark background, pulled back toward the paper until text, muted text and links on it clear 4.5 : 1. */
    readonly property color highlightBackground: {
        const hue = p.noBackground
            ? Qt.color(Kirigami.Theme.neutralBackgroundColor)
            : Qt.color(p.lightText ? "#8a6d00" : (p.color === "yellow" || p.color === "orange" ? "#ffb340" : "#ffe55c"));
        const fgs = [p.highlightText, p.mutedText, p.link];
        for (let t = 0.6; t > 0; t -= 0.05) {
            const c = p.mix(p.background, hue, t);
            let ok = true;
            for (let i = 0; i < fgs.length; ++i) {
                ok = ok && p.contrast(fgs[i], c) >= 4.5;
            }
            if (ok) {
                return c;
            }
        }
        return p.background;
    }
    /** InlineText keeps the run's own colour over a mark (as Obsidian does); this is that colour for body text. */
    readonly property color highlightText: p.text

    // ---- checkbox ----
    readonly property color checkboxBorder: p.mutedText
    readonly property color checkboxFill: p.accent
    readonly property color checkboxCheck: {
        const cands = [p.background, Qt.color("#ffffff"), Qt.color("#000000")];
        if (p.contrast(cands[0], p.accent) >= 3) {
            return cands[0];
        }
        return p.contrast(cands[1], p.accent) >= p.contrast(cands[2], p.accent) ? cands[1] : cands[2];
    }

    // ---- editor selection ----
    readonly property color selection: p.noBackground ? Kirigami.Theme.highlightColor : p.mix(p.background, p.link, 0.3)
    readonly property color selectedText: p.ensure(p.noBackground ? Kirigami.Theme.highlightedTextColor : p.text, [p.selection], 4.5)

    // ---- colour maths (WCAG 2.x) ----
    function channel(v: real): real {
        return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
    }
    function luminance(c: color): real {
        return 0.2126 * p.channel(c.r) + 0.7152 * p.channel(c.g) + 0.0722 * p.channel(c.b);
    }
    function contrast(a: color, b: color): real {
        const la = p.luminance(a);
        const lb = p.luminance(b);
        return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05);
    }
    /** Opaque linear mix a -> b by t. */
    function mix(a: color, b: color, t: real): color {
        return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1);
    }
    function worstContrast(fg: color, bgs: var): real {
        let w = 21;
        for (let i = 0; i < bgs.length; ++i) {
            w = Math.min(w, p.contrast(fg, bgs[i]));
        }
        return w;
    }
    /**
     * @p fg, or the smallest step of it toward black or white (the side it
     * already sits on first) that reaches @p ratio against every colour in @p bgs.
     */
    function ensure(fg: color, bgs: var, ratio: real): color {
        const f = Qt.color(fg);
        if (p.worstContrast(f, bgs) >= ratio) {
            return Qt.rgba(f.r, f.g, f.b, 1);
        }
        let bgLum = 0;
        for (let i = 0; i < bgs.length; ++i) {
            bgLum += p.luminance(bgs[i]) / bgs.length;
        }
        const black = Qt.color("#000000");
        const white = Qt.color("#ffffff");
        const order = p.luminance(f) <= bgLum ? [black, white] : [white, black];
        for (let step = 1; step <= 20; ++step) {
            for (let k = 0; k < 2; ++k) {
                const c = p.mix(f, order[k], step / 20);
                if (p.worstContrast(c, bgs) >= ratio) {
                    return c;
                }
            }
        }
        return p.worstContrast(black, bgs) >= p.worstContrast(white, bgs) ? black : white;
    }
}
