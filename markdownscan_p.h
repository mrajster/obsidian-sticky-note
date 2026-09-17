// SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

/**
 * Private block-level scanner shared by TaskMarkdown (render + toggle validation)
 * and MarkdownBlocks (the block model). There is exactly ONE implementation of
 * the frontmatter / fence / indented-code decisions, so what renders as a
 * clickable checkbox and what a toggle accepts can never drift apart.
 */
namespace TaskMarkdown::Scan
{
/** A position inside a line: the byte index plus the column that index sits at. */
struct Cursor {
    qsizetype idx = 0;
    int col = 0;
};

/** The line with a trailing '\r' removed (CRLF documents behave like LF ones). */
QStringView lineCore(const QString &line);
bool isBlank(QStringView core);
/** Trailing spaces and tabs removed. */
QStringView rtrimmed(QStringView v);
/** Advance @p c past spaces and tabs, expanding tabs to four-column stops. */
void skipSpaces(QStringView core, Cursor &c);
/** A CommonMark thematic break ("---", "* * *", "___"), at most three columns of indent. */
bool isThematicBreak(QStringView core);
/**
 * If a list marker starts exactly at @p c, consume it plus the whitespace after it
 * and return the item's content column. Returns -1 and leaves @p c alone otherwise.
 */
int consumeListMarker(QStringView core, Cursor &c);

struct FenceOpen {
    bool ok = false;
    QChar ch;
    qsizetype len = 0;
    int indent = 0;
};
/** An opening code fence starting exactly at @p c. */
FenceOpen matchFenceOpen(QStringView core, const Cursor &c);
/** A closing fence for an opener of @p ch x @p len at column @p openIndent. */
bool closesFence(QStringView core, QChar ch, qsizetype len, int openIndent);
/** Index of the closing frontmatter delimiter, or -1 when there is no frontmatter. */
qsizetype frontmatterEnd(const QStringList &lines);

enum class LineCode : quint8 {
    Prose = 0,
    FenceOpen = 1,
    FenceBody = 2,
    FenceClose = 3,
    Indented = 4,
};

/** The result of one block-level pass over a document. */
struct BlockScan {
    qsizetype bodyStart = 0; //!< first line index that is not hidden frontmatter
    qsizetype frontmatterEnd = -1; //!< index of the closing ---/..., -1 = none
    QList<bool> isCode; //!< per source line: emit verbatim, never transform
    QList<LineCode> code; //!< per source line: finer classification, same pass
};
BlockScan scanBlocks(const QStringList &lines);
}
