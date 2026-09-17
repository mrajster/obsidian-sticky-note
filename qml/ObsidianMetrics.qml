/*
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick

/**
 * THE single source of every layout number the rendered view uses.
 *
 * Every value is Obsidian 1.13.7's reading-view CSS, expressed in em of the
 * base font so it scales with the configured size (16 px body = 12 pt). No
 * block component may hard-code a pixel value: if a number is not in here, it
 * does not exist.
 *
 * Colours are NOT metrics. They are plain Plasma theme colours, assigned by
 * NoteView from Kirigami.Theme, and live here only so there is one object to
 * hand down the tree.
 */
QtObject {
    id: m

    required property real basePointSize
    required property string textFamily

    // ---- base ----------------------------------------------------------------
    readonly property real em: m.basePointSize * 4 / 3
    function pt(px: real): real { return px * 0.75; }
    readonly property real lhBody: 1.5 * m.em

    /** Chromium lays out in 1/64 px units and rounds DOWN. */
    function snap(px: real): real { return Math.floor(px * 64) / 64; }

    /**
     * CSS (LayoutNG) baseline position inside a line box of height @p lh:
     * half-leading split with rounded ascent/descent, floored.
     */
    function baselineInBox(fm: FontMetrics, lh: real): real {
        return m.baselineInBoxAt(fm, lh, 1);
    }

    /**
     * baselineInBox() for a run drawn at @p scale times the whole pixel size
     * @p fm was resolved at (Qt rounds font sizes; InlineText scales the rest
     * back in). CSS rounds the EXACT size's ascent and descent, so scale first.
     */
    function baselineInBoxAt(fm: FontMetrics, lh: real, scale: real): real {
        const a = Math.round(fm.ascent * scale);
        const d = Math.round(fm.descent * scale);
        return a + Math.floor((lh - a - d) / 2);
    }

    readonly property string monoFamily: Qt.fontFamilies().indexOf("DejaVu Sans Mono") >= 0
        ? "DejaVu Sans Mono" : "monospace"

    // ---- container / spacing -------------------------------------------------
    readonly property real containerPadding: 2 * m.em
    readonly property real pSpacing: m.em
    readonly property real headingSpacing: 2.5 * m.em
    /**
     * CSS font matching, not Qt's. For a requested weight above 500 CSS takes
     * the lightest face AT OR ABOVE it; Qt takes the nearest and breaks ties
     * downwards. With no SemiBold face installed (the common Noto Sans install)
     * Obsidian therefore draws weight 600 with Bold while Qt would pick Medium,
     * which is ~3% narrower and drifts every later glyph on the line.
     */
    readonly property int weight600: (m.probe500.advanceWidth === m.probe600.advanceWidth
                                      && m.probe700.advanceWidth !== m.probe500.advanceWidth) ? 700 : 600
    function cssWeight(w: int): int { return w === 600 ? m.weight600 : w; }
    readonly property TextMetrics probe500: TextMetrics { font.family: m.textFamily; font.pixelSize: 64; font.weight: 500; text: "Obsidian 0123 bold" }
    readonly property TextMetrics probe600: TextMetrics { font.family: m.textFamily; font.pixelSize: 64; font.weight: 600; text: "Obsidian 0123 bold" }
    readonly property TextMetrics probe700: TextMetrics { font.family: m.textFamily; font.pixelSize: 64; font.weight: 700; text: "Obsidian 0123 bold" }

    readonly property int boldWeight: m.cssWeight(600)

    function gapPx(token: string): real {
        switch (token) {
        case "p": return m.em;
        case "heading": return 2.5 * m.em;
        case "hr": return 2 * m.em;
        case "table": return 2 * m.em;
        case "table-hr": return 3 * m.em;
        case "table-heading": return 3.5 * m.em;
        case "properties": return 2 * m.em;
        default: return 0; // "none", missing, unknown
        }
    }

    // ---- headings --------------------------------------------------------------
    readonly property var headingScale: [1.618, 1.462, 1.318, 1.188, 1.076, 1.0]
    readonly property var headingWeightTable: [700, 680, 660, 640, 620, 600]
    readonly property var headingLineHeightTable: [1.2, 1.2, 1.3, 1.4, 1.5, 1.5]
    readonly property var headingLetterSpacingTable: [-0.015, -0.011, -0.008, -0.005, -0.002, 0]

    function clampLevel(level: int): int { return Math.max(1, Math.min(6, level)) - 1; }
    function headingSize(level: int): real { return m.headingScale[m.clampLevel(level)] * m.em; }
    function headingWeight(level: int): int { return m.cssWeight(m.headingWeightTable[m.clampLevel(level)]); }
    function headingLineHeight(level: int): real {
        return m.headingLineHeightTable[m.clampLevel(level)] * m.headingSize(level);
    }
    function headingLetterSpacing(level: int): real {
        return m.headingLetterSpacingTable[m.clampLevel(level)] * m.headingSize(level);
    }
    function headingMetrics(level: int): FontMetrics {
        return [m.fmH1, m.fmH2, m.fmH3, m.fmH4, m.fmH5, m.fmH6][m.clampLevel(level)];
    }

    // ---- inline title ----------------------------------------------------------
    readonly property real inlineTitleSize: m.headingSize(1)
    readonly property real inlineTitleLineHeight: 1.2 * m.inlineTitleSize
    readonly property real inlineTitleMarginBottom: 0.5 * m.inlineTitleSize

    // ---- lists -----------------------------------------------------------------
    // Advances come from TextMetrics (a notifying property), not from
    // FontMetrics.advanceWidth(): a plain call is never re-evaluated when the
    // font arrives, and the compiled bindings would keep the default font's value.
    readonly property real indentTop: 3 * m.zeroAdvance.advanceWidth
    readonly property real indentStep: 2.25 * m.em
    function listTextX(d: int): real { return d <= 0 ? 0 : m.indentTop + (d - 1) * m.indentStep; }
    readonly property real listItemPad: m.snap(0.075 * m.em)

    readonly property real bulletSize: 0.3 * m.em
    readonly property real bulletCenterOffset: 0.8 * m.em
    readonly property real orderedMarkerGap: m.spaceAdvance.advanceWidth

    readonly property real guideWidth: 1
    readonly property real guideOffset: -0.85 * m.em

    // ---- checkbox ----------------------------------------------------------------
    readonly property real checkboxSize: m.em
    readonly property real checkboxBorder: 1
    readonly property real checkboxRadius: 0.25 * m.em
    readonly property real checkboxHang: 1.5 * m.em
    readonly property real checkboxGap: 0.5 * m.em
    readonly property real checkboxTopInLine: 0.29 * m.em

    // ---- blockquote / callout ----------------------------------------------------
    readonly property real blockquoteBorder: 2
    readonly property real blockquotePadding: 1.5 * m.em

    readonly property real calloutPadTop: 0.75 * m.em
    readonly property real calloutPadRight: 0.75 * m.em
    readonly property real calloutPadBottom: 0.75 * m.em
    readonly property real calloutPadLeft: 1.5 * m.em
    readonly property real calloutRadius: 0.25 * m.em
    readonly property real calloutIconSize: 1.125 * m.em
    readonly property real calloutTitleGap: 0.25 * m.em
    readonly property int calloutTitleWeight: m.cssWeight(600)
    readonly property real calloutTitleLineHeight: 1.3 * m.em

    // ---- code ----------------------------------------------------------------------
    readonly property real inlineCodeSize: 0.875 * m.em
    readonly property real inlineCodePadX: 0.3 * m.inlineCodeSize
    readonly property real inlineCodePadY: 0.15 * m.inlineCodeSize
    readonly property real inlineCodeRadius: 0.25 * m.em
    readonly property real codeBlockPadY: 0.75 * m.em
    readonly property real codeBlockPadX: m.em
    readonly property real codeBlockMinHeight: 2.375 * m.em
    readonly property real codeBlockRadius: 0.25 * m.em
    readonly property real codeFontSize: 0.875 * m.em
    readonly property real codeLinePitch: 1.5 * m.em

    // ---- table -----------------------------------------------------------------------
    readonly property real tableCellPadY: 0.25 * m.em
    readonly property real tableCellPadX: 0.5 * m.em
    readonly property real tableBorder: 1
    readonly property real tableLineHeight: 1.3 * m.em
    readonly property int tableHeaderWeight: m.cssWeight(600)
    readonly property real tableColumnMinWidth: 6 * m.zeroAdvance.advanceWidth

    // ---- hr / tag ----------------------------------------------------------------------
    readonly property real hrThickness: 2
    readonly property real hrMargin: 2 * m.em

    readonly property real tagSize: 0.875 * m.em
    readonly property real tagPadY: 0.25 * m.tagSize
    readonly property real tagPadX: 0.65 * m.tagSize
    readonly property real tagRadius: 2 * m.tagSize
    readonly property real tagLineHeight: m.tagSize

    // ---- properties (app.css variables; NOT yet measured against a render) ----------------
    readonly property real propertiesPadY: 0.5 * m.em
    readonly property real propertiesHeadingLineHeight: 1.2 * m.em
    readonly property real propertiesHeadingPad: 0.25 * m.em
    readonly property real propertiesHeadingMarginBottom: 0.5 * m.em
    readonly property real propertyRowHeight: 1.75 * m.em
    readonly property real propertyLabelWidth: 9 * m.em
    readonly property real propertyFontSize: 0.875 * m.em
    readonly property real propertyIconSize: m.em
    readonly property real propertyIconGap: 0.25 * m.em
    readonly property real propertiesShiftX: -0.25 * m.em

    // ---- font metrics ------------------------------------------------------------------------
    readonly property FontMetrics fmBody: FontMetrics { font.family: m.textFamily; font.pointSize: m.pt(m.em) }
    readonly property TextMetrics zeroAdvance: TextMetrics { font.family: m.textFamily; font.pointSize: m.pt(m.em); text: "0" }
    readonly property TextMetrics spaceAdvance: TextMetrics { font.family: m.textFamily; font.pointSize: m.pt(m.em); text: " " }
    readonly property FontMetrics fmBold: FontMetrics { font.family: m.textFamily; font.pointSize: m.pt(m.em); font.weight: m.boldWeight }
    readonly property FontMetrics fmMono: FontMetrics { font.family: m.monoFamily; font.pointSize: m.pt(m.codeFontSize) }
    readonly property FontMetrics fmTag: FontMetrics { font.family: m.textFamily; font.pointSize: m.pt(m.tagSize) }
    readonly property FontMetrics fmProperty: FontMetrics { font.family: m.textFamily; font.pointSize: m.pt(m.propertyFontSize) }
    readonly property FontMetrics fmH1: FontMetrics { font.family: m.textFamily; font.pointSize: m.pt(m.headingSize(1)); font.weight: 700 }
    readonly property FontMetrics fmH2: FontMetrics { font.family: m.textFamily; font.pointSize: m.pt(m.headingSize(2)); font.weight: 680 }
    readonly property FontMetrics fmH3: FontMetrics { font.family: m.textFamily; font.pointSize: m.pt(m.headingSize(3)); font.weight: 660 }
    readonly property FontMetrics fmH4: FontMetrics { font.family: m.textFamily; font.pointSize: m.pt(m.headingSize(4)); font.weight: 640 }
    readonly property FontMetrics fmH5: FontMetrics { font.family: m.textFamily; font.pointSize: m.pt(m.headingSize(5)); font.weight: 620 }
    readonly property FontMetrics fmH6: FontMetrics { font.family: m.textFamily; font.pointSize: m.pt(m.headingSize(6)); font.weight: m.headingWeight(6) }

    // ---- colours (theme, not metrics; NoteView assigns them) ------------------------------------
    property color textColor: "black"
    property color mutedTextColor: "gray"
    property color linkColor: "blue"
    property color backgroundColor: "white"
    property color codeBackgroundColor: "#f0f0f0"
    property color codeTextColor: "black"
    property color tagTextColor: "blue"
    property color tagBackgroundColor: "#e0e0ff"
    property color markBackgroundColor: "yellow"
    property color borderColor: "#c0c0c0"
    property color accentColor: "blue"
    property color positiveColor: "green"
    property color neutralColor: "orange"
    property color negativeColor: "red"

    /** Replaces every §2.3 placeholder the C++ inline renderer writes. */
    function resolveInline(html: string): string {
        return m.resolveInlineAt(html, 1);
    }

    /**
     * resolveInline() for a TextEdit that is drawn scaled by @p scale
     * (InlineText.fontScale): absolute sizes inside it shrink by the same factor.
     *
     * %MARKBG% resolves to transparent: QTextDocument would paint ==highlight==
     * from Qt's own font ascent, a pixel off Chromium's rounded one, so
     * InlineText paints highlights itself from the block's "marks" ranges.
     */
    function resolveInlineAt(html: string, scale: real): string {
        if (!html) {
            return "";
        }
        return html
            .split("font-weight:600").join("font-weight:" + m.boldWeight)
            // Obsidian tags are pills, not underlined links.
            .split("<a href=\"obsnote:tag/").join("<a style=\"text-decoration:none\" href=\"obsnote:tag/")
            .split("%MONO%").join(m.monoFamily)
            .split("%CODEPT%").join(String(m.pt(m.inlineCodeSize) / scale))
            .split("%TAGPT%").join(String(m.pt(m.tagSize) / scale))
            .split("%CODEPADX%").join(String(m.inlineCodePadX / scale))
            .split("%TAGPADX%").join(String(m.tagPadX / scale))
            .split("%LINK%").join(String(m.linkColor))
            .split("%MARKBG%").join("transparent")
            .split("%CODEFG%").join(String(m.codeTextColor))
            .split("%TAGFG%").join(String(m.tagTextColor));
    }

    // ---- geometry dump helpers (tests/geometry; never used for layout) ---------------------------
    function r2(v: real): real { return Math.round(v * 100) / 100; }

    /**
     * One dump entry for @p item, in @p origin's coordinates. @p extra overrides
     * or adds keys; its x/y are item-local and get mapped too.
     */
    function geometryEntry(kind: string, block: var, item: Item, origin: Item, extra: var): var {
        const p = item.mapToItem(origin, 0, 0);
        const e = {
            kind: kind,
            text: block && block.text !== undefined ? String(block.text).substring(0, 40) : "",
            sourceLine: block && block.sourceLine !== undefined ? block.sourceLine : -1,
            x: m.r2(p.x), y: m.r2(p.y), w: m.r2(item.width), h: m.r2(item.height),
            depth: block && block.depth !== undefined ? block.depth : 0
        };
        if (block && block.checked !== undefined) {
            e.checked = !!block.checked;
        }
        if (extra) {
            for (const k in extra) {
                const v = extra[k];
                if (k === "textStartX") {
                    e[k] = m.r2(p.x + v);
                } else if (k === "firstLineBaselineY") {
                    e[k] = m.r2(p.y + v);
                } else if (k === "dx") {
                    e.x = m.r2(p.x + v);
                } else if (k === "dy") {
                    e.y = m.r2(p.y + v);
                } else {
                    e[k] = typeof v === "number" ? m.r2(v) : v;
                }
            }
        }
        return e;
    }

    /** "tag" / "code-inline" entries for every pill segment an InlineText drew. */
    function dumpPills(out: var, block: var, inlineText: Item, origin: Item) {
        const segs = inlineText.pillSegments || [];
        for (let i = 0; i < segs.length; ++i) {
            const s = segs[i];
            const p = inlineText.mapToItem(origin, s.x, s.y);
            const t = inlineText.mapToItem(origin, s.textStartX, s.baseline);
            out.push({
                kind: s.type === "tag" ? "tag" : "code-inline",
                text: "",
                sourceLine: block && block.sourceLine !== undefined ? block.sourceLine : -1,
                x: m.r2(p.x), y: m.r2(p.y), w: m.r2(s.w), h: m.r2(s.h),
                textStartX: m.r2(t.x), firstLineBaselineY: m.r2(t.y),
                lineCount: 1, depth: block && block.depth !== undefined ? block.depth : 0,
                fontPx: m.r2(s.fontPx)
            });
        }
    }

    /** Plain text -> safe rich text (inline title, property values). */
    function escapeHtml(text: string): string {
        return String(text).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
    }
}
