/*
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick

/**
 * One run of inline markdown (a paragraph, a heading, a list item's text, a
 * table cell) laid out on a CSS line grid.
 *
 * Qt's own Text/TextEdit height and baseline are NOT usable for parity (contract
 * §0.1), so this item owns its geometry:
 *   - height         = lineCount * lineHeightPx
 *   - first baseline = metrics.baselineInBoxAt(fontMetrics, lineHeightPx, fontScale)
 *   - the TextEdit is shifted so Qt's own first baseline lands exactly there.
 *
 * Fractional font sizes: Qt's font database rounds every font to a whole pixel
 * size, so a 23.392 px H2 would be laid out AND drawn at 23 px (1.7% narrow,
 * wrapping differently from Obsidian). The TextEdit is therefore laid out at
 * the whole pixel size Qt would use anyway (renderPx) and scaled by the
 * remainder (fontScale); everything inside it is expressed in its own,
 * unscaled coordinates. At whole-pixel sizes (body text) fontScale is exactly 1.
 *
 * The TextEdit is read-only AND disabled: it never takes a mouse press, so a
 * click falls through to NoteView's background MouseArea, which asks linkAt().
 */
Item {
    id: inline

    required property string html
    required property var decorations
    required property ObsidianMetrics metrics
    required property real fontPx
    required property int fontWeight
    required property real letterSpacing
    required property real lineHeightPx
    required property bool struck
    required property real availableWidth

    /** Metrics (at this run's font size) whose rounded ascent/descent define the CSS baseline. */
    property FontMetrics fontMetrics: inline.metrics.fmBody
    property string fontFamily: inline.metrics.textFamily
    property color color: inline.metrics.textColor
    property int horizontalAlignment: TextEdit.AlignLeft
    /** ==highlight== ranges as QTextDocument positions: [{start, length}]. */
    property var marks: []
    /**
     * Table cells: measure naturalWidth as CSS max-content (advance widths only)
     * with a second, never-wrapping layout. Costs a hidden TextEdit, so opt-in.
     */
    property bool measureNaturalWidth: false

    /** Marker for NoteView's hit test. */
    readonly property bool isInlineText: true

    /** The whole pixel size Qt lays the text out at, and the scale that makes it fontPx. */
    readonly property int renderPx: Math.max(1, Math.round(inline.fontPx))
    readonly property real fontScale: inline.fontPx / inline.renderPx

    readonly property real firstBaselineY: inline.metrics.baselineInBoxAt(inline.fontMetrics, inline.lineHeightPx, inline.fontScale)
    readonly property real textStartX: 0
    readonly property int lineCount: Math.max(1, edit.lineCount)
    /**
     * Unwrapped width of the content (table column sizing). A QTextDocument's
     * ideal width is the advance sum PLUS the ink overhang of the last glyph
     * (0 or 1 px: "Col A" gets +1, "Col B" does not); CSS max-content is the
     * advance sum alone. The measured path reads the advances directly.
     */
    readonly property real naturalWidth: (measure.item ? measure.item.advance : Math.max(0, edit.implicitWidth - 1)) * inline.fontScale

    /** Rich text with the placeholders resolved and every block on the line grid. */
    readonly property string resolvedHtml: {
        const lh = "line-height:" + (inline.lineHeightPx / inline.fontScale) + "px;";
        let h = inline.metrics.resolveInlineAt(inline.html, inline.fontScale);
        if (h.indexOf("<p") < 0) {
            h = "<p style=\"margin:0\">" + h + "</p>";
        }
        return h.replace(/<p style="/g, "<p style=\"" + lh).replace(/<p>/g, "<p style=\"" + lh + "margin:0\">");
    }

    function linkAt(x: real, y: real): string {
        const p = inline.mapToItem(edit, x, y);
        if (p.x < 0 || p.y < 0 || p.x > edit.width || p.y > edit.height) {
            return "";
        }
        return edit.linkAt(p.x, p.y);
    }

    /** Text-edit y of line @p lineTop's baseline (Qt's own layout, edit coordinates). */
    function editBaselineForRectY(rectY: real): real {
        return rectY + editFont.ascent;
    }

    width: inline.availableWidth
    height: inline.lineCount * inline.lineHeightPx
    implicitWidth: edit.implicitWidth * inline.fontScale
    implicitHeight: inline.height

    FontMetrics {
        id: editFont
        font: edit.font
    }

    FontMetrics {
        id: monoPill
        font.family: inline.metrics.monoFamily
        font.pointSize: inline.metrics.pt(inline.metrics.inlineCodeSize)
    }

    FontMetrics {
        id: tagPill
        font.family: inline.fontFamily
        font.pointSize: inline.metrics.pt(inline.metrics.tagSize)
    }

    /** [{x, y, w, h, type}] in this item's coordinates, one per line of each pill. */
    property var pillSegments: []
    /** [{x, y, w, h, baseline}] in this item's coordinates, one per line of each highlight. */
    property var markSegments: []

    /**
     * One {rectY, left, right} per visual line the document range
     * [start, start + length) covers, in the TextEdit's own coordinates.
     */
    function lineRuns(start: int, length: int): var {
        const runs = [];
        const from = Math.max(0, Math.min(start, edit.length));
        const to = Math.max(from, Math.min(start + length, edit.length));
        let seg = null;
        let prevX = 0;
        for (let p = from; p <= to; ++p) {
            const r = edit.positionToRectangle(p);
            if (seg && Math.abs(r.y - seg.rectY) > 0.5) {
                // The span wrapped: close this line's run at the last
                // position that was still on it.
                seg.right = prevX;
                runs.push(seg);
                seg = null;
            }
            if (!seg) {
                seg = { rectY: r.y, left: r.x, right: r.x };
            }
            seg.right = r.x;
            prevX = r.x;
        }
        if (seg) {
            runs.push(seg);
        }
        return runs;
    }

    /**
     * A background box spanning whole pixels: Chromium snaps inline backgrounds
     * to device pixels (each edge to the pixel it rounds to), and a whole-pixel
     * size is drawn identically by Qt's software renderer (which ceils a
     * fractional size) and its GPU renderer (which fills pixel centres).
     */
    function snappedBox(left: real, right: real, top: real, height: real): var {
        return {
            x: left,
            y: top,
            w: Math.max(0, Math.round(right) - Math.round(left)),
            h: Math.max(0, Math.round(top + height) - Math.round(top))
        };
    }

    function recomputePills() {
        const s = inline.fontScale;
        const out = [];
        const decs = inline.decorations || [];
        if (decs.length > 0 && edit.length > 0) {
            for (let i = 0; i < decs.length; ++i) {
                const d = decs[i];
                const isTag = d.type === "tag";
                const fm = isTag ? tagPill : monoPill;
                const padX = isTag ? inline.metrics.tagPadX : inline.metrics.inlineCodePadX;
                const padY = isTag ? inline.metrics.tagPadY : inline.metrics.inlineCodePadY;
                const asc = Math.round(fm.ascent);
                const desc = Math.round(fm.descent);
                const runs = inline.lineRuns(d.start, d.length);
                for (let r = 0; r < runs.length; ++r) {
                    const g = runs[r];
                    const baseline = edit.y + s * inline.editBaselineForRectY(g.rectY);
                    const box = inline.snappedBox(edit.x + s * g.left - padX, edit.x + s * g.right + padX,
                                                  baseline - asc - padY, asc + desc + 2 * padY);
                    out.push({
                        type: d.type,
                        x: box.x,
                        y: box.y,
                        w: box.w,
                        h: box.h,
                        textStartX: edit.x + s * g.left,
                        baseline: baseline,
                        fontPx: isTag ? inline.metrics.tagSize : inline.metrics.inlineCodeSize
                    });
                }
            }
        }
        inline.pillSegments = out;

        // <mark>: the background covers the inline box's content area, i.e. the
        // run font's ROUNDED ascent above the baseline and rounded descent below
        // it (Chromium). QTextDocument would use Qt's own, differently rounded
        // ascent -- a pixel high -- so the resolved HTML leaves it transparent.
        const marks = [];
        const ms = inline.marks || [];
        if (ms.length > 0 && edit.length > 0) {
            const asc = Math.round(inline.fontMetrics.ascent * s);
            const desc = Math.round(inline.fontMetrics.descent * s);
            for (let i = 0; i < ms.length; ++i) {
                const runs = inline.lineRuns(ms[i].start, ms[i].length);
                for (let r = 0; r < runs.length; ++r) {
                    const g = runs[r];
                    const baseline = edit.y + s * inline.editBaselineForRectY(g.rectY);
                    const box = inline.snappedBox(edit.x + s * g.left, edit.x + s * g.right, baseline - asc, asc + desc);
                    box.baseline = baseline;
                    marks.push(box);
                }
            }
        }
        inline.markSegments = marks;
    }

    function schedulePills() {
        pillTimer.restart();
    }

    // Coalesces the burst of width/text/layout changes into one recompute
    // after the TextEdit has re-laid itself out.
    Timer {
        id: pillTimer
        interval: 0
        onTriggered: inline.recomputePills()
    }

    onDecorationsChanged: inline.schedulePills()
    onMarksChanged: inline.schedulePills()
    onWidthChanged: inline.schedulePills()
    onFontScaleChanged: inline.schedulePills()
    Component.onCompleted: inline.schedulePills()

    Repeater {
        model: inline.markSegments

        delegate: Rectangle {
            required property var modelData

            x: modelData.x
            y: modelData.y
            width: modelData.w
            height: modelData.h
            color: inline.metrics.markBackgroundColor
        }
    }

    Repeater {
        model: inline.pillSegments

        delegate: Rectangle {
            required property var modelData

            x: modelData.x
            y: modelData.y
            width: modelData.w
            height: modelData.h
            radius: modelData.type === "tag" ? inline.metrics.tagRadius : inline.metrics.inlineCodeRadius
            color: modelData.type === "tag" ? inline.metrics.tagBackgroundColor : inline.metrics.codeBackgroundColor
        }
    }

    TextEdit {
        id: edit

        transformOrigin: Item.TopLeft
        scale: inline.fontScale

        // Qt puts the first baseline at positionToRectangle(0).y + ascent;
        // move it onto the CSS baseline. Depends on the layout, hence contentHeight.
        y: inline.firstBaselineY - inline.fontScale * inline.editBaselineForRectY(edit.contentHeight >= 0 ? edit.positionToRectangle(0).y : 0)
        // +1: room for the last glyph's ink overhang (see naturalWidth), so a
        // run exactly as wide as the box does not wrap where CSS would not.
        width: inline.width / inline.fontScale + 1

        readOnly: true
        enabled: false
        activeFocusOnPress: false
        selectByMouse: false
        textFormat: TextEdit.RichText
        wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
        horizontalAlignment: inline.horizontalAlignment
        color: inline.color
        textMargin: 0

        font.family: inline.fontFamily
        font.pointSize: inline.metrics.pt(inline.renderPx)
        font.weight: inline.fontWeight
        font.letterSpacing: inline.letterSpacing / inline.fontScale
        font.strikeout: inline.struck

        text: inline.resolvedHtml

        onContentHeightChanged: inline.schedulePills()
        onTextChanged: inline.schedulePills()
    }

    // CSS max-content for table columns: the same rich text, never wrapped,
    // measured by cursor positions (advances) instead of the ideal width.
    Loader {
        id: measure

        active: inline.measureNaturalWidth
        visible: false

        sourceComponent: TextEdit {
            id: probe

            property real advance: 0

            function remeasure() {
                const text = probe.getText(0, probe.length);
                let w = 0;
                for (let i = 0; i <= probe.length; ++i) {
                    const c = i < probe.length ? text.charCodeAt(i) : 0;
                    if (i === probe.length || c === 0x2028 || c === 0x2029 || c === 10) {
                        w = Math.max(w, probe.positionToRectangle(i).x);
                    }
                }
                probe.advance = w;
            }

            readOnly: true
            enabled: false
            textFormat: TextEdit.RichText
            wrapMode: TextEdit.NoWrap
            textMargin: 0

            font: edit.font
            text: inline.resolvedHtml

            onImplicitWidthChanged: probe.remeasure()
            onTextChanged: probe.remeasure()
            onFontChanged: probe.remeasure()
            Component.onCompleted: probe.remeasure()
        }
    }
}
