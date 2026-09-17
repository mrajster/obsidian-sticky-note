/*
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "taskmarkdown.h"
#include "markdownscan_p.h"

#include <QLatin1StringView>
#include <QRegularExpression>
#include <QStringView>
#include <QUrl>

using namespace TaskMarkdown::Scan;

namespace
{

/**
 * A GFM task-list item: optional indent, a bullet ("-", "*", "+") or an ordered
 * marker ("1.", "1)"), whitespace, then the checkbox. The lookahead makes
 * "- [ ]" with nothing after it a task line, while rejecting "- [x]y".
 * Capture 1 is the whole prefix (indent + marker + spacing), capture 2 the
 * single character inside the brackets.
 */
const QRegularExpression &taskRe()
{
    static const QRegularExpression re(QStringLiteral(R"(^([ \t]*(?:[-*+]|[0-9]{1,9}[.)])[ \t]+)\[([ xX])\](?=[ \t]|$))"));
    return re;
}

/**
 * Rewrite [[Wikilinks]] outside of inline code spans. Everything else -- including
 * the trailing '\r' of a CRLF line -- is copied through byte for byte.
 */
QString inlineTransform(const QString &text)
{
    QString out;
    out.reserve(text.size() + 16);

    const qsizetype n = text.size();
    qsizetype p = 0;
    while (p < n) {
        const QChar c = text.at(p);

        if (c == QLatin1Char('`')) {
            // A run of n backticks opens a code span closed by a run of exactly n.
            const qsizetype runStart = p;
            while (p < n && text.at(p) == QLatin1Char('`')) {
                ++p;
            }
            const qsizetype runLen = p - runStart;
            qsizetype q = p;
            qsizetype spanEnd = -1;
            while (q < n) {
                if (text.at(q) != QLatin1Char('`')) {
                    ++q;
                    continue;
                }
                qsizetype r = q;
                while (r < n && text.at(r) == QLatin1Char('`')) {
                    ++r;
                }
                if (r - q == runLen) {
                    spanEnd = r;
                    break;
                }
                q = r;
            }
            if (spanEnd >= 0) {
                out += QStringView(text).mid(runStart, spanEnd - runStart);
                p = spanEnd;
            } else {
                // Unbalanced: the backticks are literal text, keep scanning after them.
                out += QStringView(text).mid(runStart, runLen);
            }
            continue;
        }

        if (c == QLatin1Char('[') && p + 1 < n && text.at(p + 1) == QLatin1Char('[')) {
            const qsizetype close = text.indexOf(QStringLiteral("]]"), p + 2);
            if (close >= p + 2) {
                const QString inner = text.mid(p + 2, close - (p + 2));
                const qsizetype bar = inner.indexOf(QLatin1Char('|'));
                const QString target = bar >= 0 ? inner.left(bar) : inner;
                // An Obsidian EMBED ("![[Note]]") must not become a Markdown image:
                // Text.MarkdownText would try to FETCH "obsnote:..." as an image
                // source and log 'Protocol "obsnote" is unknown' for every embed.
                // Render it as an ordinary link instead, labelled with the target
                // (the part after '|' on an embed is a size hint like "300x200",
                // which would make nonsense link text).
                const bool isEmbed = !out.isEmpty() && out.endsWith(QLatin1Char('!'));
                const QString display = isEmbed ? target : (bar >= 0 ? inner.mid(bar + 1) : inner);
                if (!target.isEmpty()) {
                    if (isEmbed) {
                        out.chop(1); // drop the '!' so this is a link, not an image
                    }
                    out += QLatin1Char('[');
                    out += display;
                    out += QLatin1String("](");
                    out += TaskMarkdown::linkScheme();
                    out += QLatin1String(":wiki/");
                    out += QString::fromLatin1(QUrl::toPercentEncoding(target));
                    out += QLatin1Char(')');
                    p = close + 2;
                    continue;
                }
            }
        }

        out += c;
        ++p;
    }

    return out;
}

} // namespace

namespace TaskMarkdown::Scan
{

/** CommonMark measures indentation in columns; a tab advances to the next stop. */
namespace
{
constexpr int kTabStop = 4;
}

/** A position inside a line: the byte index plus the column that index sits at. */

/**
 * Everything in this file matches against the line with its trailing carriage
 * return removed, so CRLF documents behave exactly like LF ones. The CR itself
 * is never dropped from the emitted text.
 */
QStringView lineCore(const QString &line)
{
    QStringView v(line);
    if (v.endsWith(QLatin1Char('\r'))) {
        v.chop(1);
    }
    return v;
}

bool isBlank(QStringView core)
{
    return core.trimmed().isEmpty();
}

/** Trailing spaces and tabs never change what a delimiter line means. */
QStringView rtrimmed(QStringView v)
{
    while (!v.isEmpty() && (v.back() == QLatin1Char(' ') || v.back() == QLatin1Char('\t'))) {
        v.chop(1);
    }
    return v;
}

/** Advance @p c past spaces and tabs, expanding tabs to four-column stops. */
void skipSpaces(QStringView core, Cursor &c)
{
    while (c.idx < core.size()) {
        const QChar ch = core.at(c.idx);
        if (ch == QLatin1Char(' ')) {
            ++c.col;
        } else if (ch == QLatin1Char('\t')) {
            c.col += kTabStop - (c.col % kTabStop);
        } else {
            break;
        }
        ++c.idx;
    }
}

/**
 * A CommonMark thematic break: at most three columns of indent, then three or
 * more of the same character out of '-', '*' and '_', with spaces or tabs
 * allowed between them and nothing else on the line.
 */
bool isThematicBreak(QStringView core)
{
    Cursor c;
    skipSpaces(core, c);
    if (c.col > 3 || c.idx >= core.size()) {
        return false;
    }
    const QChar ch = core.at(c.idx);
    if (ch != QLatin1Char('-') && ch != QLatin1Char('*') && ch != QLatin1Char('_')) {
        return false;
    }
    int run = 0;
    for (qsizetype i = c.idx; i < core.size(); ++i) {
        const QChar x = core.at(i);
        if (x == ch) {
            ++run;
        } else if (x != QLatin1Char(' ') && x != QLatin1Char('\t')) {
            return false;
        }
    }
    return run >= 3;
}

/**
 * If a list marker ("-", "*", "+", "1.", "1)") starts exactly at @p c, consume it
 * together with the whitespace after it and return the column where the item's
 * CONTENT begins -- the indent every continuation line of that item, including a
 * nested code fence, is measured against. Returns -1 and leaves @p c alone
 * otherwise, so "***", "---" and "-[ ]" are never mistaken for list items.
 */
int consumeListMarker(QStringView core, Cursor &c)
{
    if (c.idx >= core.size()) {
        return -1;
    }
    Cursor m = c;
    const QChar first = core.at(m.idx);
    if (first == QLatin1Char('-') || first == QLatin1Char('*') || first == QLatin1Char('+')) {
        ++m.idx;
        ++m.col;
    } else if (first >= QLatin1Char('0') && first <= QLatin1Char('9')) {
        int digits = 0;
        while (m.idx < core.size() && core.at(m.idx) >= QLatin1Char('0') && core.at(m.idx) <= QLatin1Char('9')) {
            ++m.idx;
            ++m.col;
            if (++digits > 9) {
                return -1; // CommonMark allows at most nine digits
            }
        }
        if (m.idx >= core.size()) {
            return -1;
        }
        const QChar delim = core.at(m.idx);
        if (delim != QLatin1Char('.') && delim != QLatin1Char(')')) {
            return -1;
        }
        ++m.idx;
        ++m.col;
    } else {
        return -1;
    }

    const int markerEnd = m.col;
    if (m.idx >= core.size()) {
        c = m; // "-" alone: an empty list item
        return markerEnd + 1;
    }
    const QChar next = core.at(m.idx);
    if (next != QLatin1Char(' ') && next != QLatin1Char('\t')) {
        return -1;
    }
    Cursor afterWs = m;
    skipSpaces(core, afterWs);
    if (afterWs.idx >= core.size()) {
        c = m; // "-   " with nothing after it
        return markerEnd + 1;
    }
    // More than four columns of padding starts an indented code block INSIDE the
    // item, so the item's content column is one past the marker, not where the
    // first non-space character happens to sit.
    const int content = (afterWs.col - markerEnd > kTabStop) ? markerEnd + 1 : afterWs.col;
    c = afterWs;
    return content;
}


/**
 * An opening code fence starting exactly at @p c: three or more backticks or
 * tildes. A backtick fence may not carry a backtick anywhere in its info string
 * (CommonMark), which is what stops a prose line full of inline code spans from
 * opening a block. A tilde fence's info string is unrestricted.
 */
FenceOpen matchFenceOpen(QStringView core, const Cursor &c)
{
    FenceOpen f;
    if (c.idx >= core.size()) {
        return f;
    }
    const QChar ch = core.at(c.idx);
    if (ch != QLatin1Char('`') && ch != QLatin1Char('~')) {
        return f;
    }
    qsizetype i = c.idx;
    while (i < core.size() && core.at(i) == ch) {
        ++i;
    }
    const qsizetype run = i - c.idx;
    if (run < 3) {
        return f;
    }
    if (ch == QLatin1Char('`')) {
        for (qsizetype k = i; k < core.size(); ++k) {
            if (core.at(k) == QLatin1Char('`')) {
                return f;
            }
        }
    }
    f.ok = true;
    f.ch = ch;
    f.len = run;
    f.indent = c.col;
    return f;
}

/**
 * A closing fence: the same character as the opener, repeated at least as many
 * times, indented at most three columns past the opener, then nothing but blanks.
 * An info string is never allowed on a closing fence.
 */
bool closesFence(QStringView core, QChar ch, qsizetype len, int openIndent)
{
    Cursor c;
    skipSpaces(core, c);
    if (c.col > openIndent + 3) {
        return false;
    }
    qsizetype i = c.idx;
    qsizetype run = 0;
    while (i < core.size() && core.at(i) == ch) {
        ++i;
        ++run;
    }
    if (run < len) {
        return false;
    }
    while (i < core.size()) {
        const QChar x = core.at(i);
        if (x != QLatin1Char(' ') && x != QLatin1Char('\t')) {
            return false;
        }
        ++i;
    }
    return true;
}

/**
 * The index of the closing delimiter of a YAML frontmatter block, or -1 when the
 * document does not open with one.
 *
 * "---" is frontmatter ONLY when it is the very first line of the document, the
 * line right after it is neither blank nor itself a thematic break (that shape is
 * a horizontal rule followed by prose, not a metadata block), and a closing "---"
 * or "..." actually exists. Everything else -- "***", "___", a "---" anywhere but
 * line one -- is a thematic break and must be rendered, never swallowed.
 */
qsizetype frontmatterEnd(const QStringList &lines)
{
    if (lines.size() < 2) {
        return -1;
    }
    if (rtrimmed(lineCore(lines.at(0))) != QLatin1String("---")) {
        return -1;
    }
    const QStringView second = lineCore(lines.at(1));
    if (isBlank(second) || isThematicBreak(second)) {
        return -1;
    }
    for (qsizetype k = 1; k < lines.size(); ++k) {
        const QStringView core = rtrimmed(lineCore(lines.at(k)));
        if (core == QLatin1String("---") || core == QLatin1String("...")) {
            return k;
        }
    }
    return -1;
}

/** The result of one block-level pass over a document. */

/**
 * One CommonMark-shaped block pass. It tracks open list items so that the two
 * indent-sensitive constructs are measured against the right column:
 *
 *  - a code fence may be indented up to three columns past its container's
 *    content column, so a fence nested in a list item can sit far past column 3;
 *  - an indented code block needs four columns past that same content column, so
 *    a continuation paragraph of a list item is prose, not code.
 */
BlockScan scanBlocks(const QStringList &lines)
{
    BlockScan scan;
    scan.isCode.fill(false, lines.size());
    scan.code.fill(LineCode::Prose, lines.size());

    const qsizetype fmEnd = frontmatterEnd(lines);
    scan.frontmatterEnd = fmEnd;
    if (fmEnd >= 0) {
        scan.bodyStart = fmEnd + 1;
        while (scan.bodyStart < lines.size() && isBlank(lineCore(lines.at(scan.bodyStart)))) {
            ++scan.bodyStart;
        }
    }

    bool inFence = false;
    QChar fenceChar;
    qsizetype fenceLen = 0;
    int fenceIndent = 0;

    bool inIndentedCode = false;
    bool prevWasBlank = true; // the start of the document starts a block
    QList<int> listStack; // content column of every open list item, outermost first

    for (qsizetype i = scan.bodyStart; i < lines.size(); ++i) {
        const QStringView core = lineCore(lines.at(i));
        const bool blank = isBlank(core);

        // (a) Fenced code: nothing inside is ever transformed, and only a fence of
        //     the same character and at least the same length closes it.
        if (inFence) {
            scan.isCode[i] = true;
            scan.code[i] = LineCode::FenceBody;
            if (!blank && closesFence(core, fenceChar, fenceLen, fenceIndent)) {
                scan.code[i] = LineCode::FenceClose;
                inFence = false;
                fenceLen = 0;
            }
            prevWasBlank = blank;
            continue;
        }
        if (blank) {
            // A blank line ends a paragraph but closes neither a list item nor an
            // indented code block, so neither piece of state is touched here.
            prevWasBlank = true;
            continue;
        }

        Cursor c;
        skipSpaces(core, c);
        while (!listStack.isEmpty() && c.col < listStack.last()) {
            listStack.removeLast();
        }
        const int base = listStack.isEmpty() ? 0 : listStack.last();

        // (b) Indented code block, measured from the enclosing item's content column.
        if (c.col >= base + 4 && (inIndentedCode || prevWasBlank)) {
            inIndentedCode = true;
            scan.isCode[i] = true;
            scan.code[i] = LineCode::Indented;
            prevWasBlank = false;
            continue;
        }
        inIndentedCode = false;
        prevWasBlank = false;

        // (c) Any list markers this line opens (there can be several: "- - item").
        while (true) {
            Cursor probe = c;
            const int content = consumeListMarker(core, probe);
            if (content < 0) {
                break;
            }
            listStack.append(content);
            c = probe;
        }

        // (d) An opening fence, relative to whatever container we are now in.
        const int fenceBase = listStack.isEmpty() ? 0 : listStack.last();
        if (c.col <= fenceBase + 3) {
            const FenceOpen f = matchFenceOpen(core, c);
            if (f.ok) {
                inFence = true;
                fenceChar = f.ch;
                fenceLen = f.len;
                fenceIndent = f.indent;
                scan.isCode[i] = true;
                scan.code[i] = LineCode::FenceOpen;
            }
        }
    }

    return scan;
}

} // namespace TaskMarkdown::Scan

namespace TaskMarkdown
{

QString linkScheme()
{
    return QStringLiteral("obsnote");
}

QStringList splitLines(const QString &text)
{
    return text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
}

QString joinLines(const QStringList &lines)
{
    return lines.join(QLatin1Char('\n'));
}

bool isTaskLine(const QString &line)
{
    const QString core = lineCore(line).toString();
    return taskRe().match(core).hasMatch();
}

bool isTaskChecked(const QString &line)
{
    const QString core = lineCore(line).toString();
    const QRegularExpressionMatch m = taskRe().match(core);
    return m.hasMatch() && m.capturedView(2) != QLatin1String(" ");
}

bool toggleTaskLine(QString &line)
{
    const QString core = lineCore(line).toString();
    const QRegularExpressionMatch m = taskRe().match(core);
    if (!m.hasMatch()) {
        return false;
    }
    const qsizetype pos = m.capturedStart(2);
    // Exactly one character changes; every other byte of the line is untouched.
    line[pos] = m.capturedView(2) == QLatin1String(" ") ? QLatin1Char('x') : QLatin1Char(' ');
    return true;
}

int parseToggleLink(const QString &link)
{
    const QString prefix = linkScheme() + QLatin1String(":toggle/");
    if (!link.startsWith(prefix)) {
        return -1;
    }
    const QStringView number = QStringView(link).mid(prefix.size());
    if (number.isEmpty()) {
        return -1;
    }
    for (const QChar c : number) {
        if (c < QLatin1Char('0') || c > QLatin1Char('9')) {
            return -1;
        }
    }
    bool ok = false;
    const int value = number.toInt(&ok);
    return ok ? value : -1;
}

QString parseWikiLink(const QString &link)
{
    const QString prefix = linkScheme() + QLatin1String(":wiki/");
    if (!link.startsWith(prefix)) {
        return QString();
    }
    const QString encoded = link.mid(prefix.size());
    if (encoded.isEmpty()) {
        return QString();
    }
    return QUrl::fromPercentEncoding(encoded.toUtf8());
}

bool isTaskLineInDocument(const QString &raw, int lineIndex)
{
    if (lineIndex < 0) {
        return false;
    }
    const QStringList lines = splitLines(raw);
    if (lineIndex >= lines.size()) {
        return false;
    }
    const BlockScan scan = scanBlocks(lines);
    if (lineIndex < scan.bodyStart || scan.isCode.at(lineIndex)) {
        return false;
    }
    return isTaskLine(lines.at(lineIndex));
}

QString render(const QString &raw)
{
    const QStringList lines = splitLines(raw);
    // One block-level pass decides what is code and what is hidden frontmatter;
    // render() only ever rewrites the lines that pass says are ordinary prose.
    const BlockScan scan = scanBlocks(lines);

    QStringList out;
    out.reserve(lines.size());

    for (qsizetype i = scan.bodyStart; i < lines.size(); ++i) {
        const QString &line = lines.at(i);

        // Fences, their contents and indented code travel byte for byte.
        if (scan.isCode.at(i)) {
            out.append(line);
            continue;
        }

        const QString core = lineCore(line).toString();

        // Task line: the source line index travels to QML inside the href.
        const QRegularExpressionMatch task = taskRe().match(core);
        if (task.hasMatch()) {
            const QChar glyph = task.capturedView(2) == QLatin1String(" ") ? QChar(0x2610) : QChar(0x2611);
            QString rewritten = task.captured(1);
            rewritten += QLatin1Char('[');
            rewritten += glyph;
            rewritten += QLatin1String("](");
            rewritten += linkScheme();
            rewritten += QLatin1String(":toggle/");
            rewritten += QString::number(i);
            rewritten += QLatin1Char(')');
            // capturedEnd(0) indexes into core, which shares its prefix with line,
            // so the trailing '\r' of a CRLF line rides along in the remainder.
            rewritten += inlineTransform(line.mid(task.capturedEnd(0)));
            out.append(rewritten);
            continue;
        }

        // Everything else.
        out.append(inlineTransform(line));
    }

    return joinLines(out);
}

} // namespace TaskMarkdown
