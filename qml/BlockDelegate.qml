/*
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick

/**
 * One block of a BlockList: the gap above it, the block itself, and the
 * list-item bottom pads that close after it.
 *
 * DelegateChooser needs Qt 6.9 and the project minimum is 6.7, so the block
 * component is picked by kind with a plain Loader (contract §0.6).
 */
Item {
    id: del

    required property var modelData
    required property int index
    required property ObsidianMetrics metrics
    required property real availableWidth

    signal taskToggleRequested(int sourceLine, string expectedLineText)

    readonly property string kind: del.modelData && del.modelData.kind ? del.modelData.kind : ""
    readonly property bool isListItem: del.kind === "bullet" || del.kind === "ordered" || del.kind === "task"
    readonly property real gap: del.metrics.gapPx(del.modelData ? del.modelData.gap : "none")
    readonly property real padAfter: (del.modelData && del.modelData.listPadAfter ? del.modelData.listPadAfter : 0) * del.metrics.listItemPad
    /** Non-list blocks inside a list item sit at that item's text column. */
    readonly property real contentX: !del.isListItem && del.modelData && del.modelData.inItem
        ? del.metrics.listTextX(del.modelData.depth || 0) : 0
    readonly property Item blockItem: loader.item
    readonly property real contentWidth: Math.max(0, del.width - del.contentX)

    width: del.availableWidth
    height: del.gap + (loader.item ? loader.item.height : 0) + del.padAfter

    Loader {
        id: loader

        // No explicit size: a sized Loader would stretch every block (a narrow
        // table included) to the full width. Blocks size themselves.
        x: del.contentX
        y: del.gap

        sourceComponent: {
            switch (del.kind) {
            case "heading": return headingComponent;
            case "paragraph": return paragraphComponent;
            case "bullet":
            case "ordered":
            case "task": return listItemComponent;
            case "blockquote": return blockquoteComponent;
            case "callout": return calloutComponent;
            case "code": return codeComponent;
            case "table": return tableComponent;
            case "hr": return ruleComponent;
            case "properties": return propertiesComponent;
            default: return null;
            }
        }
    }

    Connections {
        target: loader.item
        ignoreUnknownSignals: true

        function onToggleRequested(sourceLine: int, expectedLineText: string) {
            del.taskToggleRequested(sourceLine, expectedLineText);
        }
    }

    Component {
        id: headingComponent
        HeadingBlock { block: del.modelData; metrics: del.metrics; availableWidth: del.contentWidth }
    }
    Component {
        id: paragraphComponent
        ParagraphBlock { block: del.modelData; metrics: del.metrics; availableWidth: del.contentWidth }
    }
    Component {
        id: listItemComponent
        ListItemBlock {
            block: del.modelData
            metrics: del.metrics
            availableWidth: del.contentWidth
            listX: del.contentX
            listY: del.y + del.gap
        }
    }
    Component {
        id: blockquoteComponent
        BlockquoteBlock { block: del.modelData; metrics: del.metrics; availableWidth: del.contentWidth }
    }
    Component {
        id: calloutComponent
        CalloutBlock { block: del.modelData; metrics: del.metrics; availableWidth: del.contentWidth }
    }
    Component {
        id: codeComponent
        CodeBlock { block: del.modelData; metrics: del.metrics; availableWidth: del.contentWidth }
    }
    Component {
        id: tableComponent
        TableBlock {
            block: del.modelData
            metrics: del.metrics
            availableWidth: del.contentWidth
            listX: del.contentX
            listY: del.y + del.gap
        }
    }
    Component {
        id: ruleComponent
        RuleBlock { block: del.modelData; metrics: del.metrics; availableWidth: del.contentWidth }
    }
    Component {
        id: propertiesComponent
        PropertiesBlock { block: del.modelData; metrics: del.metrics; availableWidth: del.contentWidth }
    }
}
