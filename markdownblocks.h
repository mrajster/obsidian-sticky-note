// SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QString>
#include <QVariantList>

/**
 * Pure. Raw markdown -> flat ordered list of render blocks (QVariantMap each).
 * Uses TaskMarkdown::Scan::scanBlocks() for frontmatter/fence/indented-code
 * decisions, so a block with kind=="task" && toggleable==true exists for line i
 * IFF TaskMarkdown::isTaskLineInDocument(raw, i). Unit-tested for that invariant.
 */
namespace MarkdownBlocks
{
QVariantList parse(const QString &raw);
/** Inline markdown -> Qt rich-text subset with %PLACEHOLDERS% (see §2.3). */
QString inlineHtml(const QString &inlineSource);
}
