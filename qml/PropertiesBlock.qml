/*
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import org.kde.kirigami as Kirigami

/**
 * YAML frontmatter shown as Obsidian's "Properties" block (read-only here).
 *
 * The numbers come from app.css variables via ObsidianMetrics and are NOT yet
 * verified against a rendered reference (contract §2.2).
 */
Item {
    id: props

    required property var block
    required property ObsidianMetrics metrics
    required property real availableWidth

    readonly property var entries: props.block.properties || []

    width: props.availableWidth
    height: column.y + column.height + props.metrics.propertiesPadY

    Column {
        id: column

        x: props.metrics.propertiesShiftX
        y: props.metrics.propertiesPadY
        width: Math.max(0, props.width - x)

        Item {
            id: headingRow

            width: column.width
            height: props.metrics.propertiesHeadingLineHeight + 2 * props.metrics.propertiesHeadingPad
                + props.metrics.propertiesHeadingMarginBottom

            Text {
                x: props.metrics.propertiesHeadingPad
                y: props.metrics.propertiesHeadingPad
                   + props.metrics.baselineInBox(props.metrics.fmBold, props.metrics.propertiesHeadingLineHeight)
                   - baselineOffset
                // i18nc() exists in the applet (KLocalizedContext) but not in bare test engines.
                text: typeof i18nc === "function" ? i18nc("@title frontmatter block", "Properties") : "Properties"
                textFormat: Text.PlainText
                font.family: props.metrics.textFamily
                font.pointSize: props.metrics.pt(props.metrics.em)
                font.weight: props.metrics.boldWeight
                color: props.metrics.textColor
            }
        }

        Repeater {
            model: props.entries

            delegate: Item {
                id: row

                required property var modelData

                readonly property var values: row.modelData.values && row.modelData.values.length > 0
                    ? row.modelData.values : []
                readonly property real labelWidth: Math.min(props.metrics.propertyLabelWidth, column.width * 0.4)

                width: column.width
                height: Math.max(props.metrics.propertyRowHeight, valueArea.height)

                Kirigami.Icon {
                    x: props.metrics.propertiesHeadingPad
                    y: (props.metrics.propertyRowHeight - height) / 2
                    width: props.metrics.propertyIconSize
                    height: props.metrics.propertyIconSize
                    source: row.values.length > 0 ? "view-list-text" : "text-plain"
                    color: props.metrics.mutedTextColor
                    isMask: true
                }

                Text {
                    x: props.metrics.propertiesHeadingPad + props.metrics.propertyIconSize + props.metrics.propertyIconGap
                    width: Math.max(0, row.labelWidth - x)
                    y: props.metrics.baselineInBox(props.metrics.fmProperty, props.metrics.propertyRowHeight) - baselineOffset
                    text: row.modelData.key || ""
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    font.family: props.metrics.textFamily
                    font.pointSize: props.metrics.pt(props.metrics.propertyFontSize)
                    color: props.metrics.mutedTextColor
                }

                Item {
                    id: valueArea

                    x: row.labelWidth
                    width: Math.max(0, row.width - x)
                    height: row.values.length > 0 ? pills.height : Math.max(props.metrics.propertyRowHeight, plain.y + plain.height)

                    Text {
                        id: plain

                        visible: row.values.length === 0
                        width: valueArea.width
                        y: props.metrics.baselineInBox(props.metrics.fmProperty, props.metrics.propertyRowHeight) - baselineOffset
                        text: row.modelData.value || ""
                        textFormat: Text.PlainText
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        font.family: props.metrics.textFamily
                        font.pointSize: props.metrics.pt(props.metrics.propertyFontSize)
                        color: props.metrics.textColor
                    }

                    Flow {
                        id: pills

                        visible: row.values.length > 0
                        width: valueArea.width
                        spacing: props.metrics.propertyIconGap
                        topPadding: (props.metrics.propertyRowHeight - props.metrics.lhBody) / 2
                        bottomPadding: topPadding

                        Repeater {
                            model: row.values

                            delegate: Rectangle {
                                id: pill

                                required property var modelData

                                width: Math.min(pillText.implicitWidth + 2 * props.metrics.tagPadX, pills.width)
                                height: props.metrics.lhBody
                                radius: props.metrics.tagRadius
                                color: props.metrics.codeBackgroundColor

                                Text {
                                    id: pillText

                                    x: props.metrics.tagPadX
                                    width: pill.width - 2 * props.metrics.tagPadX
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: String(pill.modelData)
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                    font.family: props.metrics.textFamily
                                    font.pointSize: props.metrics.pt(props.metrics.propertyFontSize)
                                    color: props.metrics.textColor
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    function dumpGeometry(out: var, origin: Item, extra: var) {
        out.push(props.metrics.geometryEntry("properties", props.block, props, origin, {
            fontPx: props.metrics.propertyFontSize, lineHeightPx: props.metrics.propertyRowHeight
        }));
    }
}
