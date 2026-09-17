/*
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
    SPDX-License-Identifier: GPL-2.0-or-later

    Geometry dump for reading-view parity with Obsidian.

    Instantiates the REAL NoteView against a REAL MarkdownNote bound to
    dumpFile, waits until the layout has settled, walks the block delegates and
    writes a JSON file in the shape of Obsidian's rects dump:

      {"sizerWidth","baseFontPx","showInlineTitle","coordOrigin","sourceLineBase":0,
       "blocks":[{"i","kind","text","sourceLine","x","y","w","h","textStartX",
                  "firstLineBaselineY","lineCount","depth","checked","fontPx",
                  "lineHeightPx", ...}]}

    Every coordinate is px relative to the content box top-left, i.e. the
    Flickable's content item offset by metrics.containerPadding on both axes.

    Everything is MEASURED from the live item tree (mapToItem, baselineOffset,
    positionToRectangle). Where the view does not expose a separate item for a
    sub-part (a bullet dot, a table cell box, a callout title row, a pill
    background) the entry is still emitted but flagged "derived": true so the
    comparison output can tell a measurement from a formula. Delegate parts
    can opt into exact measurement by setting objectName to the dump kind:
    "bullet", "checkbox", "inline-title", "callout-title", "callout-icon",
    "callout-content", "tr", "th", "td", "code-inline", "tag", "guide".
    "mark" entries are InlineText's painted ==highlight== boxes; "guide"
    entries (the indentation guide a list item draws for its nested list)
    follow the list item that owns them.

    Context properties (set by main.cpp): harnessHelper, dumpFile, dumpWidth,
    dumpFontPx, dumpInlineTitle, dumpShowProperties, dumpTextFamily, dumpOut,
    dumpPng.
*/

import QtQuick
import QtQuick.Window

Window {
    id: dump

    // content width + 2 * containerPadding (2em each side)
    width: Math.ceil(dumpWidth + 4 * dumpFontPx)
    height: 1200
    visible: true
    color: "white"

    property int frames: 0
    property int phase: 0
    property string lastSignature: ""
    property int stableTicks: 0
    property int ticks: 0

    MarkdownNote {
        id: note
        path: dumpFile
    }

    NoteView {
        id: view

        anchors.fill: parent

        note: note
        basePointSize: dumpFontPx * 0.75
        fontFamily: dumpTextFamily
        inlineTitle: note.fileName.replace(/\.md$/i, "")
        showInlineTitle: dumpInlineTitle
        showProperties: dumpShowProperties
        readOnly: true
    }

    onFrameSwapped: dump.frames += 1

    Timer {
        interval: 50
        repeat: true
        running: true
        onTriggered: dump.tick()
    }

    // ---------------------------------------------------------------------
    // settling
    // ---------------------------------------------------------------------

    function tick() {
        dump.ticks += 1;
        if (note.status !== MarkdownNote.Ready) {
            return;
        }
        const flick = dump.findFlickable(view);
        if (!flick) {
            harnessHelper.log("DUMP: NoteView has no Flickable (contract §2.4)");
            harnessHelper.finish(5);
            return;
        }
        // Two frames after the blocks arrived (offscreen may never swap: give up
        // on the frame count after 3 s and rely on the stability check alone).
        if (dump.frames < 2 && dump.ticks < 60) {
            return;
        }
        const sig = dump.signature(flick);
        if (sig === dump.lastSignature) {
            dump.stableTicks += 1;
        } else {
            dump.lastSignature = sig;
            dump.stableTicks = 0;
        }
        if (dump.stableTicks < 4) {
            return;
        }
        if (dump.phase === 0) {
            // Grow the window so the whole note is laid out and grabbed.
            const wanted = Math.min(16000, Math.max(200, Math.ceil(flick.contentHeight)));
            dump.phase = 1;
            dump.stableTicks = 0;
            dump.frames = 0;
            dump.ticks = 0;
            if (wanted !== dump.height) {
                dump.height = wanted;
                return;
            }
        }
        if (dump.phase === 1) {
            dump.phase = 2;
            dump.writeDump(flick);
        }
    }

    function signature(flick) {
        const ds = dump.collectDelegates(flick.contentItem, []);
        let sum = 0;
        for (let i = 0; i < ds.length; ++i) {
            sum += ds[i].height + ds[i].y;
        }
        return flick.contentHeight.toFixed(3) + ":" + ds.length + ":" + sum.toFixed(3) + ":" + view.blocks.length;
    }

    // ---------------------------------------------------------------------
    // item-tree helpers
    // ---------------------------------------------------------------------

    function kidsOf(item) {
        return item && item.children ? item.children : [];
    }

    function has(o, name) {
        return o !== null && o !== undefined && (name in o);
    }

    function findFlickable(item) {
        if (dump.has(item, "flickableDirection") && dump.has(item, "contentItem") && dump.has(item, "contentY")) {
            return item;
        }
        const k = dump.kidsOf(item);
        for (let i = 0; i < k.length; ++i) {
            const f = dump.findFlickable(k[i]);
            if (f) {
                return f;
            }
        }
        return null;
    }

    function isDelegate(o) {
        return dump.has(o, "modelData") && dump.has(o, "availableWidth") && o.modelData
            && typeof o.modelData.kind === "string";
    }

    function isBlockList(o) {
        return dump.has(o, "blocks") && dump.has(o, "availableWidth") && !dump.has(o, "modelData")
            && !dump.has(o, "block");
    }

    function isInlineText(o) {
        return dump.has(o, "firstBaselineY") && dump.has(o, "lineHeightPx") && dump.has(o, "decorations");
    }

    function isTextItem(o) {
        const c = harnessHelper.className(o);
        return c.indexOf("QQuickTextEdit") === 0 || c.indexOf("QQuickText") === 0
            || c.indexOf("QQuickTextArea") === 0;
    }

    /** Visible delegates under @p item, not descending into delegates. DFS order. */
    function collectDelegates(item, out) {
        const k = dump.kidsOf(item);
        const local = [];
        for (let i = 0; i < k.length; ++i) {
            const c = k[i];
            if (!c.visible) {
                continue;
            }
            if (dump.isDelegate(c)) {
                local.push(c);
            } else {
                if (local.length > 0) {
                    dump.pushSorted(out, local);
                    local.length = 0;
                }
                dump.collectDelegates(c, out);
            }
        }
        dump.pushSorted(out, local);
        return out;
    }

    function pushSorted(out, local) {
        local.sort((a, b) => (dump.has(a, "index") && dump.has(b, "index")) ? a.index - b.index : a.y - b.y);
        for (let i = 0; i < local.length; ++i) {
            out.push(local[i]);
        }
    }

    /** First descendant satisfying @p pred; never enters nested delegates or block lists. */
    function findFirst(item, pred) {
        const k = dump.kidsOf(item);
        for (let i = 0; i < k.length; ++i) {
            const c = k[i];
            if (!c.visible || dump.isDelegate(c) || dump.isBlockList(c)) {
                continue;
            }
            if (pred(c)) {
                return c;
            }
            const f = dump.findFirst(c, pred);
            if (f) {
                return f;
            }
        }
        return null;
    }

    function findAll(item, pred, out) {
        const k = dump.kidsOf(item);
        for (let i = 0; i < k.length; ++i) {
            const c = k[i];
            if (!c.visible || dump.isDelegate(c) || dump.isBlockList(c)) {
                continue;
            }
            if (pred(c)) {
                out.push(c);
            }
            dump.findAll(c, pred, out);
        }
        return out;
    }

    function findNamed(item, name) {
        return dump.findFirst(item, c => c.objectName === name);
    }

    function loadedItem(d) {
        const k = dump.kidsOf(d);
        for (let i = 0; i < k.length; ++i) {
            if (dump.has(k[i], "sourceComponent") && dump.has(k[i], "item")) {
                return k[i].item;
            }
        }
        for (let i = 0; i < k.length; ++i) {
            if (k[i].visible) {
                return k[i];
            }
        }
        return d;
    }

    // ---------------------------------------------------------------------
    // measurement
    // ---------------------------------------------------------------------

    property var origin: null
    property real pad: 0
    property real em: dumpFontPx

    function r2(v) {
        return Math.round(v * 100) / 100;
    }

    function rectOf(item) {
        const p = item.mapToItem(dump.origin, 0, 0);
        return { x: p.x - dump.pad, y: p.y - dump.pad, w: item.width, h: item.height };
    }

    function pointOf(item, x, y) {
        const p = item.mapToItem(dump.origin, x, y);
        return { x: p.x - dump.pad, y: p.y - dump.pad };
    }

    /** textStartX / firstLineBaselineY / lineCount / fontPx / lineHeightPx of a text-bearing item. */
    function textInfo(container) {
        const inline = dump.isInlineText(container) ? container : dump.findFirst(container, dump.isInlineText);
        const te = dump.isTextItem(container) ? container
                 : dump.findFirst(inline ? inline : container, dump.isTextItem);
        if (!te) {
            return null;
        }
        // Baseline: for a TextEdit, Item.baselineOffset is NOT where Qt draws the
        // first line once a CSS line-height larger than the font is applied
        // (measured against the rendered PNG: it sits 2 px high at 16 px / 24 px).
        // The layout's own first line is authoritative: cursor rect top + font
        // ascent, which matches the rendered ink. For a plain Text item
        // baselineOffset is used as-is.
        const isEdit = typeof te.positionToRectangle === "function";
        const r0 = isEdit ? te.positionToRectangle(0) : Qt.rect(0, 0, 0, 0);
        const baselineLocal = isEdit ? r0.y + dump.ascentOf(te.font) : te.baselineOffset;
        const start = dump.pointOf(te, r0.x + (!isEdit && dump.has(te, "leftPadding") ? te.leftPadding : 0), baselineLocal);
        const info = {
            textStartX: start.x,
            firstLineBaselineY: start.y,
            baselineOffsetY: dump.pointOf(te, 0, te.baselineOffset).y,
            lineCount: te.lineCount,
            fontPx: inline && dump.has(inline, "fontPx") ? inline.fontPx
                  : (te.font.pixelSize > 0 ? te.font.pixelSize : te.font.pointSize * 4 / 3),
            lineHeightPx: inline && dump.has(inline, "lineHeightPx") ? inline.lineHeightPx
                        : (dump.has(te, "lineHeightMode") && te.lineHeightMode === Text.FixedHeight ? te.lineHeight : null),
            inline: inline,
            textItem: te,
        };
        if (inline && dump.has(inline, "firstBaselineY")) {
            const declared = dump.pointOf(inline, 0, inline.firstBaselineY).y;
            info.declaredBaselineY = declared;
            if (Math.abs(declared - info.firstLineBaselineY) > 0.5) {
                harnessHelper.log("DUMP: note: InlineText.firstBaselineY (" + dump.r2(declared)
                                  + ") != measured first-line baseline (" + dump.r2(info.firstLineBaselineY) + ")");
            }
        }
        return info;
    }

    function entry(kind, block, rect, extra) {
        const e = {
            kind: kind,
            text: block && typeof block.text === "string" ? block.text.substring(0, 40) : "",
            sourceLine: block && block.sourceLine !== undefined ? block.sourceLine : null,
            x: rect.x, y: rect.y, w: rect.w, h: rect.h,
        };
        if (block && block.depth !== undefined && block.depth > 0) {
            e.depth = block.depth;
        }
        if (extra) {
            for (const key in extra) {
                e[key] = extra[key];
            }
        }
        return e;
    }

    property FontMetrics probeMetrics: FontMetrics {}

    function ascentOf(font) {
        dump.probeMetrics.font = font;
        return dump.probeMetrics.ascent;
    }

    function addText(e, info) {
        if (!info) {
            return e;
        }
        e.textStartX = info.textStartX;
        e.firstLineBaselineY = info.firstLineBaselineY;
        e.lineCount = info.lineCount;
        e.fontPx = info.fontPx;
        e.lineHeightPx = info.lineHeightPx;
        e.baselineOffsetY = info.baselineOffsetY;
        if (info.declaredBaselineY !== undefined) {
            e.declaredBaselineY = info.declaredBaselineY;
        }
        return e;
    }

    function isListItem(b) {
        return b.kind === "bullet" || b.kind === "ordered" || b.kind === "task";
    }

    /** Does @p next belong to the subtree of list item @p item? */
    function inSubtree(item, next) {
        if (next.depth < item.depth || next.depth === 0) {
            return false;
        }
        if (next.depth === item.depth) {
            return !dump.isListItem(next) && next.inItem === true;
        }
        return true;
    }

    function emitDecorations(out, block, info) {
        if (!info || !info.inline || !info.textItem || typeof info.textItem.positionToRectangle !== "function") {
            return;
        }
        // ==highlight== backgrounds, as painted (one box per line the span covers).
        const marks = dump.has(info.inline, "markSegments") ? (info.inline.markSegments || []) : [];
        for (let i = 0; i < marks.length; ++i) {
            const g = marks[i];
            const p = dump.pointOf(info.inline, g.x, g.y);
            out.push(dump.entry("mark", block, { x: p.x, y: p.y, w: g.w, h: g.h }, {
                text: "",
                textStartX: p.x,
                firstLineBaselineY: dump.pointOf(info.inline, 0, g.baseline).y,
                lineCount: 1,
                fontPx: info.fontPx,
            }));
        }
        const decos = info.inline.decorations || [];
        const te = info.textItem;
        const pills = dump.findAll(info.inline, c => c !== te && dump.has(c, "radius") && c.width > 0, []);
        for (let i = 0; i < decos.length; ++i) {
            const d = decos[i];
            const kind = d.type === "tag" ? "tag" : "code-inline";
            const before = te.positionToRectangle(Math.max(0, d.start - 1));
            const first = te.positionToRectangle(d.start);
            const after = te.positionToRectangle(d.start + d.length + 1);
            const left = dump.pointOf(te, before.x, before.y);
            const textStart = dump.pointOf(te, first.x, first.y);
            const sameLine = Math.abs(after.y - before.y) < 0.5;
            const right = sameLine ? dump.pointOf(te, after.x, after.y).x : dump.pointOf(te, te.width, 0).x;
            const e = dump.entry(kind, block, { x: left.x, y: left.y, w: right - left.x, h: before.height }, {
                text: typeof te.getText === "function" ? te.getText(d.start, d.start + d.length) : "",
                textStartX: textStart.x,
                // Baseline of the line the span starts on (it may have wrapped).
                firstLineBaselineY: dump.pointOf(te, 0, first.y + dump.ascentOf(te.font)).y,
                fontPx: kind === "tag" ? view.metrics.tagSize : view.metrics.inlineCodeSize,
            });
            // Prefer the real pill background under this span when one exists.
            let best = null;
            for (let p = 0; p < pills.length; ++p) {
                const pr = dump.rectOf(pills[p]);
                if (pr.x <= left.x + 1.5 && pr.x + pr.w >= left.x + 1 && Math.abs((pr.y + pr.h / 2) - (left.y + before.height / 2)) < before.height) {
                    best = pr;
                    break;
                }
            }
            if (best) {
                e.x = best.x; e.y = best.y; e.w = best.w; e.h = best.h;
            } else {
                e.derived = true;
            }
            out.push(e);
        }
    }

    function emitList(out, delegates) {
        for (let i = 0; i < delegates.length; ++i) {
            const d = delegates[i];
            const b = d.modelData;
            const item = dump.loadedItem(d);
            const rect = dump.rectOf(item);
            const info = dump.textInfo(item);

            if (b.kind === "heading") {
                out.push(dump.addText(dump.entry("h" + b.level, b, rect), info));
                dump.emitDecorations(out, b, info);
            } else if (b.kind === "paragraph") {
                out.push(dump.addText(dump.entry("p", b, rect), info));
                dump.emitDecorations(out, b, info);
            } else if (dump.isListItem(b)) {
                dump.emitListItem(out, delegates, i, item, rect, info);
            } else if (b.kind === "blockquote") {
                const children = dump.collectDelegates(item, []);
                const firstInfo = children.length > 0 ? dump.textInfo(dump.loadedItem(children[0])) : null;
                out.push(dump.addText(dump.entry("blockquote", b, rect), firstInfo));
                dump.emitList(out, children);
            } else if (b.kind === "callout") {
                dump.emitCallout(out, b, item, rect);
            } else if (b.kind === "code") {
                out.push(dump.addText(dump.entry("pre", b, rect, { language: b.language || "" }), info));
            } else if (b.kind === "table") {
                dump.emitTable(out, b, item, rect);
            } else if (b.kind === "hr") {
                out.push(dump.entry("hr", b, rect));
            } else if (b.kind === "properties") {
                out.push(dump.addText(dump.entry("properties", b, rect), info));
            } else {
                out.push(dump.addText(dump.entry(b.kind, b, rect), info));
            }
        }
    }

    function emitListItem(out, delegates, i, item, rect, info) {
        const d = delegates[i];
        const b = d.modelData;
        const m = view.metrics;

        // h = the whole subtree including its list-item paddings.
        let last = d;
        for (let j = i + 1; j < delegates.length && dump.inSubtree(b, delegates[j].modelData); ++j) {
            last = delegates[j];
        }
        const lastRect = dump.rectOf(last);
        // The last delegate's listPadAfter also closes ancestors of this item;
        // only the pads closing this item or its descendants belong to it.
        const lastBlock = last.modelData;
        const closingInside = (lastBlock.depth || 0) - (b.depth || 0) + 1;
        const foreign = Math.max(0, (lastBlock.listPadAfter || 0) - closingInside);
        const bottom = lastRect.y + last.height - foreign * m.listItemPad;
        const textX = info ? info.textStartX : rect.x;
        const right = rect.x + rect.w;
        const kind = b.kind === "task" ? "task" : "li";
        const extra = { listType: b.listType };
        if (b.kind === "task") {
            extra.checked = b.checked === true;
        }
        if (b.markerText) {
            extra.markerText = b.markerText;
        }
        const e = dump.addText(dump.entry(kind, b, { x: textX, y: rect.y, w: right - textX, h: bottom - rect.y }, extra), info);
        out.push(e);

        const lineTop = rect.y + m.listItemPad;
        if (b.kind === "bullet") {
            let dot = dump.findNamed(item, "bullet");
            if (!dot) {
                dot = dump.findFirst(item, c => dump.has(c, "radius") && dump.has(c, "color")
                                    && c.width > 0 && Math.abs(c.width - c.height) < 0.01
                                    && c.width <= 0.5 * dump.em && !dump.isInlineText(c));
            }
            let br;
            let derived = false;
            if (dot) {
                br = dump.rectOf(dot);
            } else {
                const s = m.bulletSize;
                br = { x: textX - m.bulletCenterOffset - s / 2, y: lineTop + m.lhBody / 2 - s / 2, w: s, h: s };
                derived = true;
            }
            const be = dump.entry("bullet", b, br, { dotCenterX: br.x + br.w / 2, dotCenterY: br.y + br.h / 2 });
            if (derived) {
                be.derived = true;
            }
            out.push(be);
        } else if (b.kind === "task") {
            let cb = dump.findNamed(item, "checkbox");
            let box = cb;
            if (!cb) {
                cb = dump.findFirst(item, c => dump.has(c, "checked") && dump.has(c, "interactive") && dump.has(c, "metrics"));
                box = cb;
                if (cb && Math.abs(cb.width - m.checkboxSize) > 0.5) {
                    const inner = dump.findFirst(cb, c => dump.has(c, "border") && Math.abs(c.width - m.checkboxSize) < 0.5
                                                 && Math.abs(c.height - m.checkboxSize) < 0.5);
                    if (inner) {
                        box = inner;
                    }
                }
            }
            if (box) {
                out.push(dump.entry("checkbox", b, dump.rectOf(box), {
                    checked: b.checked === true,
                    interactive: cb && dump.has(cb, "interactive") ? cb.interactive : null,
                }));
            } else {
                harnessHelper.log("DUMP: task at line " + b.sourceLine + " has no TaskCheckbox item");
            }
        }
        dump.emitDecorations(out, b, info);

        // The indentation guide this item draws for its nested list, if any.
        const guide = dump.guidesByLine[b.sourceLine];
        if (guide) {
            out.push(dump.entry("guide", b, dump.rectOf(guide), { text: "" }));
        }
    }

    /** sourceLine -> the IndentGuides line Rectangle drawn for that list item's children. */
    property var guidesByLine: ({})

    function collectGuides(item, into) {
        const k = dump.kidsOf(item);
        for (let i = 0; i < k.length; ++i) {
            const c = k[i];
            if (c.objectName === "guide" && dump.has(c, "modelData") && c.modelData
                    && c.modelData.sourceLine !== undefined && c.visible) {
                into[c.modelData.sourceLine] = c;
            }
            dump.collectGuides(c, into);
        }
        return into;
    }

    function emitCallout(out, b, item, rect) {
        const m = view.metrics;
        out.push(dump.entry("callout", b, rect, { calloutType: b.calloutType, folded: b.folded === true }));

        const titleInfo = dump.textInfo(item);
        const named = dump.findNamed(item, "callout-title");
        let tr;
        let derived = false;
        let titleRow = null;
        if (named) {
            tr = dump.rectOf(named);
            titleRow = named;
        } else if (titleInfo && titleInfo.inline && titleInfo.inline.parent && titleInfo.inline.parent !== item) {
            // The row holding icon + title text.
            titleRow = titleInfo.inline.parent;
            tr = dump.rectOf(titleRow);
        } else {
            tr = { x: rect.x + m.calloutPadLeft, y: rect.y + m.calloutPadTop,
                   w: rect.w - m.calloutPadLeft - m.calloutPadRight, h: m.calloutTitleLineHeight };
            derived = true;
        }
        const te = dump.addText(dump.entry("callout-title", b, tr), titleInfo);
        if (derived) {
            te.derived = true;
        }
        out.push(te);

        let icon = dump.findNamed(item, "callout-icon");
        if (!icon) {
            icon = dump.findFirst(item, c => dump.has(c, "isMask") && dump.has(c, "source"));
        }
        if (icon) {
            // Obsidian's icon box is the 1.3em title line slot, icon-wide.
            const ir = dump.rectOf(icon);
            const slot = titleRow ? dump.rectOf(titleRow) : ir;
            out.push(dump.entry("callout-icon", b, { x: ir.x, y: slot.y, w: ir.w, h: slot.h },
                                { text: "", iconY: ir.y, iconH: ir.h }));
        }

        let body = dump.findNamed(item, "callout-content");
        const bodyList = dump.findBlockList(item);
        if (!body) {
            body = bodyList;
        }
        if (body && body.visible) {
            const br = dump.rectOf(body);
            // The body box runs down to the callout's bottom padding (the
            // trailing 1em after the last child lives there, as in Obsidian).
            if (body === bodyList) {
                br.h = rect.y + rect.h - m.calloutPadBottom - br.y;
            }
            out.push(dump.entry("callout-content", b, br, { listHeight: body.height }));
        }
        if (bodyList && bodyList.visible) {
            dump.emitList(out, dump.collectDelegates(bodyList, []));
        }
    }

    function findBlockList(item) {
        const k = dump.kidsOf(item);
        for (let i = 0; i < k.length; ++i) {
            if (dump.isBlockList(k[i])) {
                return k[i];
            }
            if (dump.isDelegate(k[i])) {
                continue;
            }
            const f = dump.findBlockList(k[i]);
            if (f) {
                return f;
            }
        }
        return null;
    }

    function emitTable(out, b, item, rect) {
        const m = view.metrics;
        out.push(dump.entry("table", b, rect, { columns: (b.align || []).length }));

        const named = dump.findAll(item, c => c.objectName === "tr" || c.objectName === "th" || c.objectName === "td", []);
        if (named.length > 0) {
            let row = -1;
            for (let i = 0; i < named.length; ++i) {
                const c = named[i];
                if (c.objectName === "tr") {
                    row += 1;
                }
                const line = row <= 0 ? b.sourceLine : b.sourceLine + 1 + row;
                const e = dump.entry(c.objectName, b, dump.rectOf(c), { sourceLine: line });
                if (c.objectName !== "tr") {
                    dump.addText(e, dump.textInfo(c));
                    e.text = e.lineCount !== undefined && dump.textInfo(c) && dump.textInfo(c).textItem
                        && typeof dump.textInfo(c).textItem.getText === "function"
                        ? dump.textInfo(c).textItem.getText(0, 40) : "";
                }
                out.push(e);
            }
            return;
        }

        // The view's own row items and column grid (the arrays the grid lines are drawn from).
        const rowItems = dump.findAll(item, c => dump.has(c, "cellRepeater") && dump.has(c, "header"), []);
        if (rowItems.length > 0 && dump.has(item, "columnX") && dump.has(item, "columnWidths")) {
            rowItems.sort((a, c) => a.y - c.y);
            for (let r = 0; r < rowItems.length; ++r) {
                const row = rowItems[r];
                const rr = dump.rectOf(row);
                const line = r === 0 ? b.sourceLine : b.sourceLine + 1 + r;
                const cellsText = [];
                for (let c = 0; c < row.cellRepeater.count; ++c) {
                    const cell = row.cellRepeater.itemAt(c);
                    cellsText.push(cell && cell.modelData ? String(cell.modelData.text || "") : "");
                }
                out.push(dump.entry("tr", b, rr, { sourceLine: line, text: cellsText.join(" ").substring(0, 40) }));
                for (let c = 0; c < row.cellRepeater.count; ++c) {
                    const cell = row.cellRepeater.itemAt(c);
                    if (!cell) {
                        continue;
                    }
                    const info = dump.textInfo(cell);
                    const e = dump.addText(dump.entry(r === 0 ? "th" : "td", b, {
                        x: rr.x + item.columnX[c], y: rr.y, w: item.columnWidths[c], h: rr.h,
                    }, { sourceLine: line, text: cellsText[c].substring(0, 40) }), info);
                    out.push(e);
                    dump.emitDecorations(out, { sourceLine: line, depth: 0, text: cellsText[c] }, info);
                }
            }
            return;
        }

        // Fallback: one InlineText per cell; cell/row boxes derived from the
        // contract's cell padding around the measured text items.
        const cells = dump.findAll(item, dump.isInlineText, []);
        const rows = [];
        for (let i = 0; i < cells.length; ++i) {
            const r = dump.rectOf(cells[i]);
            let row = null;
            for (let k = 0; k < rows.length; ++k) {
                if (Math.abs(rows[k].y - r.y) < 1) {
                    row = rows[k];
                }
            }
            if (!row) {
                row = { y: r.y, cells: [] };
                rows.push(row);
            }
            row.cells.push({ item: cells[i], rect: r });
        }
        rows.sort((a, c) => a.y - c.y);
        const headerCount = (b.header || []).length;
        for (let ri = 0; ri < rows.length; ++ri) {
            const row = rows[ri];
            row.cells.sort((a, c) => a.rect.x - c.rect.x);
            const line = ri === 0 && headerCount > 0 ? b.sourceLine : b.sourceLine + 1 + ri;
            const boxes = row.cells.map(c => ({
                x: c.rect.x - m.tableCellPadX, y: c.rect.y - m.tableCellPadY,
                w: c.rect.w + 2 * m.tableCellPadX, h: c.rect.h + 2 * m.tableCellPadY,
            }));
            const minX = Math.min.apply(null, boxes.map(bx => bx.x));
            const maxX = Math.max.apply(null, boxes.map(bx => bx.x + bx.w));
            const maxH = Math.max.apply(null, boxes.map(bx => bx.h));
            out.push(dump.entry("tr", b, { x: minX, y: boxes[0].y, w: maxX - minX, h: maxH },
                                { sourceLine: line, derived: true }));
            for (let ci = 0; ci < row.cells.length; ++ci) {
                const info = dump.textInfo(row.cells[ci].item);
                const e = dump.addText(dump.entry(ri === 0 && headerCount > 0 ? "th" : "td", b,
                                                  { x: boxes[ci].x, y: boxes[ci].y, w: boxes[ci].w, h: maxH },
                                                  { sourceLine: line, derived: true }), info);
                e.text = info && info.textItem && typeof info.textItem.getText === "function"
                    ? info.textItem.getText(0, 40) : "";
                out.push(e);
            }
        }
    }

    function emitInlineTitle(out, flick) {
        let title = dump.findNamed(flick.contentItem, "inline-title");
        if (!title) {
            title = dump.findFirst(flick.contentItem, c => dump.isInlineText(c) || dump.isTextItem(c));
        }
        if (!title) {
            harnessHelper.log("DUMP: inline title requested but no title item found");
            return;
        }
        const e = dump.entry("inline-title", null, dump.rectOf(title), { text: view.inlineTitle.substring(0, 40) });
        dump.addText(e, dump.textInfo(title));
        out.push(e);
    }

    function writeDump(flick) {
        const m = view.metrics;
        dump.origin = flick.contentItem;
        dump.pad = m.containerPadding;
        dump.em = dump.has(m, "em") ? m.em : dumpFontPx;

        dump.guidesByLine = dump.collectGuides(flick.contentItem, {});
        const out = [];
        if (dumpInlineTitle) {
            dump.emitInlineTitle(out, flick);
        }
        dump.emitList(out, dump.collectDelegates(flick.contentItem, []));

        const keys = ["x", "y", "w", "h", "textStartX", "firstLineBaselineY", "declaredBaselineY", "baselineOffsetY",
                      "fontPx", "lineHeightPx", "dotCenterX", "dotCenterY"];
        for (let i = 0; i < out.length; ++i) {
            out[i].i = i;
            for (let k = 0; k < keys.length; ++k) {
                if (typeof out[i][keys[k]] === "number") {
                    out[i][keys[k]] = dump.r2(out[i][keys[k]]);
                }
            }
        }

        const doc = {
            generator: "obsnote_qmlharness --dump",
            note: note.fileName,
            coordOrigin: "content box top-left inside 2em padding",
            sourceLineBase: 0,
            sizerWidth: dumpWidth,
            contentWidth: dump.r2(flick.width - 2 * dump.pad),
            baseFontPx: dumpFontPx,
            basePointSize: view.basePointSize,
            textFamily: dumpTextFamily,
            showInlineTitle: dumpInlineTitle,
            showProperties: dumpShowProperties,
            containerPadding: dump.r2(dump.pad),
            contentHeight: dump.r2(flick.contentHeight),
            window: [dump.width, dump.height],
            blockModelCount: view.blocks.length,
            blocks: out,
            // The view's own dumpGeometry() (formula-based self report), kept
            // separate for cross-checking; compare.py reads only "blocks".
            viewSelfReport: typeof view.dumpGeometry === "function" ? view.dumpGeometry() : null,
        };

        let ok = harnessHelper.writeTextFile(dumpOut, JSON.stringify(doc, null, 1) + "\n");
        if (ok) {
            harnessHelper.log("DUMP: wrote " + dumpOut + " (" + out.length + " entries from "
                              + view.blocks.length + " model blocks)");
        }
        if (dumpPng !== "" && !harnessHelper.grabPng(dump, dumpPng)) {
            ok = false;
        }
        harnessHelper.finish(ok ? 0 : 6);
    }
}
