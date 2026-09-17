/*
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>
#include <QStringList>

/**
 * Pure functions. No QObject, no I/O, no QTextDocument. Unit-tested by tests/tst_taskmarkdown.cpp.
 */
namespace TaskMarkdown
{
/** The URL scheme used for in-render click targets. Value: "obsnote". */
QString linkScheme();

/**
 * Transform raw source markdown into the markdown string handed to
 * Text { textFormat: Text.MarkdownText }. See contract §7 for the exact algorithm.
 * Pure: same input -> same output, no state.
 */
QString render(const QString &raw);

/** True if @p line is a GFM task-list item ("  - [ ] x", "* [X] y", "1. [ ] z"). */
bool isTaskLine(const QString &line);

/** True if @p line is a task line AND it is checked ([x] or [X]). */
bool isTaskChecked(const QString &line);

/**
 * True only if line @p lineIndex of the whole document @p raw is a task that
 * render() would actually turn into a clickable checkbox: fence-aware,
 * indented-code-aware and frontmatter-aware, using the same single block scan the
 * renderer uses. This is the check a toggle must pass before touching the file --
 * isTaskLine() alone cannot tell a real task from "- [ ]" inside a code fence.
 */
bool isTaskLineInDocument(const QString &raw, int lineIndex);

/**
 * Flip the checkbox of @p line in place, touching exactly one character.
 * Returns false and leaves @p line untouched when it is not a task line.
 */
bool toggleTaskLine(QString &line);

/** "obsnote:toggle/12" -> 12; anything else -> -1. */
int parseToggleLink(const QString &link);

/** "obsnote:wiki/Some%20Note" -> "Some Note"; anything else -> QString(). */
QString parseWikiLink(const QString &link);

/**
 * Split @p text on '\n' keeping every other byte (so '\r' stays at line end and a
 * trailing newline survives join). splitLines()/joinLines() are exact inverses.
 */
QStringList splitLines(const QString &text);
QString joinLines(const QStringList &lines);
}
