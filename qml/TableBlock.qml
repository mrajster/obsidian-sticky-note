/*
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick

/**
 * GFM table, border-collapse geometry:
 *   column width = max(content + 2*padX + 1 border, 6ch)   (border-box)
 *   row height   = content (1.3em line grid) + 2*padY + 1 border
 *   table        = sum(columns) + 1  x  sum(rows) + 1
 * A table wider than the view is scaled down proportionally and its cells
 * wrap -- it never scrolls or overflows sideways.
 */
Item {
    id: table

    required property var block
    required property ObsidianMetrics metrics
    required property real availableWidth

    readonly property var allRows: [table.block.header || []].concat(table.block.rows || [])
    readonly property var align: table.block.align || []
    readonly property int columnCount: {
        let n = 0;
        for (let r = 0; r < table.allRows.length; ++r) {
            n = Math.max(n, table.allRows[r].length);
        }
        return n;
    }
    readonly property real border: table.metrics.tableBorder

    /** Natural (unwrapped) text width per column, fed by the cells. */
    property var naturalText: []
    readonly property var columnWidths: {
        const m = table.metrics;
        const widths = [];
        let sum = 0;
        for (let c = 0; c < table.columnCount; ++c) {
            const t = table.naturalText[c] || 0;
            const w = Math.max(t + 2 * m.tableCellPadX + table.border, m.tableColumnMinWidth);
            widths.push(w);
            sum += w;
        }
        const room = table.availableWidth - table.border;
        if (sum > room && sum > 0 && room > 0) {
            const f = room / sum;
            for (let c = 0; c < widths.length; ++c) {
                widths[c] *= f;
            }
        }
        return widths;
    }
    readonly property var columnX: {
        const xs = [0];
        for (let c = 0; c < table.columnWidths.length; ++c) {
            xs.push(xs[c] + table.columnWidths[c]);
        }
        return xs;
    }

    function refreshNatural() {
        const widths = [];
        for (let r = 0; r < rowRepeater.count; ++r) {
            const row = rowRepeater.itemAt(r);
            if (!row) {
                continue;
            }
            for (let c = 0; c < row.cellRepeater.count; ++c) {
                const cell = row.cellRepeater.itemAt(c);
                if (cell) {
                    widths[c] = Math.max(widths[c] || 0, cell.naturalWidth);
                }
            }
        }
        table.naturalText = widths;
    }

    Timer {
        id: naturalTimer
        interval: 0
        onTriggered: table.refreshNatural()
    }

    width: table.columnX[table.columnX.length - 1] + table.border
    height: rows.height + table.border

    /**
     * This table's position inside its BlockList (BlockDelegate sets it). The
     * grid is pixel-snapped the way Chromium snaps collapsed borders: every
     * line lands on the device pixel its exact edge rounds to, so rules meet
     * the frame exactly instead of overshooting it.
     *
     * Why the lengths are whole pixels: Qt Quick's software renderer draws a
     * Rectangle at round(position) with ceil(size), while the GPU renderer
     * fills pixel centres. A fractional length (114.06 px) is therefore 115 px
     * in one and 114 px in the other; an integral length is the same in both.
     */
    property real listX: 0
    property real listY: 0

    /** Whole-pixel span from table-local @p from to @p to (inclusive of a 1 px line at @p to). */
    function snappedSpan(origin: real, from: real, to: real): int {
        return Math.round(origin + to) - Math.round(origin + from) + table.border;
    }

    // Grid lines: the table's outer frame plus every column boundary...
    Repeater {
        model: table.columnCount + 1
        delegate: Rectangle {
            required property int index
            x: table.columnX[index]
            width: table.border
            height: table.snappedSpan(table.listY, 0, rows.height)
            color: table.metrics.borderColor
        }
    }

    Column {
        id: rows

        x: table.border / 2
        y: table.border / 2

        Repeater {
            id: rowRepeater

            model: table.allRows

            delegate: Item {
                id: row

                required property var modelData
                required property int index

                readonly property bool header: row.index === 0
                readonly property alias cellRepeater: cellRepeater
                property real contentHeight: table.metrics.tableLineHeight

                function refreshHeight() {
                    let h = table.metrics.tableLineHeight;
                    for (let c = 0; c < cellRepeater.count; ++c) {
                        const cell = cellRepeater.itemAt(c);
                        if (cell) {
                            h = Math.max(h, cell.height);
                        }
                    }
                    row.contentHeight = h;
                }

                width: table.columnX[table.columnX.length - 1]
                height: row.contentHeight + 2 * table.metrics.tableCellPadY + table.border

                // ...and every row boundary, from the left frame line to the right one.
                Rectangle {
                    x: -table.border / 2
                    y: -table.border / 2
                    width: table.snappedSpan(table.listX, 0, table.columnX[table.columnX.length - 1])
                    height: table.border
                    color: table.metrics.borderColor
                }

                Rectangle {
                    visible: row.index === table.allRows.length - 1
                    x: -table.border / 2
                    y: row.height - table.border / 2
                    width: table.snappedSpan(table.listX, 0, table.columnX[table.columnX.length - 1])
                    height: table.border
                    color: table.metrics.borderColor
                }

                Repeater {
                    id: cellRepeater

                    model: row.modelData

                    delegate: InlineText {
                        id: cell

                        required property var modelData
                        required property int index

                        x: table.columnX[cell.index] + table.border / 2 + table.metrics.tableCellPadX
                        y: table.border / 2 + table.metrics.tableCellPadY
                        html: cell.modelData.inlineHtml || ""
                        decorations: cell.modelData.decorations || []
                        marks: cell.modelData.marks || []
                        // Columns are sized from CSS max-content: advances only.
                        measureNaturalWidth: true
                        metrics: table.metrics
                        fontPx: table.metrics.em
                        fontWeight: row.header ? table.metrics.tableHeaderWeight : Font.Normal
                        fontMetrics: row.header ? table.metrics.fmBold : table.metrics.fmBody
                        letterSpacing: 0
                        lineHeightPx: table.metrics.tableLineHeight
                        struck: false
                        horizontalAlignment: {
                            switch (table.align[cell.index]) {
                            case "center": return TextEdit.AlignHCenter;
                            case "right": return TextEdit.AlignRight;
                            default: return TextEdit.AlignLeft;
                            }
                        }
                        availableWidth: Math.max(0, (table.columnWidths[cell.index] || 0)
                                                 - table.border - 2 * table.metrics.tableCellPadX)

                        onNaturalWidthChanged: naturalTimer.restart()
                        onHeightChanged: row.refreshHeight()
                        Component.onCompleted: {
                            naturalTimer.restart();
                            row.refreshHeight();
                        }
                    }
                }
            }
        }
    }

    function dumpGeometry(out: var, origin: Item, extra: var) {
        const m = table.metrics;
        out.push(m.geometryEntry("table", table.block, table, origin, { fontPx: m.em, lineHeightPx: m.tableLineHeight }));
        const tableLine = table.block.sourceLine || 0;
        for (let r = 0; r < rowRepeater.count; ++r) {
            const row = rowRepeater.itemAt(r);
            if (!row) {
                continue;
            }
            // Row 0 is the header line; the delimiter row is source line +1.
            const line = r === 0 ? tableLine : tableLine + r + 1;
            const rowBlock = { sourceLine: line, text: "", depth: 0 };
            out.push(m.geometryEntry("tr", rowBlock, row, origin, { fontPx: m.em, lineHeightPx: m.tableLineHeight }));
            for (let c = 0; c < row.cellRepeater.count; ++c) {
                const cell = row.cellRepeater.itemAt(c);
                if (!cell) {
                    continue;
                }
                const cellBlock = { sourceLine: line, text: cell.modelData.text || "", depth: 0 };
                const cx = table.columnX[c];
                const e = m.geometryEntry(r === 0 ? "th" : "td", cellBlock, row, origin, {
                    dx: cx,
                    w: table.columnWidths[c],
                    h: row.height,
                    textStartX: cx + table.border / 2 + m.tableCellPadX,
                    firstLineBaselineY: table.border / 2 + m.tableCellPadY + cell.firstBaselineY,
                    lineCount: cell.lineCount,
                    fontPx: m.em,
                    lineHeightPx: m.tableLineHeight
                });
                out.push(e);
                m.dumpPills(out, cellBlock, cell, origin);
            }
        }
    }
}
