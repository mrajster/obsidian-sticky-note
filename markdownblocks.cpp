// SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "markdownblocks.h"

#include "markdownscan_p.h"
#include "taskmarkdown.h"

#include <QHash>
#include <QRegularExpression>
#include <QUrl>
#include <QVariantMap>

using namespace TaskMarkdown::Scan;

namespace
{

// ===========================================================================
// Inline: markdown -> Qt rich-text subset
// ===========================================================================

/**
 * Collects the rich text for one paragraph together with the plain text a
 * QTextDocument will hold after setHtml(), so decoration offsets are exact
 * QTextDocument character positions.
 *
 * Whitespace is collapsed here exactly like a browser does (and like Qt's HTML
 * importer would), so the counted positions cannot drift from what Qt builds:
 * runs of blanks become one space, nothing leads a line, nothing trails one.
 */
class Emitter
{
public:
    QString html;
    QString plain;
    QVariantList decorations;
    /** ==highlight== ranges ({start, length}, document positions); the view paints them. */
    QVariantList marks;

    void tag(QLatin1StringView s)
    {
        html += s;
    }
    void tag(const QString &s)
    {
        html += s;
    }

    void text(QChar c)
    {
        if (c == QLatin1Char(' ') || c == QLatin1Char('\t') || c == QLatin1Char('\n') || c == QLatin1Char('\r')) {
            if (m_lastSpace) {
                return;
            }
            m_lastSpace = true;
            html += QLatin1Char(' ');
            plain += QLatin1Char(' ');
            return;
        }
        m_lastSpace = false;
        switch (c.unicode()) {
        case '&':
            html += QLatin1String("&amp;");
            break;
        case '<':
            html += QLatin1String("&lt;");
            break;
        case '>':
            html += QLatin1String("&gt;");
            break;
        case '"':
            html += QLatin1String("&quot;");
            break;
        case '%': // never let user text form a %PLACEHOLDER%
            html += QLatin1String("&#37;");
            break;
        default:
            html += c;
        }
        plain += c;
    }

    void text(QStringView s)
    {
        for (const QChar c : s) {
            text(c);
        }
    }

    void lineBreak()
    {
        chopTrailingSpace();
        html += QLatin1String("<br>");
        plain += QChar(0x2028); // QTextDocument's line separator: one position
        m_lastSpace = true;
    }

    /** The zero-width space whose letter-spacing is a pill's side padding. */
    void pad(QLatin1StringView placeholder)
    {
        html += QLatin1String("<span style=\"letter-spacing:") + placeholder + QLatin1String("px\">&#8288;</span>");
        plain += QChar(0x2060);
        m_lastSpace = false;
    }

    void finish()
    {
        chopTrailingSpace();
    }

    qsizetype pos() const
    {
        return plain.size();
    }

private:
    void chopTrailingSpace()
    {
        if (plain.endsWith(QLatin1Char(' ')) && html.endsWith(QLatin1Char(' '))) {
            plain.chop(1);
            html.chop(1);
        }
    }

    bool m_lastSpace = true;
};

QString escapeAttr(const QString &s)
{
    QString out;
    out.reserve(s.size() + 8);
    for (const QChar c : s) {
        switch (c.unicode()) {
        case '&':
            out += QLatin1String("&amp;");
            break;
        case '<':
            out += QLatin1String("&lt;");
            break;
        case '>':
            out += QLatin1String("&gt;");
            break;
        case '"':
            out += QLatin1String("&quot;");
            break;
        case '%':
            out += QLatin1String("&#37;");
            break;
        default:
            out += c;
        }
    }
    return out;
}

bool isAsciiPunct(QChar c)
{
    static const QString punct = QStringLiteral("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~");
    return c.unicode() < 128 && punct.contains(c);
}

bool isWs(QChar c)
{
    return c.isSpace();
}

bool isTagChar(QChar c)
{
    return c.isLetterOrNumber() || c.isMark() || c == QLatin1Char('_') || c == QLatin1Char('-') || c == QLatin1Char('/');
}

QString wikiHref(const QString &target)
{
    return TaskMarkdown::linkScheme() + QLatin1String(":wiki/") + QString::fromLatin1(QUrl::toPercentEncoding(target));
}

class InlineParser
{
public:
    InlineParser(const QString &s, Emitter &e)
        : m_s(s)
        , m_e(e)
    {
    }

    void parse(qsizetype from, qsizetype to)
    {
        qsizetype i = from;
        while (i < to) {
            i = step(i, from, to);
        }
    }

private:
    QChar at(qsizetype k) const
    {
        return (k >= 0 && k < m_s.size()) ? m_s.at(k) : QChar();
    }

    qsizetype runLength(qsizetype k, qsizetype to) const
    {
        const QChar c = at(k);
        qsizetype r = 0;
        while (k + r < to && m_s.at(k + r) == c) {
            ++r;
        }
        return r;
    }

    /** End (exclusive) of the code span opening at @p k, or -1. */
    qsizetype codeSpanEnd(qsizetype k, qsizetype to, qsizetype *openLen = nullptr) const
    {
        const qsizetype len = runLength(k, to);
        if (openLen) {
            *openLen = len;
        }
        qsizetype q = k + len;
        while (q < to) {
            if (m_s.at(q) != QLatin1Char('`')) {
                ++q;
                continue;
            }
            const qsizetype r = runLength(q, to);
            if (r == len) {
                return q + r;
            }
            q += r;
        }
        return -1;
    }

    /** Index of the ']' matching the '[' at @p k (nesting, escapes, code aware), or -1. */
    qsizetype matchBracket(qsizetype k, qsizetype to) const
    {
        int depth = 0;
        for (qsizetype q = k; q < to; ++q) {
            const QChar c = m_s.at(q);
            if (c == QLatin1Char('\\')) {
                ++q;
            } else if (c == QLatin1Char('`')) {
                const qsizetype end = codeSpanEnd(q, to);
                if (end > 0) {
                    q = end - 1;
                } else {
                    q += runLength(q, to) - 1;
                }
            } else if (c == QLatin1Char('[')) {
                ++depth;
            } else if (c == QLatin1Char(']')) {
                if (--depth == 0) {
                    return q;
                }
            }
        }
        return -1;
    }

    /**
     * The closing delimiter for an emphasis-like run @p delim opened before
     * @p from. Returns the index the closer starts at, or -1.
     */
    qsizetype findCloser(const QString &delim, qsizetype from, qsizetype to) const
    {
        const qsizetype len = delim.size();
        const QChar dc = delim.at(0);
        const bool underscore = dc == QLatin1Char('_');
        qsizetype k = from;
        while (k < to) {
            const QChar c = m_s.at(k);
            if (c == QLatin1Char('\\')) {
                k += 2;
                continue;
            }
            if (c == QLatin1Char('`')) {
                const qsizetype end = codeSpanEnd(k, to);
                k = end > 0 ? end : k + runLength(k, to);
                continue;
            }
            if (c == QLatin1Char('[') && at(k + 1) == QLatin1Char('[')) {
                const qsizetype close = m_s.indexOf(QLatin1String("]]"), k + 2);
                if (close > 0 && close + 2 <= to) {
                    k = close + 2;
                    continue;
                }
            }
            if (c != dc) {
                ++k;
                continue;
            }
            const qsizetype r = runLength(k, to);
            if (r < len || (len == 1 && r == 2) || (len == 2 && dc != QLatin1Char('*') && dc != QLatin1Char('_') && r != 2)) {
                k += r;
                continue;
            }
            const qsizetype closer = k + r - len;
            const bool nonEmpty = closer > from;
            const bool leftOk = nonEmpty && !isWs(m_s.at(closer - 1));
            const bool rightOk = !underscore || closer + len >= to || !m_s.at(closer + len).isLetterOrNumber();
            if (leftOk && rightOk) {
                return closer;
            }
            k += r;
        }
        return -1;
    }

    void emitCode(QStringView content)
    {
        QString body = content.toString();
        body.replace(QLatin1Char('\n'), QLatin1Char(' '));
        if (body.size() >= 2 && body.startsWith(QLatin1Char(' ')) && body.endsWith(QLatin1Char(' ')) && !body.trimmed().isEmpty()) {
            body = body.mid(1, body.size() - 2);
        }
        m_e.pad(QLatin1StringView("%CODEPADX%"));
        m_e.tag(QLatin1StringView("<span style=\"font-family:'%MONO%';font-size:%CODEPT%pt;color:%CODEFG%\">"));
        const qsizetype start = m_e.pos();
        m_e.text(QStringView(body));
        const qsizetype length = m_e.pos() - start;
        m_e.tag(QLatin1StringView("</span>"));
        m_e.pad(QLatin1StringView("%CODEPADX%"));
        m_e.decorations.append(QVariantMap{{QStringLiteral("type"), QStringLiteral("code")},
                                           {QStringLiteral("start"), int(start)},
                                           {QStringLiteral("length"), int(length)}});
    }

    void emitTag(const QString &tag)
    {
        m_e.tag(QLatin1String("<a href=\"") + escapeAttr(TaskMarkdown::linkScheme() + QLatin1String(":tag/") + QString::fromLatin1(QUrl::toPercentEncoding(tag.mid(1), "/")))
                + QLatin1String("\" style=\"text-decoration:none\">"));
        m_e.pad(QLatin1StringView("%TAGPADX%"));
        m_e.tag(QLatin1StringView("<span style=\"font-size:%TAGPT%pt;color:%TAGFG%\">"));
        const qsizetype start = m_e.pos();
        m_e.text(QStringView(tag));
        const qsizetype length = m_e.pos() - start;
        m_e.tag(QLatin1StringView("</span>"));
        m_e.pad(QLatin1StringView("%TAGPADX%"));
        m_e.tag(QLatin1StringView("</a>"));
        m_e.decorations.append(QVariantMap{{QStringLiteral("type"), QStringLiteral("tag")},
                                           {QStringLiteral("start"), int(start)},
                                           {QStringLiteral("length"), int(length)}});
    }

    void openLink(const QString &href)
    {
        m_e.tag(QLatin1String("<a href=\"") + escapeAttr(href) + QLatin1String("\" style=\"color:%LINK%\">"));
    }

    /** "Other%20Note.md" and other scheme-less destinations are internal links, as in Obsidian. */
    static QString linkHref(const QString &dest)
    {
        static const QRegularExpression scheme(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]{1,31}:"));
        if (dest.isEmpty() || scheme.match(dest).hasMatch() || dest.startsWith(QLatin1Char('#'))) {
            return dest;
        }
        QString target = QUrl::fromPercentEncoding(dest.toUtf8());
        if (target.endsWith(QLatin1String(".md"), Qt::CaseInsensitive)) {
            target.chop(3);
        }
        return wikiHref(target);
    }

    /** Parses "(dest "title")" starting at @p k (which must be '('). Returns end (exclusive) or -1. */
    qsizetype parseDestination(qsizetype k, qsizetype to, QString *dest) const
    {
        if (at(k) != QLatin1Char('(')) {
            return -1;
        }
        qsizetype q = k + 1;
        while (q < to && isWs(m_s.at(q))) {
            ++q;
        }
        QString d;
        if (at(q) == QLatin1Char('<')) {
            const qsizetype gt = m_s.indexOf(QLatin1Char('>'), q + 1);
            if (gt < 0 || gt >= to) {
                return -1;
            }
            d = m_s.mid(q + 1, gt - q - 1);
            q = gt + 1;
        } else {
            int parens = 0;
            const qsizetype s0 = q;
            while (q < to) {
                const QChar c = m_s.at(q);
                if (c == QLatin1Char('\\') && q + 1 < to) {
                    q += 2;
                    continue;
                }
                if (isWs(c)) {
                    break;
                }
                if (c == QLatin1Char('(')) {
                    ++parens;
                } else if (c == QLatin1Char(')')) {
                    if (parens == 0) {
                        break;
                    }
                    --parens;
                }
                ++q;
            }
            d = m_s.mid(s0, q - s0);
        }
        while (q < to && isWs(m_s.at(q))) {
            ++q;
        }
        const QChar tq = at(q);
        if (q < to && (tq == QLatin1Char('"') || tq == QLatin1Char('\'') || tq == QLatin1Char('('))) {
            const QChar close = tq == QLatin1Char('(') ? QLatin1Char(')') : tq;
            const qsizetype end = m_s.indexOf(close, q + 1);
            if (end < 0 || end >= to) {
                return -1;
            }
            q = end + 1;
            while (q < to && isWs(m_s.at(q))) {
                ++q;
            }
        }
        if (at(q) != QLatin1Char(')') || q >= to) {
            return -1;
        }
        *dest = d;
        return q + 1;
    }

    /** One inline construct (or one literal character) at @p i; returns the next index. */
    qsizetype step(qsizetype i, qsizetype from, qsizetype to)
    {
        const QChar c = m_s.at(i);
        const QChar n1 = i + 1 < to ? m_s.at(i + 1) : QChar();

        if (c == QLatin1Char('\\')) {
            if (n1 == QLatin1Char('\n')) {
                m_e.lineBreak();
                return i + 2;
            }
            if (i + 1 < to && isAsciiPunct(n1)) {
                m_e.text(n1);
                return i + 2;
            }
            m_e.text(c);
            return i + 1;
        }

        if (c == QLatin1Char('\n')) {
            m_e.lineBreak(); // Obsidian's default: a single newline is a line break
            return i + 1;
        }

        if (c == QLatin1Char('`')) {
            qsizetype openLen = 0;
            const qsizetype end = codeSpanEnd(i, to, &openLen);
            if (end < 0) {
                for (qsizetype k = 0; k < openLen; ++k) {
                    m_e.text(QLatin1Char('`'));
                }
                return i + openLen;
            }
            emitCode(QStringView(m_s).mid(i + openLen, end - openLen - (i + openLen)));
            return end;
        }

        if (c == QLatin1Char('%') && n1 == QLatin1Char('%')) {
            const qsizetype close = m_s.indexOf(QLatin1String("%%"), i + 2);
            if (close >= 0 && close + 2 <= to) {
                return close + 2; // Obsidian comment: hidden in reading view
            }
        }

        if (c == QLatin1Char('$')) {
            if (n1 == QLatin1Char('$')) {
                const qsizetype close = m_s.indexOf(QLatin1String("$$"), i + 2);
                if (close > i + 2 && close + 2 <= to) {
                    m_e.tag(QLatin1StringView("<i>"));
                    m_e.text(QStringView(m_s).mid(i + 2, close - i - 2).trimmed());
                    m_e.tag(QLatin1StringView("</i>"));
                    return close + 2;
                }
            } else if (i + 1 < to && !isWs(n1)) {
                for (qsizetype k = i + 1; k < to; ++k) {
                    const QChar x = m_s.at(k);
                    if (x == QLatin1Char('\\')) {
                        ++k;
                        continue;
                    }
                    if (x == QLatin1Char('\n')) {
                        break;
                    }
                    if (x == QLatin1Char('$') && !isWs(m_s.at(k - 1)) && !(k + 1 < to && m_s.at(k + 1).isDigit())) {
                        m_e.tag(QLatin1StringView("<i>"));
                        m_e.text(QStringView(m_s).mid(i + 1, k - i - 1));
                        m_e.tag(QLatin1StringView("</i>"));
                        return k + 1;
                    }
                }
            }
            m_e.text(c);
            return i + 1;
        }

        // [[wikilink]] and ![[embed]]
        const bool embed = c == QLatin1Char('!') && n1 == QLatin1Char('[') && at(i + 2) == QLatin1Char('[');
        if (embed || (c == QLatin1Char('[') && n1 == QLatin1Char('['))) {
            const qsizetype open = embed ? i + 3 : i + 2;
            const qsizetype close = m_s.indexOf(QLatin1String("]]"), open);
            if (close > open && close + 2 <= to && !QStringView(m_s).mid(open, close - open).contains(QLatin1Char('\n'))) {
                const QString inner = m_s.mid(open, close - open);
                const qsizetype bar = inner.indexOf(QLatin1Char('|'));
                const QString target = (bar >= 0 ? inner.left(bar) : inner).trimmed();
                if (!target.isEmpty()) {
                    QString display;
                    if (embed) {
                        display = target;
                    } else if (bar >= 0) {
                        display = inner.mid(bar + 1);
                    } else {
                        display = target;
                        if (display.startsWith(QLatin1Char('#'))) {
                            display = display.mid(1);
                        }
                        display.replace(QLatin1Char('#'), QLatin1String(" > "));
                    }
                    openLink(wikiHref(target));
                    m_e.text(QStringView(display));
                    m_e.tag(QLatin1StringView("</a>"));
                    return close + 2;
                }
            }
        }

        // ![alt](url) -> a link labelled with the alt text (never fetched)
        if (c == QLatin1Char('!') && n1 == QLatin1Char('[')) {
            const qsizetype close = matchBracket(i + 1, to);
            QString dest;
            const qsizetype end = close > 0 ? parseDestination(close + 1, to, &dest) : -1;
            if (end > 0) {
                openLink(linkHref(dest));
                if (close > i + 2) {
                    parse(i + 2, close);
                } else {
                    m_e.text(QStringView(dest));
                }
                m_e.tag(QLatin1StringView("</a>"));
                return end;
            }
        }

        // ^[inline footnote]
        if (c == QLatin1Char('^') && n1 == QLatin1Char('[')) {
            const qsizetype close = matchBracket(i + 1, to);
            if (close > 0) {
                m_e.tag(QLatin1StringView("<sup>"));
                m_e.text(QStringView(u"[*]"));
                m_e.tag(QLatin1StringView("</sup>"));
                return close + 1;
            }
        }

        if (c == QLatin1Char('[')) {
            const qsizetype close = matchBracket(i, to);
            if (close > 0) {
                if (n1 == QLatin1Char('^') && close > i + 2 && at(close + 1) != QLatin1Char('(')) {
                    m_e.tag(QLatin1StringView("<sup>"));
                    m_e.text(QLatin1Char('['));
                    m_e.text(QStringView(m_s).mid(i + 2, close - i - 2));
                    m_e.text(QLatin1Char(']'));
                    m_e.tag(QLatin1StringView("</sup>"));
                    return close + 1;
                }
                QString dest;
                const qsizetype end = parseDestination(close + 1, to, &dest);
                if (end > 0) {
                    openLink(linkHref(dest));
                    parse(i + 1, close);
                    m_e.tag(QLatin1StringView("</a>"));
                    return end;
                }
            }
            m_e.text(c);
            return i + 1;
        }

        if (c == QLatin1Char('<')) {
            const qsizetype gt = m_s.indexOf(QLatin1Char('>'), i + 1);
            if (gt > i + 1 && gt < to) {
                const QString inner = m_s.mid(i + 1, gt - i - 1);
                static const QRegularExpression uri(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]{1,31}:[^\\s<>]*$"));
                static const QRegularExpression mail(QStringLiteral("^[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?(?:\\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*$"));
                if (uri.match(inner).hasMatch() || mail.match(inner).hasMatch()) {
                    openLink(inner.contains(QLatin1Char('@')) && !inner.contains(QLatin1Char(':')) ? QLatin1String("mailto:") + inner : inner);
                    m_e.text(QStringView(inner));
                    m_e.tag(QLatin1StringView("</a>"));
                    return gt + 1;
                }
            }
            m_e.text(c); // raw HTML is shown literally
            return i + 1;
        }

        if ((c == QLatin1Char('h') || c == QLatin1Char('H')) && (i == from || !at(i - 1).isLetterOrNumber())) {
            const QStringView rest = QStringView(m_s).mid(i, to - i);
            if (rest.startsWith(QLatin1String("https://"), Qt::CaseInsensitive) || rest.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)) {
                qsizetype end = i;
                static const QString stops = QStringLiteral("<>\"");
                while (end < to && !isWs(m_s.at(end)) && !stops.contains(m_s.at(end))) {
                    ++end;
                }
                static const QString trailing = QStringLiteral(".,:;!?\"'*_~");
                while (end > i) {
                    const QChar last = m_s.at(end - 1);
                    if (trailing.contains(last)) {
                        --end;
                    } else if (last == QLatin1Char(')')) {
                        const QStringView url = QStringView(m_s).mid(i, end - i);
                        if (url.count(QLatin1Char(')')) > url.count(QLatin1Char('('))) {
                            --end;
                        } else {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                const QString url = m_s.mid(i, end - i);
                if (url.size() > 8) {
                    openLink(url);
                    m_e.text(QStringView(url));
                    m_e.tag(QLatin1StringView("</a>"));
                    return end;
                }
            }
        }

        if (c == QLatin1Char('#') && (i == from || isWs(at(i - 1)))) {
            qsizetype end = i + 1;
            bool nonDigit = false;
            while (end < to && isTagChar(m_s.at(end))) {
                if (!m_s.at(end).isDigit()) {
                    nonDigit = true;
                }
                ++end;
            }
            if (end > i + 1 && nonDigit) {
                emitTag(m_s.mid(i, end - i));
                return end;
            }
        }

        // Emphasis-like delimiters.
        struct Delim {
            QLatin1StringView token;
            QLatin1StringView open;
            QLatin1StringView close;
        };
        static const Delim delims[] = {
            {QLatin1StringView("**"), QLatin1StringView("<span style=\"font-weight:600\">"), QLatin1StringView("</span>")},
            {QLatin1StringView("__"), QLatin1StringView("<span style=\"font-weight:600\">"), QLatin1StringView("</span>")},
            {QLatin1StringView("~~"), QLatin1StringView("<s>"), QLatin1StringView("</s>")},
            {QLatin1StringView("=="), QLatin1StringView("<span style=\"background-color:%MARKBG%\">"), QLatin1StringView("</span>")},
            {QLatin1StringView("*"), QLatin1StringView("<i>"), QLatin1StringView("</i>")},
            {QLatin1StringView("_"), QLatin1StringView("<i>"), QLatin1StringView("</i>")},
        };
        if (c == QLatin1Char('*') || c == QLatin1Char('_') || c == QLatin1Char('~') || c == QLatin1Char('=')) {
            const qsizetype run = runLength(i, to);
            for (const Delim &d : delims) {
                const qsizetype len = d.token.size();
                if (c != d.token.at(0) || run < len) {
                    continue;
                }
                if (len == 1 && run == 2) {
                    continue;
                }
                const qsizetype innerStart = i + len;
                if (innerStart >= to || isWs(m_s.at(innerStart))) {
                    continue;
                }
                if (c == QLatin1Char('_') && i > from && at(i - 1).isLetterOrNumber()) {
                    continue;
                }
                const qsizetype closer = findCloser(QString(d.token), innerStart, to);
                if (closer < 0) {
                    continue;
                }
                const qsizetype openedAt = m_e.pos();
                m_e.tag(d.open);
                parse(innerStart, closer);
                m_e.tag(d.close);
                if (d.token == QLatin1StringView("==") && m_e.pos() > openedAt) {
                    m_e.marks.append(QVariantMap{{QStringLiteral("start"), int(openedAt)},
                                                 {QStringLiteral("length"), int(m_e.pos() - openedAt)}});
                }
                return closer + len;
            }
            // Unmatched: the whole run is literal.
            for (qsizetype k = 0; k < run; ++k) {
                m_e.text(c);
            }
            return i + run;
        }

        m_e.text(c);
        return i + 1;
    }

    const QString &m_s;
    Emitter &m_e;
};

struct Inline {
    QString html;
    QString text;
    QVariantList decorations;
    QVariantList marks;
};

/** Lines are trimmed and joined with '\n'; a trailing " ^block-id" is hidden. */
Inline renderInline(const QString &source)
{
    Emitter e;
    InlineParser p(source, e);
    p.parse(0, source.size());
    e.finish();
    Inline out;
    out.html = QLatin1String("<p style=\"margin:0\">") + e.html + QLatin1String("</p>");
    out.text = e.plain;
    out.text.remove(QChar(0x2060));
    out.text.replace(QChar(0x2028), QLatin1Char('\n'));
    out.text.replace(QChar(0x00A0), QLatin1Char(' '));
    out.decorations = e.decorations;
    out.marks = e.marks;
    return out;
}

QString joinInlineLines(const QStringList &lines)
{
    QStringList t;
    t.reserve(lines.size());
    for (const QString &l : lines) {
        t.append(l.trimmed());
    }
    while (!t.isEmpty() && t.first().isEmpty()) {
        t.removeFirst();
    }
    QString joined = t.join(QLatin1Char('\n'));
    static const QRegularExpression blockId(QStringLiteral("(?:^|[ \\t])\\^[A-Za-z0-9-]+[ \\t]*$"));
    joined.remove(blockId);
    return joined.trimmed();
}

// ===========================================================================
// Block structure
// ===========================================================================

struct Line {
    int src = 0;
    QString text; //!< no '\r'; leading whitespace expanded to spaces
    LineCode code = LineCode::Prose;
};

QString expandLeading(QStringView core)
{
    QString out;
    int col = 0;
    qsizetype i = 0;
    for (; i < core.size(); ++i) {
        const QChar c = core.at(i);
        if (c == QLatin1Char(' ')) {
            out += QLatin1Char(' ');
            ++col;
        } else if (c == QLatin1Char('\t')) {
            const int n = 4 - (col % 4);
            out += QString(n, QLatin1Char(' '));
            col += n;
        } else {
            break;
        }
    }
    out += core.mid(i);
    return out;
}

int indentOf(const QString &t)
{
    int k = 0;
    while (k < t.size() && t.at(k) == QLatin1Char(' ')) {
        ++k;
    }
    return k;
}

QString dedent(const QString &t, int n)
{
    return t.mid(qMin(n, indentOf(t)));
}

bool blank(const QString &t)
{
    return isBlank(QStringView(t));
}

struct Marker {
    bool ok = false;
    bool ordered = false;
    QChar bullet;
    QChar delim;
    int start = 1;
    int contentCol = 0;
    int indent = 0;
    QString firstContent;
    bool task = false;
    QChar taskChar;
    QString afterBox;
};

Marker listMarker(const QString &text)
{
    Marker mk;
    if (isThematicBreak(QStringView(text))) {
        return mk;
    }
    Cursor c;
    skipSpaces(QStringView(text), c);
    Cursor m = c;
    const int content = consumeListMarker(QStringView(text), m);
    if (content < 0) {
        return mk;
    }
    mk.ok = true;
    mk.indent = c.col;
    mk.contentCol = content;
    const QChar first = text.at(c.idx);
    if (first.isDigit()) {
        mk.ordered = true;
        qsizetype k = c.idx;
        while (k < text.size() && text.at(k).isDigit()) {
            ++k;
        }
        mk.start = QStringView(text).mid(c.idx, k - c.idx).toInt();
        mk.delim = text.at(k);
    } else {
        mk.bullet = first;
    }
    if (m.idx < text.size()) {
        mk.firstContent = QString(qMax(0, m.col - content), QLatin1Char(' ')) + text.mid(m.idx);
    }
    // Task box: "[c]" followed by blank or end of line, any single character.
    const QString lead = mk.firstContent.trimmed().isEmpty() ? QString() : mk.firstContent.mid(indentOf(mk.firstContent));
    if (lead.size() >= 3 && lead.at(0) == QLatin1Char('[') && lead.at(2) == QLatin1Char(']')
        && (lead.size() == 3 || lead.at(3) == QLatin1Char(' ') || lead.at(3) == QLatin1Char('\t'))) {
        mk.task = true;
        mk.taskChar = lead.at(1);
        mk.afterBox = lead.size() > 3 ? lead.mid(4) : QString();
    }
    return mk;
}

bool isSetextUnderline(const QString &t)
{
    const int ind = indentOf(t);
    if (ind > 3) {
        return false;
    }
    const QString s = t.mid(ind).trimmed();
    if (s.isEmpty()) {
        return false;
    }
    const QChar c = s.at(0);
    if (c != QLatin1Char('=') && c != QLatin1Char('-')) {
        return false;
    }
    for (const QChar x : s) {
        if (x != c) {
            return false;
        }
    }
    return true;
}

/** ATX heading: returns level (1-6) and fills @p content, or 0. */
int atxHeading(const QString &t, QString *content)
{
    const int ind = indentOf(t);
    if (ind > 3) {
        return 0;
    }
    const QString s = t.mid(ind);
    int r = 0;
    while (r < s.size() && s.at(r) == QLatin1Char('#')) {
        ++r;
    }
    if (r == 0 || r > 6) {
        return 0;
    }
    if (r < s.size() && s.at(r) != QLatin1Char(' ') && s.at(r) != QLatin1Char('\t')) {
        return 0;
    }
    QString c = s.mid(r).trimmed();
    // Optional closing sequence: " ###"
    qsizetype k = c.size();
    while (k > 0 && c.at(k - 1) == QLatin1Char('#')) {
        --k;
    }
    if (k == 0) {
        c.clear();
    } else if (k < c.size() && (c.at(k - 1) == QLatin1Char(' ') || c.at(k - 1) == QLatin1Char('\t'))) {
        c = c.left(k).trimmed();
    }
    if (content) {
        *content = c;
    }
    return r;
}

bool startsFenceText(const QString &t)
{
    Cursor c;
    skipSpaces(QStringView(t), c);
    return c.col <= 3 && matchFenceOpen(QStringView(t), c).ok;
}

/** Can @p t interrupt a paragraph (or end a lazy continuation)? */
bool interruptsParagraph(const Line &l)
{
    if (l.code != LineCode::Prose) {
        return true;
    }
    const QString &t = l.text;
    if (blank(t)) {
        return true;
    }
    const Marker mk = listMarker(t);
    if (mk.ok && mk.task) {
        return true; // a task line always starts an item (toggle invariant)
    }
    const int ind = indentOf(t);
    if (ind > 3) {
        return false;
    }
    const QString s = t.mid(ind);
    if (atxHeading(t, nullptr) || s.startsWith(QLatin1Char('>')) || isThematicBreak(QStringView(t)) || startsFenceText(t) || s.startsWith(QLatin1String("$$"))) {
        return true;
    }
    if (mk.ok && !blank(mk.firstContent) && (!mk.ordered || mk.start == 1)) {
        return true;
    }
    return false;
}

QStringList splitRow(const QString &t)
{
    QString s = t.trimmed();
    if (s.startsWith(QLatin1Char('|'))) {
        s.remove(0, 1);
    }
    if (s.endsWith(QLatin1Char('|')) && !s.endsWith(QLatin1String("\\|"))) {
        s.chop(1);
    }
    QStringList cells;
    QString cur;
    for (qsizetype k = 0; k < s.size(); ++k) {
        const QChar c = s.at(k);
        if (c == QLatin1Char('\\') && k + 1 < s.size() && s.at(k + 1) == QLatin1Char('|')) {
            cur += QLatin1Char('|');
            ++k;
            continue;
        }
        if (c == QLatin1Char('|')) {
            cells.append(cur.trimmed());
            cur.clear();
            continue;
        }
        cur += c;
    }
    cells.append(cur.trimmed());
    return cells;
}

bool isDelimiterRow(const QString &t, QStringList *align)
{
    if (!t.contains(QLatin1Char('-'))) {
        return false;
    }
    static const QRegularExpression cellRe(QStringLiteral("^:?-+:?$"));
    const QStringList cells = splitRow(t);
    QStringList a;
    for (const QString &c : cells) {
        if (!cellRe.match(c).hasMatch()) {
            return false;
        }
        const bool l = c.startsWith(QLatin1Char(':'));
        const bool r = c.endsWith(QLatin1Char(':'));
        a.append(l && r ? QStringLiteral("center") : r ? QStringLiteral("right") : QStringLiteral("left"));
    }
    if (align) {
        *align = a;
    }
    return true;
}

bool isTableStart(const QList<Line> &L, qsizetype k)
{
    if (k + 1 >= L.size()) {
        return false;
    }
    const Line &h = L.at(k);
    const Line &d = L.at(k + 1);
    if (h.code != LineCode::Prose || d.code != LineCode::Prose || blank(h.text) || blank(d.text) || indentOf(h.text) > 3 || indentOf(d.text) > 3) {
        return false;
    }
    if (!h.text.contains(QLatin1Char('|')) && !d.text.contains(QLatin1Char('|'))) {
        return false;
    }
    QStringList align;
    if (!isDelimiterRow(d.text, &align)) {
        return false;
    }
    return splitRow(h.text).size() == align.size();
}

QString unquote(const QString &v)
{
    const QString s = v.trimmed();
    if (s.size() >= 2 && ((s.startsWith(QLatin1Char('"')) && s.endsWith(QLatin1Char('"'))) || (s.startsWith(QLatin1Char('\'')) && s.endsWith(QLatin1Char('\''))))) {
        return s.mid(1, s.size() - 2);
    }
    return s;
}

QStringList splitFlowList(const QString &inner)
{
    QStringList out;
    QString cur;
    QChar quote;
    for (const QChar c : inner) {
        if (!quote.isNull()) {
            cur += c;
            if (c == quote) {
                quote = QChar();
            }
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            quote = c;
            cur += c;
        } else if (c == QLatin1Char(',')) {
            out.append(unquote(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.trimmed().isEmpty() || !out.isEmpty()) {
        out.append(unquote(cur));
    }
    return out;
}

QString capitalised(const QString &s)
{
    if (s.isEmpty()) {
        return s;
    }
    return s.left(1).toUpper() + s.mid(1);
}

QString normaliseCalloutType(const QString &t)
{
    static const QHash<QString, QString> aliases = {
        {QStringLiteral("summary"), QStringLiteral("abstract")}, {QStringLiteral("tldr"), QStringLiteral("abstract")},
        {QStringLiteral("hint"), QStringLiteral("tip")},         {QStringLiteral("important"), QStringLiteral("tip")},
        {QStringLiteral("check"), QStringLiteral("success")},    {QStringLiteral("done"), QStringLiteral("success")},
        {QStringLiteral("help"), QStringLiteral("question")},    {QStringLiteral("faq"), QStringLiteral("question")},
        {QStringLiteral("caution"), QStringLiteral("warning")},  {QStringLiteral("attention"), QStringLiteral("warning")},
        {QStringLiteral("fail"), QStringLiteral("failure")},     {QStringLiteral("missing"), QStringLiteral("failure")},
        {QStringLiteral("error"), QStringLiteral("danger")},     {QStringLiteral("cite"), QStringLiteral("quote")},
    };
    return aliases.value(t, t);
}

class BlockParser
{
public:
    explicit BlockParser(const QString &raw)
        : m_lines(TaskMarkdown::splitLines(raw))
    {
    }

    QVariantList run()
    {
        const BlockScan scan = scanBlocks(m_lines);
        m_toggleable.fill(false, m_lines.size());
        for (qsizetype i = scan.bodyStart; i < m_lines.size(); ++i) {
            m_toggleable[i] = !scan.isCode.at(i) && TaskMarkdown::isTaskLine(m_lines.at(i));
        }

        QVariantList out;
        QString prev;
        if (scan.frontmatterEnd >= 0) {
            out.append(properties(scan.frontmatterEnd));
            prev = QStringLiteral("properties");
        }

        QList<Line> body;
        body.reserve(m_lines.size());
        for (qsizetype i = scan.bodyStart; i < m_lines.size(); ++i) {
            body.append(Line{int(i), expandLeading(lineCore(m_lines.at(i))), scan.code.at(i)});
        }
        out += parseContainer(body, Ctx{}, prev, QStringLiteral("none"));
        return out;
    }

private:
    struct Ctx {
        int depth = 0;
        bool inItem = false;
        bool struck = false;
        bool quote = false;
    };

    static QString gapFor(QString prev, const QString &unit, const QString &firstGap)
    {
        if (prev.isEmpty()) {
            return firstGap;
        }
        if (prev == QLatin1String("item")) {
            if (unit == QLatin1String("list")) {
                return QStringLiteral("none");
            }
            prev = QStringLiteral("paragraph");
        }
        if (prev == QLatin1String("properties")) {
            return QStringLiteral("properties");
        }
        if (unit == QLatin1String("hr")) {
            return prev == QLatin1String("table") ? QStringLiteral("table-hr") : QStringLiteral("hr");
        }
        if (prev == QLatin1String("hr")) {
            return unit == QLatin1String("table") ? QStringLiteral("table-hr") : QStringLiteral("hr");
        }
        if (unit == QLatin1String("table")) {
            return QStringLiteral("table");
        }
        if (prev == QLatin1String("table")) {
            return unit == QLatin1String("heading") ? QStringLiteral("table-heading") : QStringLiteral("table");
        }
        if (unit == QLatin1String("heading")) {
            static const QStringList tall = {QStringLiteral("paragraph"), QStringLiteral("code"), QStringLiteral("list"), QStringLiteral("blockquote")};
            return tall.contains(prev) ? QStringLiteral("heading") : QStringLiteral("p");
        }
        return QStringLiteral("p");
    }

    static QVariantMap base(const QString &kind, int src, int end, const Ctx &ctx)
    {
        return QVariantMap{
            {QStringLiteral("kind"), kind},
            {QStringLiteral("sourceLine"), src},
            {QStringLiteral("endLine"), end},
            {QStringLiteral("depth"), ctx.depth},
            {QStringLiteral("inItem"), ctx.inItem},
            {QStringLiteral("gap"), QStringLiteral("none")},
            {QStringLiteral("listPadAfter"), 0},
            {QStringLiteral("struck"), ctx.struck},
            {QStringLiteral("text"), QString()},
            {QStringLiteral("inlineHtml"), QString()},
            {QStringLiteral("decorations"), QVariantList()},
        };
    }

    static void setInline(QVariantMap &b, const Inline &in)
    {
        b[QStringLiteral("inlineHtml")] = in.html;
        b[QStringLiteral("text")] = in.text;
        b[QStringLiteral("decorations")] = in.decorations;
        if (!in.marks.isEmpty()) {
            b[QStringLiteral("marks")] = in.marks;
        }
    }

    static QVariantMap cell(const QString &source)
    {
        const Inline in = renderInline(source);
        QVariantMap c{{QStringLiteral("inlineHtml"), in.html}, {QStringLiteral("text"), in.text}, {QStringLiteral("decorations"), in.decorations}};
        if (!in.marks.isEmpty()) {
            c[QStringLiteral("marks")] = in.marks;
        }
        return c;
    }

    QVariantMap properties(qsizetype fmEnd) const
    {
        QVariantList props;
        QString key;
        QString inlineValue;
        QStringList cont;
        bool open = false;
        auto flush = [&]() {
            if (!open) {
                return;
            }
            QString value;
            QStringList values;
            const QString iv = inlineValue.trimmed();
            bool simpleList = !cont.isEmpty() && iv.isEmpty();
            for (const QString &c : cont) {
                const QString t = c.trimmed();
                static const QRegularExpression mapItem(QStringLiteral("^-\\s+[^\"'\\s][^:]*:(\\s|$)"));
                if (!(t == QLatin1String("-") || t.startsWith(QLatin1String("- "))) || mapItem.match(t).hasMatch()) {
                    simpleList = false;
                }
            }
            if (iv.startsWith(QLatin1Char('[')) && iv.endsWith(QLatin1Char(']')) && cont.isEmpty()) {
                values = splitFlowList(iv.mid(1, iv.size() - 2));
                value = values.join(QLatin1String(", "));
            } else if (simpleList) {
                for (const QString &c : cont) {
                    values.append(unquote(c.trimmed().mid(1)));
                }
                value = values.join(QLatin1String(", "));
            } else if (cont.isEmpty()) {
                value = unquote(iv);
                if (!value.isEmpty()) {
                    values.append(value);
                }
            } else {
                QStringList all;
                if (!iv.isEmpty()) {
                    all.append(iv);
                }
                all += cont;
                value = all.join(QLatin1Char('\n')).trimmed();
            }
            props.append(QVariantMap{{QStringLiteral("key"), key}, {QStringLiteral("value"), value}, {QStringLiteral("values"), values}});
            open = false;
            cont.clear();
        };
        static const QRegularExpression keyRe(QStringLiteral("^([^\\s#:-][^:]*):(?:[ \\t]+(.*))?$"));
        for (qsizetype k = 1; k < fmEnd; ++k) {
            const QString t = lineCore(m_lines.at(k)).toString();
            if (blank(t)) {
                if (open) {
                    cont.append(QString());
                }
                continue;
            }
            const QRegularExpressionMatch m = keyRe.match(t);
            if (m.hasMatch() && !t.at(0).isSpace()) {
                flush();
                key = m.captured(1).trimmed();
                inlineValue = m.captured(2);
                open = true;
            } else if (open) {
                cont.append(t);
            }
        }
        flush();

        QVariantMap b = base(QStringLiteral("properties"), 0, int(fmEnd), Ctx{});
        b[QStringLiteral("properties")] = props;
        return b;
    }

    /** Re-classify quote-stripped lines with the same scanner (a leading blank line disables frontmatter). */
    static void rescan(QList<Line> &lines)
    {
        QStringList texts;
        texts.reserve(lines.size() + 1);
        texts.append(QString());
        for (const Line &l : lines) {
            texts.append(l.text);
        }
        const BlockScan scan = scanBlocks(texts);
        for (qsizetype k = 0; k < lines.size(); ++k) {
            lines[k].code = scan.code.at(k + 1);
        }
    }

    static void addPad(QVariantList &out, qsizetype from)
    {
        if (out.size() <= from) {
            return;
        }
        QVariantMap last = out.last().toMap();
        last[QStringLiteral("listPadAfter")] = last.value(QStringLiteral("listPadAfter")).toInt() + 1;
        out.last() = last;
    }

    QVariantList parseContainer(const QList<Line> &L, const Ctx &ctx, QString prev, const QString &firstGap)
    {
        QVariantList out;
        const qsizetype n = L.size();
        qsizetype i = 0;

        auto push = [&](QVariantMap b, const QString &unit) {
            b[QStringLiteral("gap")] = gapFor(prev, unit, firstGap);
            out.append(b);
            prev = unit;
        };

        while (i < n) {
            const Line &ln = L.at(i);

            if (ln.code == LineCode::Prose && blank(ln.text)) {
                ++i;
                continue;
            }

            // ---- fenced code (the scanner decided) ----
            if (ln.code == LineCode::FenceOpen || ln.code == LineCode::FenceBody || ln.code == LineCode::FenceClose) {
                Cursor c;
                skipSpaces(QStringView(ln.text), c);
                const FenceOpen f = matchFenceOpen(QStringView(ln.text), c);
                if (ln.code != LineCode::FenceOpen || f.ok) {
                    QStringList body;
                    QString language;
                    int fenceIndent = 0;
                    qsizetype j = i;
                    if (ln.code == LineCode::FenceOpen) {
                        fenceIndent = f.indent;
                        QString info = ln.text.mid(c.idx + f.len).trimmed();
                        language = info.section(QLatin1Char(' '), 0, 0).section(QLatin1Char('\t'), 0, 0);
                        ++j;
                    }
                    int last = ln.src;
                    while (j < n && (L.at(j).code == LineCode::FenceBody || L.at(j).code == LineCode::FenceClose)) {
                        last = L.at(j).src;
                        if (L.at(j).code == LineCode::FenceClose) {
                            ++j;
                            break;
                        }
                        body.append(dedent(L.at(j).text, fenceIndent));
                        ++j;
                    }
                    QVariantMap b = base(QStringLiteral("code"), ln.src, last, ctx);
                    const QString codeText = body.join(QLatin1Char('\n'));
                    b[QStringLiteral("codeText")] = codeText;
                    b[QStringLiteral("language")] = language;
                    b[QStringLiteral("text")] = codeText;
                    push(b, QStringLiteral("code"));
                    i = j;
                    continue;
                }
            }

            // ---- indented code ----
            if (ln.code == LineCode::Indented) {
                QStringList body;
                qsizetype j = i;
                int last = ln.src;
                while (j < n) {
                    if (L.at(j).code == LineCode::Indented) {
                        body.append(dedent(L.at(j).text, 4));
                        last = L.at(j).src;
                        ++j;
                        continue;
                    }
                    if (L.at(j).code == LineCode::Prose && blank(L.at(j).text)) {
                        qsizetype k = j;
                        while (k < n && L.at(k).code == LineCode::Prose && blank(L.at(k).text)) {
                            ++k;
                        }
                        if (k < n && L.at(k).code == LineCode::Indented) {
                            for (; j < k; ++j) {
                                body.append(QString());
                            }
                            continue;
                        }
                    }
                    break;
                }
                QVariantMap b = base(QStringLiteral("code"), ln.src, last, ctx);
                const QString codeText = body.join(QLatin1Char('\n'));
                b[QStringLiteral("codeText")] = codeText;
                b[QStringLiteral("language")] = QString();
                b[QStringLiteral("text")] = codeText;
                push(b, QStringLiteral("code"));
                i = j;
                continue;
            }

            const int ind = indentOf(ln.text);
            const QString rest = ln.text.mid(ind);

            // ---- ATX heading ----
            QString headingText;
            if (const int level = atxHeading(ln.text, &headingText)) {
                QVariantMap b = base(QStringLiteral("heading"), ln.src, ln.src, ctx);
                b[QStringLiteral("level")] = level;
                setInline(b, renderInline(headingText));
                push(b, QStringLiteral("heading"));
                ++i;
                continue;
            }

            // ---- $$ math block ----
            if (ind <= 3 && rest.startsWith(QLatin1String("$$"))) {
                const QString after = rest.mid(2).trimmed();
                QStringList body;
                qsizetype end = -1;
                if (after.size() >= 2 && after.endsWith(QLatin1String("$$"))) {
                    body.append(after.left(after.size() - 2).trimmed());
                    end = i;
                } else {
                    if (!after.isEmpty()) {
                        body.append(after);
                    }
                    for (qsizetype j = i + 1; j < n; ++j) {
                        const Line &x = L.at(j);
                        if (x.code != LineCode::Prose || blank(x.text) || listMarker(x.text).task) {
                            break;
                        }
                        const QString t = x.text.trimmed();
                        if (t.endsWith(QLatin1String("$$"))) {
                            const QString last = t.left(t.size() - 2).trimmed();
                            if (!last.isEmpty()) {
                                body.append(last);
                            }
                            end = j;
                            break;
                        }
                        body.append(x.text);
                    }
                }
                if (end >= 0) {
                    QVariantMap b = base(QStringLiteral("code"), ln.src, L.at(end).src, ctx);
                    const QString codeText = body.join(QLatin1Char('\n'));
                    b[QStringLiteral("codeText")] = codeText;
                    b[QStringLiteral("language")] = QStringLiteral("math");
                    b[QStringLiteral("text")] = codeText;
                    push(b, QStringLiteral("code"));
                    i = end + 1;
                    continue;
                }
            }

            // ---- blockquote / callout ----
            if (ind <= 3 && rest.startsWith(QLatin1Char('>'))) {
                QList<Line> inner;
                qsizetype j = i;
                bool lastNonBlank = false;
                while (j < n) {
                    const Line &x = L.at(j);
                    const int xi = indentOf(x.text);
                    if (x.code == LineCode::Prose && xi <= 3 && x.text.mid(xi).startsWith(QLatin1Char('>'))) {
                        QString s = x.text.mid(xi + 1);
                        if (s.startsWith(QLatin1Char(' '))) {
                            s.remove(0, 1);
                        } else if (s.startsWith(QLatin1Char('\t'))) {
                            s.replace(0, 1, QStringLiteral("  "));
                        }
                        inner.append(Line{x.src, expandLeading(QStringView(s)), LineCode::Prose});
                        lastNonBlank = !blank(s);
                        ++j;
                        continue;
                    }
                    if (lastNonBlank && !interruptsParagraph(x) && !isTableStart(L, j)) {
                        inner.append(Line{x.src, x.text, LineCode::Prose}); // lazy continuation
                        ++j;
                        continue;
                    }
                    break;
                }
                const int last = inner.isEmpty() ? ln.src : inner.last().src;
                Ctx childCtx;
                childCtx.struck = ctx.struck;
                childCtx.quote = true;

                static const QRegularExpression calloutRe(QStringLiteral("^ {0,3}\\[!([^\\]\\s]+)\\]([+-]?)[ \\t]*(.*)$"));
                const QRegularExpressionMatch cm = inner.isEmpty() ? QRegularExpressionMatch() : calloutRe.match(inner.first().text);
                if (cm.hasMatch()) {
                    const QString rawType = cm.captured(1).toLower();
                    QList<Line> bodyLines = inner.mid(1);
                    rescan(bodyLines);
                    QVariantMap b = base(QStringLiteral("callout"), ln.src, last, ctx);
                    b[QStringLiteral("calloutType")] = normaliseCalloutType(rawType);
                    const QString title = cm.captured(3).trimmed();
                    const Inline t = renderInline(title.isEmpty() ? capitalised(rawType) : joinInlineLines({title}));
                    b[QStringLiteral("titleHtml")] = t.html;
                    b[QStringLiteral("titleDecorations")] = t.decorations;
                    if (!t.marks.isEmpty()) {
                        b[QStringLiteral("titleMarks")] = t.marks;
                    }
                    b[QStringLiteral("text")] = t.text;
                    b[QStringLiteral("foldable")] = !cm.captured(2).isEmpty();
                    b[QStringLiteral("folded")] = cm.captured(2) == QLatin1String("-");
                    b[QStringLiteral("children")] = parseContainer(bodyLines, childCtx, QString(), QStringLiteral("p"));
                    push(b, QStringLiteral("callout"));
                } else {
                    rescan(inner);
                    QVariantMap b = base(QStringLiteral("blockquote"), ln.src, last, ctx);
                    b[QStringLiteral("children")] = parseContainer(inner, childCtx, QString(), QStringLiteral("none"));
                    push(b, QStringLiteral("blockquote"));
                }
                i = j;
                continue;
            }

            // ---- thematic break ----
            if (isThematicBreak(QStringView(ln.text))) {
                push(base(QStringLiteral("hr"), ln.src, ln.src, ctx), QStringLiteral("hr"));
                ++i;
                continue;
            }

            // ---- list ----
            const Marker mk = listMarker(ln.text);
            if (mk.ok && (mk.indent <= 3 || mk.task)) {
                parseList(L, i, ctx, out, prev, firstGap);
                continue;
            }

            // ---- table ----
            if (isTableStart(L, i)) {
                QStringList align;
                isDelimiterRow(L.at(i + 1).text, &align);
                const int cols = align.size();
                auto rowCells = [&](const QString &t) {
                    QStringList cells = splitRow(t);
                    while (cells.size() < cols) {
                        cells.append(QString());
                    }
                    cells = cells.mid(0, cols);
                    QVariantList r;
                    for (const QString &c : cells) {
                        r.append(cell(c));
                    }
                    return r;
                };
                QVariantMap b = base(QStringLiteral("table"), ln.src, L.at(i + 1).src, ctx);
                b[QStringLiteral("header")] = rowCells(ln.text);
                b[QStringLiteral("align")] = align;
                QVariantList rows;
                qsizetype j = i + 2;
                while (j < n && L.at(j).code == LineCode::Prose && !blank(L.at(j).text) && L.at(j).text.contains(QLatin1Char('|')) && !interruptsParagraph(L.at(j))) {
                    rows.append(QVariant(rowCells(L.at(j).text)));
                    b[QStringLiteral("endLine")] = L.at(j).src;
                    ++j;
                }
                b[QStringLiteral("rows")] = rows;
                push(b, QStringLiteral("table"));
                i = j;
                continue;
            }

            // ---- paragraph (maybe a setext heading) ----
            QStringList para{ln.text};
            qsizetype j = i + 1;
            int setext = 0;
            while (j < n) {
                const Line &x = L.at(j);
                if (x.code != LineCode::Prose || blank(x.text)) {
                    break;
                }
                if (isSetextUnderline(x.text)) {
                    setext = x.text.trimmed().startsWith(QLatin1Char('=')) ? 1 : 2;
                    ++j;
                    break;
                }
                if (interruptsParagraph(x) || isTableStart(L, j)) {
                    break;
                }
                para.append(x.text);
                ++j;
            }
            const int last = L.at(j - 1).src;
            if (setext) {
                QVariantMap b = base(QStringLiteral("heading"), ln.src, last, ctx);
                b[QStringLiteral("level")] = setext;
                setInline(b, renderInline(joinInlineLines(para)));
                push(b, QStringLiteral("heading"));
            } else {
                const QString source = joinInlineLines(para);
                if (!source.isEmpty()) {
                    QVariantMap b = base(QStringLiteral("paragraph"), ln.src, last, ctx);
                    setInline(b, renderInline(source));
                    push(b, QStringLiteral("paragraph"));
                }
            }
            i = j;
        }
        return out;
    }

    void parseList(const QList<Line> &L, qsizetype &i, const Ctx &ctx, QVariantList &out, QString &prev, const QString &firstGap)
    {
        const qsizetype n = L.size();
        const Marker first = listMarker(L.at(i).text);
        int index = 0;
        bool firstItem = true;

        while (i < n) {
            const Marker mk = listMarker(L.at(i).text);
            const int C = mk.contentCol;

            // Collect the item's lines, de-indented to its content column.
            QList<Line> content;
            Line l0 = L.at(i);
            l0.text = mk.firstContent;
            if (l0.code == LineCode::FenceOpen && !startsFenceText(l0.text)) {
                l0.code = LineCode::Prose;
            }
            content.append(l0);
            bool fenceOpen = l0.code == LineCode::FenceOpen;
            bool paraOpen = l0.code == LineCode::Prose && !blank(l0.text);
            qsizetype j = i + 1;
            while (j < n) {
                const Line &x = L.at(j);
                if (fenceOpen && (x.code == LineCode::FenceBody || x.code == LineCode::FenceClose)) {
                    content.append(Line{x.src, dedent(x.text, C), x.code});
                    fenceOpen = x.code != LineCode::FenceClose;
                    paraOpen = false;
                    ++j;
                    continue;
                }
                if (x.code == LineCode::Prose && blank(x.text)) {
                    qsizetype k = j;
                    while (k < n && L.at(k).code == LineCode::Prose && blank(L.at(k).text)) {
                        ++k;
                    }
                    if (k < n && indentOf(L.at(k).text) >= C) {
                        for (; j < k; ++j) {
                            content.append(Line{L.at(j).src, QString(), LineCode::Prose});
                        }
                        paraOpen = false;
                        continue;
                    }
                    break;
                }
                if (indentOf(x.text) >= C) {
                    Line y{x.src, dedent(x.text, C), x.code};
                    content.append(y);
                    if (y.code == LineCode::FenceOpen) {
                        fenceOpen = true;
                    }
                    paraOpen = y.code == LineCode::Prose && !atxHeading(y.text, nullptr) && !isThematicBreak(QStringView(y.text));
                    ++j;
                    continue;
                }
                // Inside a list any marker starts a sibling item, never a lazy line.
                if (paraOpen && !interruptsParagraph(x) && !listMarker(x.text).ok && !isTableStart(L, j)) {
                    content.append(x); // lazy continuation line
                    ++j;
                    continue;
                }
                break;
            }

            // The item block and its own first paragraph.
            Ctx itemCtx = ctx;
            itemCtx.depth = ctx.depth + 1;
            itemCtx.inItem = false;
            const bool checked = mk.task && mk.taskChar != QLatin1Char(' ');
            itemCtx.struck = ctx.struck || checked;

            QStringList para;
            qsizetype k = 0;
            const Line &c0 = content.first();
            if (mk.task) {
                para.append(mk.afterBox);
                k = 1;
            } else if (c0.code == LineCode::Prose && blank(c0.text)) {
                k = 1;
            } else if (c0.code == LineCode::Prose && !interruptsParagraph(c0) && !isTableStart(content, 0)) {
                para.append(c0.text);
                k = 1;
            }
            int itemEnd = L.at(i).src;
            if (k == 1 && !blank(c0.text)) {
                while (k < content.size()) {
                    const Line &x = content.at(k);
                    if (x.code != LineCode::Prose || blank(x.text) || interruptsParagraph(x) || isTableStart(content, k)) {
                        break;
                    }
                    para.append(x.text);
                    itemEnd = x.src;
                    ++k;
                }
            }

            const QString kind = mk.task ? QStringLiteral("task") : (first.ordered ? QStringLiteral("ordered") : QStringLiteral("bullet"));
            QVariantMap b = base(kind, L.at(i).src, itemEnd, itemCtx);
            b[QStringLiteral("inItem")] = false;
            b[QStringLiteral("struck")] = itemCtx.struck;
            b[QStringLiteral("listType")] = first.ordered ? QStringLiteral("ordered") : QStringLiteral("bullet");
            b[QStringLiteral("markerText")] = first.ordered ? QString::number(first.start + index) + QLatin1Char('.') : QString();
            const QString source = joinInlineLines(para);
            if (!source.isEmpty()) {
                setInline(b, renderInline(source));
            }
            if (mk.task) {
                const int src = L.at(i).src;
                b[QStringLiteral("checked")] = checked;
                b[QStringLiteral("taskChar")] = QString(mk.taskChar);
                b[QStringLiteral("toggleable")] = !ctx.quote && src < m_toggleable.size() && m_toggleable.at(src);
                b[QStringLiteral("expectedLineText")] = m_lines.value(src);
            }
            b[QStringLiteral("gap")] = firstItem ? gapFor(prev, QStringLiteral("list"), firstGap) : QStringLiteral("none");
            const qsizetype itemIndex = out.size();
            out.append(b);

            Ctx childCtx = itemCtx;
            childCtx.inItem = true;
            out += parseContainer(content.mid(k), childCtx, para.isEmpty() ? QString() : QStringLiteral("item"), QStringLiteral("none"));
            addPad(out, itemIndex);

            prev = QStringLiteral("list");
            firstItem = false;
            ++index;

            // Next item of the SAME list?
            qsizetype nx = j;
            while (nx < n && L.at(nx).code == LineCode::Prose && blank(L.at(nx).text)) {
                ++nx;
            }
            if (nx < n && (L.at(nx).code == LineCode::Prose || L.at(nx).code == LineCode::FenceOpen)) {
                const Marker nm = listMarker(L.at(nx).text);
                const bool same = nm.ok && nm.ordered == first.ordered && (first.ordered ? nm.delim == first.delim : nm.bullet == first.bullet);
                if (same && (nm.indent <= 3 || nm.task) && (L.at(nx).code == LineCode::Prose || !startsFenceText(L.at(nx).text))) {
                    i = nx;
                    continue;
                }
            }
            i = j;
            break;
        }
    }

    QStringList m_lines;
    QList<bool> m_toggleable;
};

} // namespace

namespace MarkdownBlocks
{

QVariantList parse(const QString &raw)
{
    return BlockParser(raw).run();
}

QString inlineHtml(const QString &inlineSource)
{
    return renderInline(inlineSource).html;
}

}
