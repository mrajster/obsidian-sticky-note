/*
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
    SPDX-License-Identifier: GPL-2.0-or-later

    Guiless functional smoke test for the Obsidian Note applet backend.
    Runs against tests/obsidian-torture.md (a copy of /tmp/obsidian-torture.md).
*/

#include "markdownblocks.h"
#include "markdownnote.h"
#include "taskmarkdown.h"

#include <QDir>
#include <QColor>
#include <QSet>
#include <functional>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QVariantMap>

namespace
{

QByteArray readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return f.readAll();
}

void writeAll(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(bytes), qint64(bytes.size()));
    f.close();
}

/**
 * Line-by-line diff of two byte buffers. Returns the 0-based indices of every
 * line that differs. Sizes may differ; the shorter is padded conceptually.
 */
QList<int> differingLines(const QByteArray &a, const QByteArray &b)
{
    const QList<QByteArray> la = a.split('\n');
    const QList<QByteArray> lb = b.split('\n');
    QList<int> out;
    const int n = qMax(la.size(), lb.size());
    for (int i = 0; i < n; ++i) {
        const QByteArray x = i < la.size() ? la.at(i) : QByteArray("<missing>");
        const QByteArray y = i < lb.size() ? lb.at(i) : QByteArray("<missing>");
        if (x != y) {
            out.append(i);
        }
    }
    return out;
}

/**
 * The 0-based index of the first fixture line containing @p needle. Tests locate
 * their fixture lines by content, never by a hard-coded number, so appending to
 * the fixture can never silently re-point an assertion at the wrong line.
 */
int lineIndexOf(const QStringList &lines, const QString &needle)
{
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i).contains(needle)) {
            return i;
        }
    }
    return -1;
}

// ------------------------------------------------------------- block model

/**
 * One line per block, children prefixed with "> ": the shape the geometry
 * reference is keyed on (kind, sourceLine, depth, gap, listPadAfter) plus the
 * task state. Exact string comparison makes any structural drift loud.
 */
void compactBlocks(const QVariantList &blocks, const QString &prefix, QStringList &out)
{
    for (const QVariant &v : blocks) {
        const QVariantMap m = v.toMap();
        QString s = prefix + m.value(QStringLiteral("kind")).toString() + QLatin1Char('@') + QString::number(m.value(QStringLiteral("sourceLine")).toInt())
            + QStringLiteral(" d") + QString::number(m.value(QStringLiteral("depth")).toInt()) + QLatin1Char(' ') + m.value(QStringLiteral("gap")).toString()
            + QStringLiteral(" pad") + QString::number(m.value(QStringLiteral("listPadAfter")).toInt());
        if (m.value(QStringLiteral("inItem")).toBool()) {
            s += QStringLiteral(" inItem");
        }
        if (m.contains(QStringLiteral("checked"))) {
            s += QStringLiteral(" [%1]%2%3")
                     .arg(m.value(QStringLiteral("taskChar")).toString(),
                          m.value(QStringLiteral("checked")).toBool() ? QStringLiteral(" checked") : QString(),
                          m.value(QStringLiteral("toggleable")).toBool() ? QStringLiteral(" toggleable") : QString());
        }
        if (!m.value(QStringLiteral("markerText")).toString().isEmpty()) {
            s += QLatin1Char(' ') + m.value(QStringLiteral("markerText")).toString();
        }
        if (m.value(QStringLiteral("struck")).toBool()) {
            s += QStringLiteral(" struck");
        }
        out.append(s);
        compactBlocks(m.value(QStringLiteral("children")).toList(), prefix + QStringLiteral("> "), out);
    }
}

QStringList compactBlocks(const QString &raw)
{
    QStringList out;
    compactBlocks(MarkdownBlocks::parse(raw), QString(), out);
    return out;
}

/** Every block in document order, children included; @p nested marks blockquote/callout content. */
void allBlocks(const QVariantList &blocks, QList<QVariantMap> &out, bool nested = false)
{
    for (const QVariant &v : blocks) {
        QVariantMap m = v.toMap();
        m.insert(QStringLiteral("_nested"), nested);
        out.append(m);
        allBlocks(m.value(QStringLiteral("children")).toList(), out, true);
    }
}

QList<QVariantMap> allBlocks(const QString &raw)
{
    QList<QVariantMap> out;
    allBlocks(MarkdownBlocks::parse(raw), out);
    return out;
}

/** What ObsidianMetrics.resolveInline() does, with fixed test values. */
QString resolvePlaceholders(QString html)
{
    html.replace(QStringLiteral("%MONO%"), QStringLiteral("DejaVu Sans Mono"));
    html.replace(QStringLiteral("%CODEPT%"), QStringLiteral("10.5"));
    html.replace(QStringLiteral("%TAGPT%"), QStringLiteral("10.5"));
    html.replace(QStringLiteral("%CODEPADX%"), QStringLiteral("4.2"));
    html.replace(QStringLiteral("%TAGPADX%"), QStringLiteral("9.1"));
    html.replace(QStringLiteral("%LINK%"), QStringLiteral("#2980b9"));
    html.replace(QStringLiteral("%MARKBG%"), QStringLiteral("#ffff00"));
    html.replace(QStringLiteral("%CODEFG%"), QStringLiteral("#aa0000"));
    html.replace(QStringLiteral("%TAGFG%"), QStringLiteral("#0000aa"));
    return html;
}

QTextCharFormat formatAt(QTextDocument &doc, int position)
{
    const QTextBlock block = doc.findBlock(position);
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment f = it.fragment();
        if (position >= f.position() && position < f.position() + f.length()) {
            return f.charFormat();
        }
    }
    return QTextCharFormat();
}

/** The inline fixtures other tests in this file build documents from. */
QStringList inlineFixtures()
{
    return {
        QStringLiteral("- [ ] a\n```\n- [ ] fenced\n```\n    - [ ] indented\n"),
        QStringLiteral("---\ntitle: x\nnot: - [ ] decoy\n---\n\n- [ ] real\n"),
        QStringLiteral("- [ ] crlf\r\n- [x] done\r\n"),
        QStringLiteral("- item\n    ```\n    - [ ] fenced in item\n    ```\n- [ ] after\n"),
        QStringLiteral("- a\n\n    continuation\n\n        - [ ] code in item\n\n- [ ] real\n"),
        QStringLiteral("---\n\n- [ ] after a rule, not frontmatter\n"),
        QStringLiteral("para\n        - [ ] deep lazy task\n"),
        QStringLiteral("> - [ ] quoted\n- [ ] not quoted\n"),
        QStringLiteral("$$\n- [ ] not swallowed by an unclosed math block\n"),
        QStringLiteral("| a | b |\n|---|---|\n- [ ] after table | pipe\n"),
        QStringLiteral("1. [ ] one\n2) [x] two\n10. [ ] ten\n"),
        QStringLiteral("- [ ]\n-\t[ ] tab\n-   [x] spaces\n"),
        QStringLiteral("~~~\n- [ ] tilde\n```\n~~~\n- [ ] out\n"),
        QStringLiteral("Title\n---\n- [ ] after setext\n"),
    };
}

} // namespace

class TstTaskMarkdown : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QByteArray m_fixture;

    QString freshCopy(const QString &name)
    {
        const QString p = m_dir.filePath(name);
        QFile::remove(p);
        QFile f(p);
        if (!f.open(QIODevice::WriteOnly)) {
            return QString();
        }
        f.write(m_fixture);
        f.close();
        return p;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY2(m_dir.isValid(), "could not create a temporary directory");
        m_fixture = readAll(QStringLiteral(TORTURE_FIXTURE));
        QVERIFY2(!m_fixture.isEmpty(), "torture fixture is missing or empty: " TORTURE_FIXTURE);
        qInfo("fixture: %s (%lld bytes)", TORTURE_FIXTURE, qint64(m_fixture.size()));
    }

    // ---------------------------------------------------------------- pure

    void testSplitJoinRoundTrip_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("empty") << QString();
        QTest::newRow("no newline") << QStringLiteral("abc");
        QTest::newRow("trailing newline") << QStringLiteral("a\nb\n");
        QTest::newRow("blank last line") << QStringLiteral("a\n\n");
        QTest::newRow("crlf") << QStringLiteral("a\r\nb\r\n");
        QTest::newRow("fixture") << QString::fromUtf8(m_fixture);
    }

    void testSplitJoinRoundTrip()
    {
        QFETCH(QString, text);
        QCOMPARE(TaskMarkdown::joinLines(TaskMarkdown::splitLines(text)), text);
    }

    void testIsTaskLine_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<bool>("isTask");
        QTest::addColumn<bool>("checked");

        QTest::newRow("dash unchecked") << QStringLiteral("- [ ] a") << true << false;
        QTest::newRow("dash checked") << QStringLiteral("- [x] a") << true << true;
        QTest::newRow("upper X") << QStringLiteral("- [X] a") << true << true;
        QTest::newRow("asterisk") << QStringLiteral("* [ ] a") << true << false;
        QTest::newRow("plus") << QStringLiteral("+ [ ] a") << true << false;
        QTest::newRow("ordered dot") << QStringLiteral("1. [ ] a") << true << false;
        QTest::newRow("ordered paren") << QStringLiteral("2) [x] a") << true << true;
        QTest::newRow("two digit") << QStringLiteral("10. [ ] a") << true << false;
        QTest::newRow("tab sep") << QStringLiteral("-\t[ ] a") << true << false;
        QTest::newRow("bare checkbox eol") << QStringLiteral("- [ ]") << true << false;
        QTest::newRow("indented tab") << QStringLiteral("\t- [ ] a") << true << false;
        QTest::newRow("crlf") << QStringLiteral("- [ ] a\r") << true << false;

        QTest::newRow("no space after marker") << QStringLiteral("-[ ] a") << false << false;
        QTest::newRow("empty brackets") << QStringLiteral("- [] a") << false << false;
        QTest::newRow("leading space inside") << QStringLiteral("- [ x] a") << false << false;
        QTest::newRow("trailing space inside") << QStringLiteral("- [x ] a") << false << false;
        QTest::newRow("two chars inside") << QStringLiteral("- [xx] a") << false << false;
        QTest::newRow("no list marker") << QStringLiteral("[ ] a") << false << false;
        QTest::newRow("no space after bracket") << QStringLiteral("- [x]no") << false << false;
        QTest::newRow("md link") << QStringLiteral("- [Link](https://e.com)") << false << false;
        QTest::newRow("wikilink item") << QStringLiteral("- [[Wikilink]]") << false << false;
        QTest::newRow("callout-ish") << QStringLiteral("- [!note] plain item") << false << false;
        QTest::newRow("mid sentence") << QStringLiteral("Prose - [ ] mid.") << false << false;
        QTest::newRow("custom state slash") << QStringLiteral("- [/] wip") << false << false;
    }

    void testIsTaskLine()
    {
        QFETCH(QString, line);
        QFETCH(bool, isTask);
        QFETCH(bool, checked);
        QCOMPARE(TaskMarkdown::isTaskLine(line), isTask);
        QCOMPARE(TaskMarkdown::isTaskChecked(line), checked);
    }

    void testToggleTaskLineTouchesOneChar()
    {
        QString line = QStringLiteral("  - [ ] milk 2%  ");
        const QString before = line;
        QVERIFY(TaskMarkdown::toggleTaskLine(line));
        QCOMPARE(line, QStringLiteral("  - [x] milk 2%  "));
        QCOMPARE(line.size(), before.size());

        int diffs = 0;
        for (qsizetype i = 0; i < line.size(); ++i) {
            if (line.at(i) != before.at(i)) {
                ++diffs;
            }
        }
        QCOMPARE(diffs, 1);

        QVERIFY(TaskMarkdown::toggleTaskLine(line));
        QCOMPARE(line, before);

        QString notATask = QStringLiteral("- [] nope");
        QVERIFY(!TaskMarkdown::toggleTaskLine(notATask));
        QCOMPARE(notATask, QStringLiteral("- [] nope"));
    }

    void testToggleTaskLineCrlfKeepsCarriageReturn()
    {
        QString line = QStringLiteral("- [x] done\r");
        QVERIFY(TaskMarkdown::toggleTaskLine(line));
        QCOMPARE(line, QStringLiteral("- [ ] done\r"));
    }

    void testLinkParsers()
    {
        QCOMPARE(TaskMarkdown::linkScheme(), QStringLiteral("obsnote"));
        QCOMPARE(TaskMarkdown::parseToggleLink(QStringLiteral("obsnote:toggle/12")), 12);
        QCOMPARE(TaskMarkdown::parseToggleLink(QStringLiteral("obsnote:toggle/0")), 0);
        QCOMPARE(TaskMarkdown::parseToggleLink(QStringLiteral("obsnote:toggle/")), -1);
        QCOMPARE(TaskMarkdown::parseToggleLink(QStringLiteral("obsnote:toggle/1a")), -1);
        QCOMPARE(TaskMarkdown::parseToggleLink(QStringLiteral("obsnote:wiki/x")), -1);
        QCOMPARE(TaskMarkdown::parseToggleLink(QStringLiteral("https://kde.org")), -1);

        QCOMPARE(TaskMarkdown::parseWikiLink(QStringLiteral("obsnote:wiki/Some%20Note")), QStringLiteral("Some Note"));
        QCOMPARE(TaskMarkdown::parseWikiLink(QStringLiteral("obsnote:wiki/")), QString());
        QCOMPARE(TaskMarkdown::parseWikiLink(QStringLiteral("obsnote:toggle/1")), QString());
    }

    void testWikilinkRewrite()
    {
        const QString out = TaskMarkdown::render(QStringLiteral("See [[Note Title|Display Alias]] now."));
        QVERIFY2(out.contains(QStringLiteral("[Display Alias](obsnote:wiki/Note%20Title)")), qPrintable(out));

        // Inline code spans are never rewritten.
        const QString code = TaskMarkdown::render(QStringLiteral("`[[Not A Link]]` and [[Real]]"));
        QVERIFY2(code.contains(QStringLiteral("`[[Not A Link]]`")), qPrintable(code));
        QVERIFY2(code.contains(QStringLiteral("obsnote:wiki/Real")), qPrintable(code));
    }

    void testEmbedsBecomeLinksNotImages()
    {
        // "![[Note]]" must NOT become "![Note](obsnote:...)": Text.MarkdownText
        // would try to fetch that as an image source and warn
        // 'Protocol "obsnote" is unknown' for every embed in the document.
        const QString one = TaskMarkdown::render(QStringLiteral("![[Embedded Note]]"));
        QCOMPARE(one, QStringLiteral("[Embedded Note](obsnote:wiki/Embedded%20Note)"));
        QVERIFY2(!one.contains(QStringLiteral("![")), qPrintable(one));

        // The "|300x200" of an embed is a size hint, not an alias: label with the target.
        const QString sized = TaskMarkdown::render(QStringLiteral("![[image with spaces.png|300x200]]"));
        QCOMPARE(sized, QStringLiteral("[image with spaces.png](obsnote:wiki/image%20with%20spaces.png)"));

        // A plain (non-embed) wikilink still honours its alias.
        QCOMPARE(TaskMarkdown::render(QStringLiteral("[[Note Title|Display Alias]]")),
                 QStringLiteral("[Display Alias](obsnote:wiki/Note%20Title)"));

        // A genuine Markdown image is untouched.
        const QString img = QStringLiteral("![alt text](https://example.com/i.png \"title\")");
        QCOMPARE(TaskMarkdown::render(img), img);

        // No embed anywhere in the fixture produces a Markdown IMAGE whose source
        // uses our private scheme -- that is exactly what made QQuickText warn.
        const QString rendered = TaskMarkdown::render(QString::fromUtf8(m_fixture));
        static const QRegularExpression imageWithOurScheme(QStringLiteral(R"(!\[[^\]]*\]\(obsnote:)"));
        const QRegularExpressionMatch bad = imageWithOurScheme.match(rendered);
        QVERIFY2(!bad.hasMatch(),
                 qPrintable(QStringLiteral("image with an obsnote: source survived the render at offset %1")
                                .arg(bad.capturedStart())));

        // The fixture really does contain embeds, so the check above is not vacuous.
        QVERIFY2(QString::fromUtf8(m_fixture).contains(QStringLiteral("![[")), "fixture has no embeds to test");
        // ...and they came out as plain links.
        QVERIFY2(rendered.contains(QStringLiteral("[Embedded Note](obsnote:wiki/Embedded%20Note)")), "embed did not become a link");
    }

    void testFrontmatterIsHidden()
    {
        const QString rendered = TaskMarkdown::render(QString::fromUtf8(m_fixture));
        QVERIFY2(!rendered.contains(QStringLiteral("not_a_task:")), "frontmatter leaked into the render");
        QVERIFY2(!rendered.contains(QStringLiteral("cssclasses:")), "frontmatter leaked into the render");
        QVERIFY(rendered.startsWith(QStringLiteral("# Torture Test")));
    }

    /**
     * (c) A "[ ]" inside a fenced code block must NOT become a clickable task.
     * The render is the only thing that produces click targets, so the proof is
     * that no obsnote:toggle/<n> href exists for any fenced line.
     */
    void testFencedCodeProducesNoTasks()
    {
        const QString raw = QString::fromUtf8(m_fixture);
        const QStringList lines = TaskMarkdown::splitLines(raw);
        const QString rendered = TaskMarkdown::render(raw);

        // Every 0-based source line index that lives inside a fence in the fixture.
        // (1-based fixture lines 197, 202, 204, 209, 211, plus the indented-code one.)
        const QList<int> fencedTaskLines = {196, 201, 203, 208, 210};
        for (int idx : fencedTaskLines) {
            QVERIFY2(idx < lines.size(), "fixture shorter than expected");
            const QString &l = lines.at(idx);
            QVERIFY2(l.contains(QStringLiteral("[ ]")) || l.contains(QStringLiteral("[x]")),
                     qPrintable(QStringLiteral("line %1 is not the expected checkbox line: %2").arg(idx).arg(l)));
            const QString href = QStringLiteral("obsnote:toggle/%1)").arg(idx);
            QVERIFY2(!rendered.contains(href),
                     qPrintable(QStringLiteral("fenced line %1 became a clickable task: %2").arg(idx).arg(l)));
            // and the literal source line survived byte for byte in the render
            QVERIFY2(rendered.contains(l), qPrintable(QStringLiteral("fenced line %1 was rewritten: %2").arg(idx).arg(l)));
        }

        // The 4-space indented code block (fixture line 226 -> index 225) likewise.
        const int indented = 225;
        QVERIFY(indented < lines.size());
        QVERIFY2(lines.at(indented).contains(QStringLiteral("NOT a task")), qPrintable(lines.at(indented)));
        QVERIFY2(!rendered.contains(QStringLiteral("obsnote:toggle/%1)").arg(indented)),
                 "indented code block line became a clickable task");

        // Frontmatter "- [ ]" (fixture line 12) is dropped entirely.
        QVERIFY2(!rendered.contains(QStringLiteral("obsnote:toggle/11)")), "frontmatter line became a clickable task");

        // Sanity: real tasks DO get hrefs, so the assertions above are not vacuous.
        QVERIFY2(rendered.contains(QStringLiteral("obsnote:toggle/128)")), "fixture line 129 should be a clickable task");
        QVERIFY2(rendered.contains(QString(QChar(0x2610))), "no unchecked glyph in the render");
        QVERIFY2(rendered.contains(QString(QChar(0x2611))), "no checked glyph in the render");
    }

    void testRenderIsPure()
    {
        const QString raw = QString::fromUtf8(m_fixture);
        QCOMPARE(TaskMarkdown::render(raw), TaskMarkdown::render(raw));
    }

    // ---------------------------------------------------------- MarkdownNote

    /** (a) Loading the fixture preserves every byte. */
    void testLoadPreservesBytes()
    {
        const QString path = freshCopy(QStringLiteral("load.md"));
        QVERIFY(!path.isEmpty());

        MarkdownNote note;
        note.setPath(path);
        QCOMPARE(note.status(), MarkdownNote::Ready);
        QCOMPARE(note.fileName(), QStringLiteral("load.md"));
        QCOMPARE(note.rawText().toUtf8(), m_fixture);
        QCOMPARE(note.errorString(), QString());
    }

    /** (b) Toggling flips exactly one line; every other byte is identical. */
    void testToggleFlipsExactlyOneLine()
    {
        const QString path = freshCopy(QStringLiteral("toggle.md"));
        QVERIFY(!path.isEmpty());

        MarkdownNote note;
        note.setPath(path);
        QCOMPARE(note.status(), MarkdownNote::Ready);

        QSignalSpy savedSpy(&note, &MarkdownNote::saved);
        QSignalSpy rejectedSpy(&note, &MarkdownNote::toggleRejected);

        const int lineIndex = 128; // fixture line 129: "- [ ] plain unchecked, dash marker"
        QVERIFY2(note.toggleTaskAtLine(lineIndex), "toggleTaskAtLine returned false");
        QCOMPARE(rejectedSpy.count(), 0);
        QCOMPARE(savedSpy.count(), 1);

        const QByteArray after = readAll(path);
        QVERIFY(!after.isEmpty());
        QCOMPARE(after.size(), m_fixture.size()); // one character in place, same length

        const QList<int> diffs = differingLines(m_fixture, after);
        if (diffs != QList<int>{lineIndex}) {
            QString msg = QStringLiteral("expected exactly line %1 to differ, got:").arg(lineIndex);
            for (int d : diffs) {
                msg += QStringLiteral("\n  line %1:\n    - %2\n    + %3")
                           .arg(d)
                           .arg(QString::fromUtf8(m_fixture.split('\n').value(d)))
                           .arg(QString::fromUtf8(after.split('\n').value(d)));
            }
            QFAIL(qPrintable(msg));
        }

        QCOMPARE(QString::fromUtf8(m_fixture.split('\n').at(lineIndex)), QStringLiteral("- [ ] plain unchecked, dash marker"));
        QCOMPARE(QString::fromUtf8(after.split('\n').at(lineIndex)), QStringLiteral("- [x] plain unchecked, dash marker"));

        // and exactly one byte in the whole file changed
        int byteDiffs = 0;
        for (int i = 0; i < m_fixture.size(); ++i) {
            if (m_fixture.at(i) != after.at(i)) {
                ++byteDiffs;
            }
        }
        QCOMPARE(byteDiffs, 1);

        // Toggle back: byte-identical to the original.
        QVERIFY(note.toggleTaskAtLine(lineIndex));
        QCOMPARE(readAll(path), m_fixture);
    }

    void testToggleRejectsNonTaskAndOutOfRange()
    {
        const QString path = freshCopy(QStringLiteral("reject.md"));
        MarkdownNote note;
        note.setPath(path);
        QCOMPARE(note.status(), MarkdownNote::Ready);

        QSignalSpy rejectedSpy(&note, &MarkdownNote::toggleRejected);

        QVERIFY(!note.toggleTaskAtLine(0)); // "---" frontmatter fence, not a task
        QCOMPARE(rejectedSpy.count(), 1);

        QVERIFY(!note.toggleTaskAtLine(999999)); // out of range
        QCOMPARE(rejectedSpy.count(), 2);

        QVERIFY(!note.toggleTaskAtLine(-1));
        QCOMPARE(rejectedSpy.count(), 3);

        QCOMPARE(readAll(path), m_fixture); // nothing was written
    }

    /** (d) save() writes atomically and preserves the trailing newline verbatim. */
    void testSavePreservesTrailingNewlineAndIsAtomic()
    {
        const QString path = freshCopy(QStringLiteral("save.md"));
        MarkdownNote note;
        note.setPath(path);
        QCOMPARE(note.status(), MarkdownNote::Ready);

        QVERIFY2(m_fixture.endsWith('\n'), "fixture should end with a newline");

        // 1. Round-tripping the exact text is a no-op that never touches the mtime.
        const QFileInfo before(path);
        const QDateTime mtimeBefore = before.lastModified();
        QSignalSpy savedSpy(&note, &MarkdownNote::saved);
        QVERIFY(note.save(note.rawText()));
        QCOMPARE(savedSpy.count(), 0);
        QCOMPARE(QFileInfo(path).lastModified(), mtimeBefore);
        QCOMPARE(readAll(path), m_fixture);

        // 2. A real edit keeps the trailing newline exactly as given.
        const QString edited = note.rawText() + QStringLiteral("\nappended line\n");
        QVERIFY(note.save(edited));
        QCOMPARE(savedSpy.count(), 1);
        QCOMPARE(note.status(), MarkdownNote::Ready);
        const QByteArray onDisk = readAll(path);
        QCOMPARE(onDisk, edited.toUtf8());
        QVERIFY2(onDisk.endsWith("\nappended line\n"), "trailing newline was not preserved");
        QVERIFY2(onDisk.startsWith(m_fixture), "the original bytes were not preserved");

        // 3. A text with NO trailing newline stays without one (no silent fixups).
        const QString noNewline = QStringLiteral("# just this");
        QVERIFY(note.save(noNewline));
        const QByteArray bare = readAll(path);
        QCOMPARE(bare, QByteArray("# just this"));
        QVERIFY2(!bare.endsWith('\n'), "a trailing newline was invented");

        // 4. Atomic: QSaveFile leaves no stray temp file behind, and the target
        //    is never a partially written file.
        const QFileInfo fi(path);
        const QStringList leftovers = QDir(fi.absolutePath()).entryList({QStringLiteral("save.md.*")}, QDir::Files);
        QVERIFY2(leftovers.isEmpty(), qPrintable(QStringLiteral("stray temp files: %1").arg(leftovers.join(u','))));

        // 5. Writing to an unwritable location reports SaveError and keeps rawText.
        MarkdownNote bad;
        bad.setPath(QStringLiteral("/proc/definitely/not/writable/x.md"));
        QSignalSpy failSpy(&bad, &MarkdownNote::saveFailed);
        QVERIFY(!bad.save(QStringLiteral("nope")));
        QCOMPARE(bad.status(), MarkdownNote::SaveError);
        QCOMPARE(failSpy.count(), 1);
    }

    void testSaveWithNoPathFails()
    {
        MarkdownNote note;
        QSignalSpy failSpy(&note, &MarkdownNote::saveFailed);
        QVERIFY(!note.save(QStringLiteral("x")));
        QCOMPARE(failSpy.count(), 1);
        QCOMPARE(note.status(), MarkdownNote::NoPath);
    }

    /** (e) An external modification triggers a reload. */
    void testExternalModificationTriggersReload()
    {
        const QString path = freshCopy(QStringLiteral("external.md"));
        MarkdownNote note;
        note.setPath(path);
        QCOMPARE(note.status(), MarkdownNote::Ready);
        QCOMPARE(note.rawText().toUtf8(), m_fixture);

        QSignalSpy reloadedSpy(&note, &MarkdownNote::reloaded);
        QSignalSpy rawSpy(&note, &MarkdownNote::rawTextChanged);

        // KDirWatch's stat backend has a 500ms granularity; make the mtime move.
        QTest::qWait(1100);
        const QByteArray external = m_fixture + "\n<!-- edited by Obsidian -->\n";
        writeAll(path, external);

        QTRY_VERIFY_WITH_TIMEOUT(reloadedSpy.count() >= 1, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(note.rawText().toUtf8(), external, 15000);
        QVERIFY(rawSpy.count() >= 1);
        QCOMPARE(note.status(), MarkdownNote::Ready);
        QVERIFY(!note.externalChangePending());
    }

    /** While editing, an external change must NOT clobber the buffer. */
    void testExternalModificationWhileEditingIsDeferred()
    {
        const QString path = freshCopy(QStringLiteral("editing.md"));
        MarkdownNote note;
        note.setPath(path);
        QCOMPARE(note.status(), MarkdownNote::Ready);
        note.setEditing(true);

        QSignalSpy pendingSpy(&note, &MarkdownNote::externalChangePendingChanged);

        QTest::qWait(1100);
        const QByteArray external = m_fixture + "\n<!-- changed under the editor -->\n";
        writeAll(path, external);

        QTRY_VERIFY_WITH_TIMEOUT(note.externalChangePending(), 15000);
        QVERIFY(pendingSpy.count() >= 1);
        QCOMPARE(note.rawText().toUtf8(), m_fixture); // buffer untouched

        note.acceptExternalChange();
        QCOMPARE(note.rawText().toUtf8(), external);
        QVERIFY(!note.externalChangePending());
    }

    void testMissingFileAndCreate()
    {
        const QString path = m_dir.filePath(QStringLiteral("sub/dir/new.md"));
        MarkdownNote note;
        note.setPath(path);
        QCOMPARE(note.status(), MarkdownNote::Missing);

        QVERIFY(note.createFile());
        QCOMPARE(note.status(), MarkdownNote::Ready);
        QVERIFY(QFileInfo::exists(path));
        QCOMPARE(note.rawText(), QString());
        QCOMPARE(readAll(path), QByteArray());
    }

    void testUrlHelpers()
    {
        MarkdownNote note;
        note.setPath(QStringLiteral("/home/user/vault/My Note.md"));
        // QUrl::toString() pretty-DECODES %20 back to a space; the wire form that
        // Qt.openUrlExternally() actually hands to the obsidian:// handler is the
        // fully-encoded one, so that is what the contract is asserted against.
        const QUrl own = note.obsidianUrl();
        QVERIFY(own.isValid());
        QCOMPARE(own.toString(QUrl::FullyEncoded), QStringLiteral("obsidian://open?path=%2Fhome%2Fuser%2Fvault%2FMy%20Note.md"));
        QCOMPARE(own.toEncoded(), QByteArray("obsidian://open?path=%2Fhome%2Fuser%2Fvault%2FMy%20Note.md"));
        QCOMPARE(QUrlQuery(own).queryItemValue(QStringLiteral("path"), QUrl::FullyDecoded), QStringLiteral("/home/user/vault/My Note.md"));

        const QUrl other = note.obsidianUrl(QStringLiteral("Other Note"));
        QVERIFY(other.isValid());
        QCOMPARE(other.toString(QUrl::FullyEncoded), QStringLiteral("obsidian://open?file=Other%20Note"));
        QCOMPARE(QUrlQuery(other).queryItemValue(QStringLiteral("file"), QUrl::FullyDecoded), QStringLiteral("Other Note"));

        QCOMPARE(note.localPathFromUrl(QUrl(QStringLiteral("file:///home/user/v/n.md"))), QStringLiteral("/home/user/v/n.md"));
        QCOMPARE(note.localPathFromUrl(QUrl(QStringLiteral("/home/user/v/n.md"))), QStringLiteral("/home/user/v/n.md"));

        QCOMPARE(note.toggleLineForLink(QStringLiteral("obsnote:toggle/7")), 7);
        QCOMPARE(note.toggleLineForLink(QStringLiteral("nope")), -1);
        QCOMPARE(note.wikilinkTargetForLink(QStringLiteral("obsnote:wiki/A%20B")), QStringLiteral("A B"));
        QCOMPARE(note.wikilinkTargetForLink(QStringLiteral("nope")), QString());
    }

    void testEmptyPathResetsToNoPath()
    {
        const QString path = freshCopy(QStringLiteral("reset.md"));
        MarkdownNote note;
        note.setPath(path);
        QCOMPARE(note.status(), MarkdownNote::Ready);
        note.setPath(QString());
        QCOMPARE(note.status(), MarkdownNote::NoPath);
        QCOMPARE(note.rawText(), QString());
        QCOMPARE(note.fileName(), QString());
    }

    // ------------------------------------------------- block-scanner regressions

    /**
     * REGRESSION (taskmarkdown.cpp fenceOpenRe, 0-3 spaces of indent): a fence
     * nested inside a list item sits past column 3, so the old scanner never saw
     * it and happily turned "- [ ]" INSIDE the code block into a clickable
     * toggle. Clicking that link rewrote code-block content.
     */
    void testFenceNestedInListItemProducesNoTasks()
    {
        const QString raw = QString::fromUtf8(m_fixture);
        const QStringList lines = TaskMarkdown::splitLines(raw);
        const QString rendered = TaskMarkdown::render(raw);

        const QStringList needles = {
            QStringLiteral("not a task, fenced inside a list item"), // 4-space fence in a list item
            QStringLiteral("tilde-fenced inside a nested list item"), // 6-space tilde fence, nested item
            QStringLiteral("backticks never close a tilde fence"), // ``` must not close ~~~
            QStringLiteral("three backticks cannot close a four-backtick fence"),
        };

        for (const QString &needle : needles) {
            const int idx = lineIndexOf(lines, needle);
            QVERIFY2(idx >= 0, qPrintable(QStringLiteral("fixture line missing: %1").arg(needle)));
            const QString &l = lines.at(idx);
            QVERIFY2(TaskMarkdown::isTaskLine(l),
                     qPrintable(QStringLiteral("line %1 is not checkbox-shaped, the test would be vacuous: %2").arg(idx).arg(l)));
            QVERIFY2(!rendered.contains(QStringLiteral("obsnote:toggle/%1)").arg(idx)),
                     qPrintable(QStringLiteral("fenced line %1 became a clickable task: %2").arg(idx).arg(l)));
            QVERIFY2(rendered.contains(l), qPrintable(QStringLiteral("fenced line %1 was rewritten: %2").arg(idx).arg(l)));
            QVERIFY2(!TaskMarkdown::isTaskLineInDocument(raw, idx),
                     qPrintable(QStringLiteral("line %1 passed the toggle guard: %2").arg(idx).arg(l)));
        }

        // The fence delimiters themselves survive byte for byte, info string included.
        QVERIFY2(rendered.contains(QStringLiteral("    ```python")), "nested fence opener was rewritten");
        QVERIFY2(rendered.contains(QStringLiteral("      ~~~text")), "nested tilde fence opener was rewritten");
    }

    /** Opener/closer pairing: char, length and info string, with no fixture involved. */
    void testFenceDelimiterRules()
    {
        // A backtick line cannot close a tilde fence, whatever its length.
        const QString tilde = QStringLiteral("~~~text\n- [ ] inside\n```\n- [x] still inside\n~~~\n- [ ] outside\n");
        const QString t = TaskMarkdown::render(tilde);
        QVERIFY2(!t.contains(QStringLiteral("obsnote:toggle/1)")), qPrintable(t));
        QVERIFY2(!t.contains(QStringLiteral("obsnote:toggle/3)")), qPrintable(t));
        QVERIFY2(t.contains(QStringLiteral("obsnote:toggle/5)")), qPrintable(t));

        // A closing fence must be at least as long as the opener.
        const QString four = QStringLiteral("````\n```\n- [ ] not a task\n````\n- [ ] a task\n");
        const QString f = TaskMarkdown::render(four);
        QVERIFY2(!f.contains(QStringLiteral("obsnote:toggle/2)")), qPrintable(f));
        QVERIFY2(f.contains(QStringLiteral("obsnote:toggle/4)")), qPrintable(f));

        // A backtick "fence" whose info string contains a backtick is not a fence
        // at all (CommonMark) -- it is a prose line full of inline code spans.
        const QString spans = QStringLiteral("``` not a fence ```\n- [ ] still a real task\n");
        const QString s = TaskMarkdown::render(spans);
        QVERIFY2(s.contains(QStringLiteral("obsnote:toggle/1)")), qPrintable(s));

        // A tilde fence's info string may contain anything, backticks included.
        const QString tildeInfo = QStringLiteral("~~~ ``` weird info\n- [ ] not a task\n~~~\n");
        const QString ti = TaskMarkdown::render(tildeInfo);
        QVERIFY2(!ti.contains(QStringLiteral("obsnote:toggle/1)")), qPrintable(ti));

        // An unterminated fence swallows the rest of the document, as CommonMark says.
        const QString open = QStringLiteral("```\n- [ ] never clickable\n");
        QCOMPARE(TaskMarkdown::render(open), open);

        // A fence indented four columns at the TOP level is indented code, not a fence.
        const QString deep = QStringLiteral("para\n\n    ```\n    - [ ] indented code\n    ```\n");
        QCOMPARE(TaskMarkdown::render(deep), deep);
    }

    /**
     * REGRESSION (line-based indented-code heuristic with no list context): the
     * continuation paragraph of a multi-line list item is indented four spaces,
     * which the old scanner called a code block -- so its [[wikilinks]] were
     * never linkified and a genuine sub-task got no toggle.
     */
    void testIndentedCodeRespectsListContinuation()
    {
        const QString raw = QString::fromUtf8(m_fixture);
        const QStringList lines = TaskMarkdown::splitLines(raw);
        const QString rendered = TaskMarkdown::render(raw);

        const int cont = lineIndexOf(lines, QStringLiteral("Continued Wikilink"));
        QVERIFY2(cont >= 0, "fixture continuation paragraph is missing");
        QVERIFY2(lines.at(cont).startsWith(QStringLiteral("    ")),
                 qPrintable(QStringLiteral("continuation line is not indented: %1").arg(lines.at(cont))));
        QVERIFY2(rendered.contains(QStringLiteral("[Continued Wikilink](obsnote:wiki/Continued%20Wikilink)")),
                 "a list-item continuation paragraph was treated as a code block");

        const int task = lineIndexOf(lines, QStringLiteral("continuation task inside the same list item"));
        QVERIFY2(task >= 0, "fixture continuation task is missing");
        QVERIFY2(rendered.contains(QStringLiteral("obsnote:toggle/%1)").arg(task)),
                 "a task in a list-item continuation got no toggle link");
        QVERIFY(TaskMarkdown::isTaskLineInDocument(raw, task));

        // Four spaces at the TOP level is still a code block: the fix must not
        // have turned the indented-code rule off, only made it list-aware.
        const int topLevel = lineIndexOf(lines, QStringLiteral("four-space indented code block at top level"));
        QVERIFY(topLevel >= 0);
        QVERIFY2(!rendered.contains(QStringLiteral("obsnote:toggle/%1)").arg(topLevel)),
                 "top-level indented code became a clickable task");
        QVERIFY(!TaskMarkdown::isTaskLineInDocument(raw, topLevel));

        // Same shape, standalone: two columns of list content + four = code.
        const QString unit = QStringLiteral("- item\n\n    Continuation with [[Wiki]].\n\n      code? no, still the item\n");
        const QString u = TaskMarkdown::render(unit);
        QVERIFY2(u.contains(QStringLiteral("obsnote:wiki/Wiki")), qPrintable(u));
        const QString code = QStringLiteral("- item\n\n      [[Not A Link]] is code (2 + 4 columns)\n");
        QVERIFY2(!TaskMarkdown::render(code).contains(QStringLiteral("obsnote:wiki/")), "list-indented code was linkified");
    }

    /**
     * REGRESSION (frontmatter stripper): a leading "---" was taken as an opening
     * delimiter unconditionally, so a note that opens with a thematic break had
     * everything up to the next "---" silently deleted from the rendered view.
     */
    void testFrontmatterOnlyWhenTrulyDelimited()
    {
        // (1) A real frontmatter block is still hidden, trailing blanks and all.
        QCOMPARE(TaskMarkdown::render(QStringLiteral("---\ntitle: X\ntags: [a]\n---\n\n# Body\n")), QStringLiteral("# Body\n"));
        QCOMPARE(TaskMarkdown::render(QStringLiteral("---\ntitle: X\n...\n# Body\n")), QStringLiteral("# Body\n"));

        // (2) A thematic break on line one is NOT an opening delimiter: nothing
        //     between it and the next rule may disappear.
        const QString rule = QStringLiteral("---\n\nIntro with [[A Link]].\n\n---\n\n# Later\n");
        const QString r = TaskMarkdown::render(rule);
        QVERIFY2(r.startsWith(QStringLiteral("---\n")), qPrintable(r));
        QVERIFY2(r.contains(QStringLiteral("obsnote:wiki/A%20Link")), qPrintable(r));
        QVERIFY2(r.contains(QStringLiteral("# Later")), qPrintable(r));
        QCOMPARE(TaskMarkdown::splitLines(r).size(), TaskMarkdown::splitLines(rule).size());

        // (3) "---" immediately followed by another thematic break, and the other
        //     two break characters, are all left alone.
        for (const QString &doc : {QStringLiteral("---\n---\n- [ ] kept\n"),
                                   QStringLiteral("***\nbody\n***\n"),
                                   QStringLiteral("___\nbody\n___\n")}) {
            const QString out = TaskMarkdown::render(doc);
            QVERIFY2(out.startsWith(TaskMarkdown::splitLines(doc).first()), qPrintable(out));
            QCOMPARE(TaskMarkdown::splitLines(out).size(), TaskMarkdown::splitLines(doc).size());
        }

        // (4) An unterminated "---" is not frontmatter either: nothing is dropped.
        const QString unterminated = QStringLiteral("---\ntitle: X\n\n- [ ] task\n");
        const QString un = TaskMarkdown::render(unterminated);
        QVERIFY2(un.startsWith(QStringLiteral("---\ntitle: X")), qPrintable(un));
        QVERIFY2(un.contains(QStringLiteral("obsnote:toggle/3)")), qPrintable(un));

        // (5) "---" in body position stays a horizontal rule, always.
        const QString body = QStringLiteral("# H\n\n---\n\ntext\n\n---\n");
        QCOMPARE(TaskMarkdown::render(body), body);

        // (6) The fixture's own trailing rules ("---", "***", "___") survive.
        const QString rendered = TaskMarkdown::render(QString::fromUtf8(m_fixture));
        QVERIFY2(rendered.contains(QStringLiteral("\n***\n")), "a thematic break was eaten");
        QVERIFY2(rendered.contains(QStringLiteral("\n___\n")), "a thematic break was eaten");
    }

    /**
     * The guard a toggle must pass before touching the file (policy S6): a line
     * index alone proves nothing, because a note body can contain a literal
     * obsnote:toggle/N link and code blocks are full of checkbox-shaped text.
     */
    void testIsTaskLineInDocumentIsBlockAware()
    {
        const QString raw = QString::fromUtf8(m_fixture);
        const QStringList lines = TaskMarkdown::splitLines(raw);

        const int real = lineIndexOf(lines, QStringLiteral("plain unchecked, dash marker"));
        QVERIFY(real >= 0);
        QVERIFY(TaskMarkdown::isTaskLineInDocument(raw, real));

        // Frontmatter, fenced code and indented code all fail the guard even
        // though isTaskLine() on the bare line says yes.
        const QStringList hidden = {
            QStringLiteral("still not a task (YAML block scalar)"), // inside frontmatter
            QStringLiteral("plain fence with no info string"), // inside ``` fence
            QStringLiteral("tilde fence, not a task"), // inside ~~~ fence
            QStringLiteral("four-space indented code block at top level"), // indented code
            QStringLiteral("not a task, fenced inside a list item"), // fence inside a list item
        };
        for (const QString &needle : hidden) {
            const int idx = lineIndexOf(lines, needle);
            QVERIFY2(idx >= 0, qPrintable(needle));
            QVERIFY2(TaskMarkdown::isTaskLine(lines.at(idx)), qPrintable(QStringLiteral("vacuous: %1").arg(needle)));
            QVERIFY2(!TaskMarkdown::isTaskLineInDocument(raw, idx), qPrintable(QStringLiteral("guard let through: %1").arg(needle)));
        }

        QVERIFY(!TaskMarkdown::isTaskLineInDocument(raw, -1));
        QVERIFY(!TaskMarkdown::isTaskLineInDocument(raw, lines.size()));
        QVERIFY(!TaskMarkdown::isTaskLineInDocument(raw, 0)); // the frontmatter "---"
        QVERIFY(!TaskMarkdown::isTaskLineInDocument(QString(), 0));
    }

    // ------------------------------------------------------- block model

    /**
     * FORMAT-REFERENCE.md is the note Obsidian's reading-view rects were measured
     * on. Its block list (kind, sourceLine, depth, spacing token, list pads) is
     * exactly the 25-row spacing model that lands within 0.37 px of Obsidian.
     */
    void testBlocksFormatReference()
    {
        const QByteArray bytes = readAll(QStringLiteral(FORMAT_REFERENCE_FIXTURE));
        QVERIFY2(!bytes.isEmpty(), FORMAT_REFERENCE_FIXTURE);
        const QString raw = QString::fromUtf8(bytes);

        const QStringList expected = {
            QStringLiteral("heading@0 d0 none pad0"),
            QStringLiteral("paragraph@2 d0 p pad0"),
            QStringLiteral("heading@4 d0 heading pad0"),
            QStringLiteral("paragraph@6 d0 p pad0"),
            QStringLiteral("bullet@8 d1 p pad1"),
            QStringLiteral("bullet@9 d1 none pad0"),
            QStringLiteral("bullet@10 d2 none pad0"),
            QStringLiteral("bullet@11 d3 none pad2"),
            QStringLiteral("bullet@12 d2 none pad2"),
            QStringLiteral("bullet@13 d1 none pad1"),
            QStringLiteral("ordered@15 d1 p pad1 1."),
            QStringLiteral("ordered@16 d1 none pad1 2."),
            QStringLiteral("ordered@17 d1 none pad1 3."),
            QStringLiteral("task@19 d1 p pad1 [ ] toggleable"),
            QStringLiteral("task@20 d1 none pad1 [x] checked toggleable struck"),
            QStringLiteral("task@21 d1 none pad0 [ ] toggleable"),
            QStringLiteral("task@22 d2 none pad1 [ ] toggleable"),
            QStringLiteral("task@23 d2 none pad2 [x] checked toggleable struck"),
            QStringLiteral("blockquote@25 d0 p pad0"),
            QStringLiteral("> paragraph@25 d0 none pad0"),
            QStringLiteral("callout@27 d0 p pad0"),
            QStringLiteral("> paragraph@28 d0 p pad0"),
            QStringLiteral("paragraph@30 d0 p pad0"),
            QStringLiteral("code@32 d0 p pad0"),
            QStringLiteral("table@37 d0 table pad0"),
            QStringLiteral("hr@42 d0 table-hr pad0"),
            QStringLiteral("paragraph@44 d0 hr pad0"),
        };
        QCOMPARE(compactBlocks(raw), expected);

        const QStringList lines = TaskMarkdown::splitLines(raw);
        const QList<QVariantMap> blocks = allBlocks(raw);
        for (const QVariantMap &b : blocks) {
            const QString kind = b.value(QStringLiteral("kind")).toString();
            const int line = b.value(QStringLiteral("sourceLine")).toInt();
            for (const char *key : {"kind", "sourceLine", "endLine", "depth", "inItem", "gap", "listPadAfter", "struck", "text", "inlineHtml", "decorations"}) {
                QVERIFY2(b.contains(QLatin1String(key)), qPrintable(QStringLiteral("L%1 %2 lacks %3").arg(line).arg(kind, QLatin1String(key))));
            }
            if (kind == QLatin1String("task")) {
                QCOMPARE(b.value(QStringLiteral("expectedLineText")).toString(), lines.at(line));
                QCOMPARE(b.value(QStringLiteral("listType")).toString(), QStringLiteral("bullet"));
                QCOMPARE(b.value(QStringLiteral("markerText")).toString(), QString()); // checkbox only, no bullet
            }
        }

        const QVariantList top = MarkdownBlocks::parse(raw);
        auto blockAt = [&](int line) {
            for (const QVariant &v : top) {
                if (v.toMap().value(QStringLiteral("sourceLine")).toInt() == line) {
                    return v.toMap();
                }
            }
            return QVariantMap();
        };
        QCOMPARE(blockAt(0).value(QStringLiteral("level")).toInt(), 1);
        QCOMPARE(blockAt(4).value(QStringLiteral("level")).toInt(), 2);
        QCOMPARE(blockAt(19).value(QStringLiteral("text")).toString(), QStringLiteral("Unchecked task"));
        QCOMPARE(blockAt(8).value(QStringLiteral("text")).toString(), QStringLiteral("Bullet level one A"));

        const QVariantMap callout = blockAt(27);
        QCOMPARE(callout.value(QStringLiteral("calloutType")).toString(), QStringLiteral("note"));
        QCOMPARE(callout.value(QStringLiteral("text")).toString(), QStringLiteral("Callout title"));
        QCOMPARE(callout.value(QStringLiteral("foldable")).toBool(), false);

        const QVariantMap code = blockAt(32);
        QCOMPARE(code.value(QStringLiteral("language")).toString(), QStringLiteral("js"));
        QCOMPARE(code.value(QStringLiteral("codeText")).toString(), QStringLiteral("const answer = 42;\nconsole.log(answer);"));
        QCOMPARE(code.value(QStringLiteral("inlineHtml")).toString(), QString());

        const QVariantMap table = blockAt(37);
        QCOMPARE(table.value(QStringLiteral("align")).toStringList(), (QStringList{QStringLiteral("left"), QStringLiteral("left")}));
        QCOMPARE(table.value(QStringLiteral("header")).toList().size(), 2);
        QCOMPARE(table.value(QStringLiteral("header")).toList().at(1).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("Col B"));
        QCOMPARE(table.value(QStringLiteral("rows")).toList().size(), 2);
        QCOMPARE(table.value(QStringLiteral("rows")).toList().at(1).toList().at(0).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("a2"));
        QCOMPARE(table.value(QStringLiteral("endLine")).toInt(), 40);

        const QVariantMap last = blockAt(44);
        QCOMPARE(last.value(QStringLiteral("text")).toString(), QStringLiteral("Text with bold, italic, strike, highlight, a wikilink and a #tag."));
        const QVariantList decorations = last.value(QStringLiteral("decorations")).toList();
        QCOMPARE(decorations.size(), 1);
        QCOMPARE(decorations.at(0).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("tag"));
    }

    /** The whole torture note: kinds, depths, source lines, task state. */
    void testBlocksTortureFixture()
    {
        const QString raw = QString::fromUtf8(m_fixture);
        const QStringList expected = {
            QStringLiteral("properties@0 d0 none pad0"),
            QStringLiteral("heading@19 d0 properties pad0"),
            QStringLiteral("paragraph@21 d0 p pad0"),
            QStringLiteral("heading@26 d0 heading pad0"),
            QStringLiteral("bullet@28 d1 p pad1"),
            QStringLiteral("bullet@29 d1 none pad1"),
            QStringLiteral("bullet@30 d1 none pad1"),
            QStringLiteral("bullet@31 d1 none pad1"),
            QStringLiteral("bullet@32 d1 none pad1"),
            QStringLiteral("bullet@33 d1 none pad1"),
            QStringLiteral("bullet@34 d1 none pad1"),
            QStringLiteral("bullet@35 d1 none pad1"),
            QStringLiteral("bullet@36 d1 none pad1"),
            QStringLiteral("bullet@37 d1 none pad1"),
            QStringLiteral("bullet@38 d1 none pad1"),
            QStringLiteral("bullet@39 d1 none pad1"),
            QStringLiteral("bullet@40 d1 none pad1"),
            QStringLiteral("bullet@41 d1 none pad1"),
            QStringLiteral("heading@43 d0 heading pad0"),
            QStringLiteral("paragraph@45 d0 p pad0"),
            QStringLiteral("heading@50 d0 heading pad0"),
            QStringLiteral("callout@52 d0 p pad0"),
            QStringLiteral("> paragraph@53 d0 p pad0"),
            QStringLiteral("> paragraph@55 d0 p pad0"),
            QStringLiteral("> task@56 d1 p pad1 [ ]"),
            QStringLiteral("> task@57 d1 none pad1 [x] checked struck"),
            QStringLiteral("callout@59 d0 p pad0"),
            QStringLiteral("> paragraph@60 d0 p pad0"),
            QStringLiteral("callout@62 d0 p pad0"),
            QStringLiteral("> paragraph@63 d0 p pad0"),
            QStringLiteral("> callout@64 d0 p pad0"),
            QStringLiteral("> > paragraph@65 d0 p pad0"),
            QStringLiteral("callout@67 d0 p pad0"),
            QStringLiteral("> paragraph@68 d0 p pad0"),
            QStringLiteral("heading@70 d0 p pad0"),
            QStringLiteral("paragraph@72 d0 p pad0"),
            QStringLiteral("paragraph@74 d0 p pad0"),
            QStringLiteral("bullet@76 d1 p pad1"),
            QStringLiteral("table@78 d0 table pad0"),
            QStringLiteral("heading@83 d0 table-heading pad0"),
            QStringLiteral("paragraph@85 d0 p pad0"),
            QStringLiteral("paragraph@87 d0 p pad0"),
            QStringLiteral("code@90 d0 p pad0"),
            QStringLiteral("paragraph@91 d0 p pad0"),
            QStringLiteral("heading@93 d0 heading pad0"),
            QStringLiteral("paragraph@95 d0 p pad0"),
            QStringLiteral("code@99 d0 p pad0"),
            QStringLiteral("code@107 d0 p pad0"),
            QStringLiteral("heading@109 d0 heading pad0"),
            QStringLiteral("paragraph@111 d0 p pad0"),
            QStringLiteral("code@115 d0 p pad0"),
            QStringLiteral("code@122 d0 p pad0"),
            QStringLiteral("heading@126 d0 heading pad0"),
            QStringLiteral("task@128 d1 p pad1 [ ] toggleable"),
            QStringLiteral("task@129 d1 none pad1 [x] checked toggleable struck"),
            QStringLiteral("task@130 d1 none pad1 [X] checked toggleable struck"),
            QStringLiteral("task@131 d1 p pad1 [ ] toggleable"),
            QStringLiteral("task@132 d1 p pad1 [ ] toggleable"),
            QStringLiteral("task@133 d1 p pad1 [ ] toggleable 1."),
            QStringLiteral("task@134 d1 p pad1 [x] checked toggleable 2. struck"),
            QStringLiteral("task@135 d1 p pad1 [ ] toggleable 10."),
            QStringLiteral("task@136 d1 p pad1 [ ] toggleable"),
            QStringLiteral("task@137 d1 none pad1 [ ] toggleable"),
            QStringLiteral("task@138 d1 none pad1 [ ] toggleable"),
            QStringLiteral("task@139 d1 none pad1 [x] checked toggleable struck"),
            QStringLiteral("task@140 d1 none pad1 [/] checked struck"),
            QStringLiteral("task@141 d1 none pad1 [-] checked struck"),
            QStringLiteral("task@142 d1 none pad1 [>] checked struck"),
            QStringLiteral("task@143 d1 none pad1 [<] checked struck"),
            QStringLiteral("task@144 d1 none pad1 [?] checked struck"),
            QStringLiteral("task@145 d1 none pad1 [!] checked struck"),
            QStringLiteral("task@146 d1 none pad1 [\"] checked struck"),
            QStringLiteral("task@147 d1 none pad1 [*] checked struck"),
            QStringLiteral("task@148 d1 none pad1 [I] checked struck"),
            QStringLiteral("task@149 d1 none pad1 [b] checked struck"),
            QStringLiteral("task@150 d1 none pad1 [k] checked struck"),
            QStringLiteral("task@151 d1 none pad1 [u] checked struck"),
            QStringLiteral("task@152 d1 none pad1 [d] checked struck"),
            QStringLiteral("task@153 d1 none pad1 [ ] toggleable"),
            QStringLiteral("task@154 d1 none pad1 [ ] toggleable"),
            QStringLiteral("task@155 d1 none pad0 [ ] toggleable"),
            QStringLiteral("task@156 d2 none pad0 [ ] toggleable"),
            QStringLiteral("task@157 d3 none pad2 [x] checked toggleable struck"),
            QStringLiteral("task@158 d2 none pad0 [ ] toggleable"),
            QStringLiteral("task@159 d3 none pad3 [>] checked struck"),
            QStringLiteral("task@160 d1 none pad0 [ ] toggleable"),
            QStringLiteral("task@161 d2 none pad2 [ ] toggleable"),
            QStringLiteral("task@162 d1 none pad1 [ ] toggleable"),
            QStringLiteral("blockquote@164 d0 p pad0"),
            QStringLiteral("> task@164 d1 none pad1 [ ]"),
            QStringLiteral("> task@165 d1 none pad1 [x] checked struck"),
            QStringLiteral("heading@167 d0 heading pad0"),
            QStringLiteral("paragraph@169 d0 p pad0"),
            QStringLiteral("bullet@170 d1 p pad1"),
            QStringLiteral("bullet@171 d1 none pad1"),
            QStringLiteral("bullet@172 d1 none pad1"),
            QStringLiteral("bullet@173 d1 none pad1"),
            QStringLiteral("bullet@176 d1 none pad1"),
            QStringLiteral("bullet@177 d1 none pad1"),
            QStringLiteral("bullet@178 d1 none pad1"),
            QStringLiteral("bullet@179 d1 none pad1"),
            QStringLiteral("heading@183 d0 heading pad0"),
            QStringLiteral("code@185 d0 p pad0"),
            QStringLiteral("code@195 d0 p pad0"),
            QStringLiteral("code@200 d0 p pad0"),
            QStringLiteral("code@206 d0 p pad0"),
            QStringLiteral("code@213 d0 p pad0"),
            QStringLiteral("code@218 d0 p pad0"),
            QStringLiteral("paragraph@223 d0 p pad0"),
            QStringLiteral("code@225 d0 p pad0"),
            QStringLiteral("heading@228 d0 heading pad0"),
            QStringLiteral("table@230 d0 table pad0"),
            QStringLiteral("table@238 d0 table pad0"),
            QStringLiteral("heading@242 d0 table-heading pad0"),
            QStringLiteral("paragraph@244 d0 p pad0"),
            QStringLiteral("task@246 d1 p pad1 [ ] toggleable"),
            QStringLiteral("paragraph@249 d0 p pad0"),
            QStringLiteral("paragraph@251 d0 p pad0"),
            QStringLiteral("paragraph@255 d0 p pad0"),
            QStringLiteral("hr@258 d0 hr pad0"),
            QStringLiteral("hr@260 d0 hr pad0"),
            QStringLiteral("hr@262 d0 hr pad0"),
            QStringLiteral("task@264 d1 hr pad1 [ ] toggleable"),
            QStringLiteral("heading@266 d0 heading pad0"),
            QStringLiteral("bullet@268 d1 p pad0"),
            QStringLiteral("code@269 d1 p pad1 inItem"),
            QStringLiteral("bullet@273 d1 none pad0"),
            QStringLiteral("bullet@274 d2 none pad0"),
            QStringLiteral("code@275 d2 p pad2 inItem"),
            QStringLiteral("heading@281 d0 heading pad0"),
            QStringLiteral("code@283 d0 p pad0"),
            QStringLiteral("heading@288 d0 heading pad0"),
            QStringLiteral("bullet@290 d1 p pad0"),
            QStringLiteral("paragraph@292 d1 p pad0 inItem"),
            QStringLiteral("task@294 d2 p pad2 [ ] toggleable"),
            QStringLiteral("paragraph@296 d0 p pad0"),
        };
        const QStringList actual = compactBlocks(raw);
        if (actual != expected) {
            for (int i = 0; i < qMax(actual.size(), expected.size()); ++i) {
                if (actual.value(i) != expected.value(i)) {
                    qWarning("first difference at %d: actual \"%s\" expected \"%s\"", i, qPrintable(actual.value(i)), qPrintable(expected.value(i)));
                    break;
                }
            }
        }
        QCOMPARE(actual, expected);

        const QStringList lines = TaskMarkdown::splitLines(raw);
        const QList<QVariantMap> blocks = allBlocks(raw);
        int tasks = 0;
        QSet<int> taskLines;
        QSet<int> bulletLines;
        for (const QVariantMap &b : blocks) {
            const QString kind = b.value(QStringLiteral("kind")).toString();
            const int line = b.value(QStringLiteral("sourceLine")).toInt();
            QVERIFY2(line >= 0 && line < lines.size(), qPrintable(kind));
            QVERIFY(b.value(QStringLiteral("endLine")).toInt() >= line);
            if (kind == QLatin1String("bullet")) {
                bulletLines.insert(line);
            }
            if (kind != QLatin1String("task")) {
                QVERIFY2(!b.contains(QStringLiteral("checked")), qPrintable(QStringLiteral("L%1").arg(line)));
                continue;
            }
            ++tasks;
            taskLines.insert(line);
            QCOMPARE(b.value(QStringLiteral("expectedLineText")).toString(), lines.at(line));
            const QString listType = b.value(QStringLiteral("listType")).toString();
            if (listType == QLatin1String("bullet")) {
                // Obsidian draws ONLY the checkbox for a task: no bullet marker.
                QVERIFY2(b.value(QStringLiteral("markerText")).toString().isEmpty(), qPrintable(QStringLiteral("L%1 carries a bullet marker").arg(line)));
            } else {
                QCOMPARE(listType, QStringLiteral("ordered"));
                QVERIFY(b.value(QStringLiteral("markerText")).toString().endsWith(QLatin1Char('.')));
            }
            const QString taskChar = b.value(QStringLiteral("taskChar")).toString();
            QCOMPARE(taskChar.size(), 1);
            QCOMPARE(b.value(QStringLiteral("checked")).toBool(), taskChar != QLatin1String(" "));
            QVERIFY(b.value(QStringLiteral("struck")).toBool() || taskChar == QLatin1String(" "));
            QVERIFY(!b.value(QStringLiteral("inlineHtml")).toString().contains(QStringLiteral("[ ]")));
            QVERIFY(!b.value(QStringLiteral("inlineHtml")).toString().contains(QChar(0x2022)));
            if (b.value(QStringLiteral("_nested")).toBool()) {
                QVERIFY2(!b.value(QStringLiteral("toggleable")).toBool(), qPrintable(QStringLiteral("quoted task L%1 is toggleable").arg(line)));
            }
        }
        QCOMPARE(tasks, 42);
        QVERIFY2(!taskLines.intersects(bulletLines), "a task line also produced a bullet block");

        // Line 128 (0-based) is the first real task; 196 and 225 are fenced / indented code.
        QVERIFY(taskLines.contains(128));
        QVERIFY(!taskLines.contains(196));
        QVERIFY(!taskLines.contains(225));
    }

    /** {i | task && toggleable} == {i | isTaskLineInDocument(raw, i)} for every fixture. */
    void testToggleableTasksMatchTheToggleGuard_data()
    {
        QTest::addColumn<QString>("raw");
        QTest::newRow("torture") << QString::fromUtf8(m_fixture);
        QTest::newRow("format-reference") << QString::fromUtf8(readAll(QStringLiteral(FORMAT_REFERENCE_FIXTURE)));
        const QStringList fixtures = inlineFixtures();
        for (int i = 0; i < fixtures.size(); ++i) {
            QTest::newRow(qPrintable(QStringLiteral("inline-%1").arg(i))) << fixtures.at(i);
        }
    }

    void testToggleableTasksMatchTheToggleGuard()
    {
        QFETCH(QString, raw);
        QSet<int> fromBlocks;
        for (const QVariantMap &b : allBlocks(raw)) {
            if (b.value(QStringLiteral("kind")).toString() == QLatin1String("task") && b.value(QStringLiteral("toggleable")).toBool()) {
                const int line = b.value(QStringLiteral("sourceLine")).toInt();
                QVERIFY2(!fromBlocks.contains(line), "two toggleable task blocks for one line");
                fromBlocks.insert(line);
            }
        }
        QSet<int> fromGuard;
        const int n = TaskMarkdown::splitLines(raw).size();
        for (int i = 0; i < n; ++i) {
            if (TaskMarkdown::isTaskLineInDocument(raw, i)) {
                fromGuard.insert(i);
            }
        }
        QVERIFY(!fromGuard.isEmpty());
        QCOMPARE(fromBlocks, fromGuard);
    }

    void testBlocksFencedAndIndentedTasksAreCode()
    {
        const QString raw = QStringLiteral("```\n- [ ] fenced\n```\n\n    - [ ] indented\n\n- item\n    ```\n    - [ ] fenced in item\n    ```\n");
        QStringList kinds;
        for (const QVariantMap &b : allBlocks(raw)) {
            QVERIFY2(b.value(QStringLiteral("kind")).toString() != QLatin1String("task"), qPrintable(b.value(QStringLiteral("text")).toString()));
            kinds.append(b.value(QStringLiteral("kind")).toString() + QLatin1Char('@') + b.value(QStringLiteral("sourceLine")).toString());
        }
        QCOMPARE(kinds, (QStringList{QStringLiteral("code@0"), QStringLiteral("code@4"), QStringLiteral("bullet@6"), QStringLiteral("code@7")}));
        const QList<QVariantMap> blocks = allBlocks(raw);
        QCOMPARE(blocks.at(0).value(QStringLiteral("codeText")).toString(), QStringLiteral("- [ ] fenced"));
        QCOMPARE(blocks.at(1).value(QStringLiteral("codeText")).toString(), QStringLiteral("- [ ] indented"));
        QCOMPARE(blocks.at(3).value(QStringLiteral("codeText")).toString(), QStringLiteral("- [ ] fenced in item"));
        QCOMPARE(blocks.at(3).value(QStringLiteral("inItem")).toBool(), true);
        QCOMPARE(blocks.at(3).value(QStringLiteral("depth")).toInt(), 1);
        QCOMPARE(blocks.at(3).value(QStringLiteral("listPadAfter")).toInt(), 1);
    }

    void testBlocksFrontmatterBecomesProperties()
    {
        const QString raw = QString::fromUtf8(m_fixture);
        const QVariantList top = MarkdownBlocks::parse(raw);
        QVERIFY(!top.isEmpty());
        const QVariantMap props = top.first().toMap();
        QCOMPARE(props.value(QStringLiteral("kind")).toString(), QStringLiteral("properties"));
        QCOMPARE(props.value(QStringLiteral("sourceLine")).toInt(), 0);
        QCOMPARE(props.value(QStringLiteral("endLine")).toInt(), 17);
        QCOMPARE(props.value(QStringLiteral("inlineHtml")).toString(), QString());

        QMap<QString, QVariantMap> byKey;
        QStringList keys;
        for (const QVariant &v : props.value(QStringLiteral("properties")).toList()) {
            const QVariantMap p = v.toMap();
            keys.append(p.value(QStringLiteral("key")).toString());
            byKey.insert(keys.last(), p);
        }
        QCOMPARE(keys,
                 (QStringList{QStringLiteral("title"), QStringLiteral("aliases"), QStringLiteral("tags"), QStringLiteral("created"), QStringLiteral("cssclasses"),
                              QStringLiteral("publish"), QStringLiteral("not_a_task"), QStringLiteral("fake_fence"), QStringLiteral("block_scalar"),
                              QStringLiteral("list_of_maps")}));
        QCOMPARE(byKey[QStringLiteral("title")].value(QStringLiteral("value")).toString(), QStringLiteral("Torture Test — Obsidian Flavored Markdown"));
        QCOMPARE(byKey[QStringLiteral("aliases")].value(QStringLiteral("values")).toStringList(),
                 (QStringList{QStringLiteral("torture"), QStringLiteral("md fixture"), QStringLiteral("Ščžđ-unicode")}));
        QCOMPARE(byKey[QStringLiteral("tags")].value(QStringLiteral("values")).toStringList(), (QStringList{QStringLiteral("test/fixture"), QStringLiteral("quoted tag")}));
        QCOMPARE(byKey[QStringLiteral("cssclasses")].value(QStringLiteral("values")).toStringList(), (QStringList{QStringLiteral("wide-table")}));
        QCOMPARE(byKey[QStringLiteral("publish")].value(QStringLiteral("value")).toString(), QStringLiteral("false"));
        QVERIFY(byKey[QStringLiteral("block_scalar")].value(QStringLiteral("value")).toString().startsWith(QStringLiteral("|\n")));
        QCOMPARE(byKey[QStringLiteral("block_scalar")].value(QStringLiteral("values")).toStringList(), QStringList());

        // Nothing from inside the frontmatter becomes a body block.
        for (int i = 1; i < top.size(); ++i) {
            QVERIFY2(top.at(i).toMap().value(QStringLiteral("sourceLine")).toInt() > 17, "a body block starts inside the frontmatter");
        }
        QCOMPARE(top.at(1).toMap().value(QStringLiteral("gap")).toString(), QStringLiteral("properties"));

        // "---" that is not frontmatter is a rule, and there is no properties block.
        const QVariantList rule = MarkdownBlocks::parse(QStringLiteral("---\n\ntext\n"));
        QCOMPARE(rule.first().toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("hr"));
        QVERIFY(MarkdownBlocks::parse(QString()).isEmpty());
    }

    void testBlocksExpectedLineTextKeepsCarriageReturn()
    {
        const QString raw = QStringLiteral("# t\r\n\r\n- [ ] one\r\n- [x] two\r\n");
        const QList<QVariantMap> blocks = allBlocks(raw);
        QStringList expected;
        for (const QVariantMap &b : blocks) {
            if (b.value(QStringLiteral("kind")).toString() == QLatin1String("task")) {
                expected.append(b.value(QStringLiteral("expectedLineText")).toString());
                QVERIFY(b.value(QStringLiteral("toggleable")).toBool());
                QVERIFY(!b.value(QStringLiteral("text")).toString().contains(QLatin1Char('\r')));
            }
        }
        QCOMPARE(expected, (QStringList{QStringLiteral("- [ ] one\r"), QStringLiteral("- [x] two\r")}));

        // And the pair really passes the file-level toggle.
        QString line = expected.at(0);
        QVERIFY(TaskMarkdown::toggleTaskLine(line));
        QCOMPARE(line, QStringLiteral("- [x] one\r"));
    }

    void testInlineHtmlGoldens_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("html");
        const QString p0 = QStringLiteral("<p style=\"margin:0\">");
        const QString p1 = QStringLiteral("</p>");
        const QString codePad = QStringLiteral("<span style=\"letter-spacing:%CODEPADX%px\">&#8288;</span>");
        const QString tagPad = QStringLiteral("<span style=\"letter-spacing:%TAGPADX%px\">&#8288;</span>");
        QTest::newRow("bold") << QStringLiteral("a **b** c") << p0 + QStringLiteral("a <span style=\"font-weight:600\">b</span> c") + p1;
        QTest::newRow("bold-underscore") << QStringLiteral("__b__") << p0 + QStringLiteral("<span style=\"font-weight:600\">b</span>") + p1;
        QTest::newRow("italic") << QStringLiteral("*i* and _j_") << p0 + QStringLiteral("<i>i</i> and <i>j</i>") + p1;
        QTest::newRow("bold-italic") << QStringLiteral("***x***") << p0 + QStringLiteral("<span style=\"font-weight:600\"><i>x</i></span>") + p1;
        QTest::newRow("intraword-underscore") << QStringLiteral("snake_case_name") << p0 + QStringLiteral("snake_case_name") + p1;
        QTest::newRow("strike") << QStringLiteral("~~s~~") << p0 + QStringLiteral("<s>s</s>") + p1;
        QTest::newRow("highlight") << QStringLiteral("==h==") << p0 + QStringLiteral("<span style=\"background-color:%MARKBG%\">h</span>") + p1;
        QTest::newRow("code") << QStringLiteral("x `a<b` y")
                              << p0 + QStringLiteral("x ") + codePad
                + QStringLiteral("<span style=\"font-family:'%MONO%';font-size:%CODEPT%pt;color:%CODEFG%\">a&lt;b</span>") + codePad + QStringLiteral(" y") + p1;
        QTest::newRow("code-hides-markup") << QStringLiteral("`**no**`")
                                           << p0 + codePad + QStringLiteral("<span style=\"font-family:'%MONO%';font-size:%CODEPT%pt;color:%CODEFG%\">**no**</span>") + codePad + p1;
        QTest::newRow("wikilink") << QStringLiteral("[[Note]]") << p0 + QStringLiteral("<a href=\"obsnote:wiki/Note\" style=\"color:%LINK%\">Note</a>") + p1;
        QTest::newRow("wikilink-alias") << QStringLiteral("[[Some Note|shown]]")
                                        << p0 + QStringLiteral("<a href=\"obsnote:wiki/Some&#37;20Note\" style=\"color:%LINK%\">shown</a>") + p1;
        QTest::newRow("embed") << QStringLiteral("![[pic.png|300x200]]")
                               << p0 + QStringLiteral("<a href=\"obsnote:wiki/pic.png\" style=\"color:%LINK%\">pic.png</a>") + p1;
        QTest::newRow("image") << QStringLiteral("![alt text](https://example.com/i.png \"t\")")
                               << p0 + QStringLiteral("<a href=\"https://example.com/i.png\" style=\"color:%LINK%\">alt text</a>") + p1;
        QTest::newRow("link") << QStringLiteral("[site](https://example.com/?a=1&b=2)")
                              << p0 + QStringLiteral("<a href=\"https://example.com/?a=1&amp;b=2\" style=\"color:%LINK%\">site</a>") + p1;
        QTest::newRow("tag") << QStringLiteral("see #project/alpha now")
                             << p0 + QStringLiteral("see <a href=\"obsnote:tag/project/alpha\" style=\"text-decoration:none\">") + tagPad
                + QStringLiteral("<span style=\"font-size:%TAGPT%pt;color:%TAGFG%\">#project/alpha</span>") + tagPad + QStringLiteral("</a> now") + p1;
        QTest::newRow("not-tags") << QStringLiteral("C# #12345 # alone") << p0 + QStringLiteral("C# #12345 # alone") + p1;
        QTest::newRow("script-escaped") << QStringLiteral("<script>alert(\"x\")</script>")
                                        << p0 + QStringLiteral("&lt;script&gt;alert(&quot;x&quot;)&lt;/script&gt;") + p1;
        QTest::newRow("img-tag-escaped") << QStringLiteral("<img src=\"http://evil/x.png\">")
                                         << p0 + QStringLiteral("&lt;img src=&quot;<a href=\"http://evil/x.png\" style=\"color:%LINK%\">http://evil/x.png</a>&quot;&gt;") + p1;
        QTest::newRow("placeholder-in-text-is-inert") << QStringLiteral("100%MONO%") << p0 + QStringLiteral("100&#37;MONO&#37;") + p1;
        QTest::newRow("newline-is-br") << QStringLiteral("one  \ntwo\\\nthree") << p0 + QStringLiteral("one<br>two<br>three") + p1;
        QTest::newRow("comment-hidden") << QStringLiteral("a %%hidden%% b") << p0 + QStringLiteral("a b") + p1;
        QTest::newRow("escapes") << QStringLiteral("\\*not\\* \\[\\[x\\]\\]") << p0 + QStringLiteral("*not* [[x]]") + p1;
        QTest::newRow("whitespace-collapses") << QStringLiteral("a    b") << p0 + QStringLiteral("a b") + p1;
    }

    void testInlineHtmlGoldens()
    {
        QFETCH(QString, source);
        QFETCH(QString, html);
        QCOMPARE(MarkdownBlocks::inlineHtml(source), html);
    }

    /** Decoration offsets are QTextDocument positions of the resolved rich text. */
    void testDecorationOffsetsMatchQTextDocument()
    {
        QString raw = QString::fromUtf8(m_fixture);
        raw += QStringLiteral("\n\nMixed `one` and #two with **`bold code`** then [[L|#nottag]] `a  b` end #last\n");
        raw += QStringLiteral("| `c1` | #t2 |\n|---|---|\n| x `y` | z |\n");
        int checked = 0;
        auto verify = [&](const QString &html, const QVariantList &decorations, const QString &text, const QString &where) {
            if (html.isEmpty()) {
                return;
            }
            QTextDocument doc;
            doc.setHtml(resolvePlaceholders(html));
            QString plain = doc.toPlainText();
            // Our plain text is the document's, minus the pill padding characters.
            QString visible = plain;
            visible.remove(QChar(0x2060));
            visible.replace(QChar(0x00A0), QLatin1Char(' '));
            QCOMPARE(visible, text);
            for (const QVariant &v : decorations) {
                const QVariantMap d = v.toMap();
                const int start = d.value(QStringLiteral("start")).toInt();
                const int length = d.value(QStringLiteral("length")).toInt();
                const QString type = d.value(QStringLiteral("type")).toString();
                QVERIFY2(length > 0, qPrintable(where));
                QVERIFY2(start >= 1 && start + length < plain.size(), qPrintable(where));
                QVERIFY2(plain.at(start - 1) == QChar(0x2060), qPrintable(where + QStringLiteral(": no pad before ") + type));
                QVERIFY2(plain.at(start + length) == QChar(0x2060), qPrintable(where + QStringLiteral(": no pad after ") + type));
                QVERIFY2(!plain.mid(start, length).contains(QChar(0x2060)), qPrintable(where));
                const QTextCharFormat inside = formatAt(doc, start);
                const QTextCharFormat padBefore = formatAt(doc, start - 1);
                // Qt stores letter-spacing in 1/64 px steps (4.2 -> 4.1875).
                QVERIFY2(qAbs(padBefore.fontLetterSpacing() - (type == QLatin1String("tag") ? 9.1 : 4.2)) <= 1.0 / 64, qPrintable(where));
                if (type == QLatin1String("tag")) {
                    QVERIFY2(plain.at(start) == QLatin1Char('#'), qPrintable(where));
                    QVERIFY2(inside.anchorHref().startsWith(QStringLiteral("obsnote:tag/")), qPrintable(where + inside.anchorHref()));
                    QCOMPARE(inside.foreground().color(), QColor(QStringLiteral("#0000aa")));
                } else {
                    QCOMPARE(type, QStringLiteral("code"));
                    QVERIFY2(inside.fontFamilies().toStringList().contains(QStringLiteral("DejaVu Sans Mono")), qPrintable(where));
                    QCOMPARE(inside.foreground().color(), QColor(QStringLiteral("#aa0000")));
                }
                ++checked;
            }
        };
        std::function<void(const QVariantList &)> walk = [&](const QVariantList &blocks) {
            for (const QVariant &v : blocks) {
                const QVariantMap b = v.toMap();
                const QString where = QStringLiteral("L%1").arg(b.value(QStringLiteral("sourceLine")).toInt());
                verify(b.value(QStringLiteral("inlineHtml")).toString(), b.value(QStringLiteral("decorations")).toList(), b.value(QStringLiteral("text")).toString(), where);
                if (b.contains(QStringLiteral("header"))) {
                    QVariantList rows = b.value(QStringLiteral("rows")).toList();
                    rows.prepend(QVariant(b.value(QStringLiteral("header")).toList()));
                    for (const QVariant &row : rows) {
                        for (const QVariant &c : row.toList()) {
                            const QVariantMap cell = c.toMap();
                            verify(cell.value(QStringLiteral("inlineHtml")).toString(), cell.value(QStringLiteral("decorations")).toList(), cell.value(QStringLiteral("text")).toString(), where + QStringLiteral(" cell"));
                        }
                    }
                }
                walk(b.value(QStringLiteral("children")).toList());
            }
        };
        walk(MarkdownBlocks::parse(raw));
        QVERIFY2(checked >= 20, qPrintable(QString::number(checked)));

        // Links keep their percent-encoding through the %-escaping.
        QTextDocument doc;
        doc.setHtml(resolvePlaceholders(MarkdownBlocks::inlineHtml(QStringLiteral("[[Some Note|x]]"))));
        QCOMPARE(formatAt(doc, 0).anchorHref(), QStringLiteral("obsnote:wiki/Some%20Note"));
        QCOMPARE(TaskMarkdown::parseWikiLink(formatAt(doc, 0).anchorHref()), QStringLiteral("Some Note"));
    }

    /**
     * "marks" ranges (the view paints ==highlight== itself) are exactly the
     * QTextDocument positions whose format carries the %MARKBG% background.
     */
    void testMarkOffsetsMatchQTextDocument()
    {
        QString raw = QString::fromUtf8(m_fixture);
        raw += QStringLiteral("\n\nA ==first== then ==two words== and ==`code` #tag [[L|x]]== end\n");
        raw += QStringLiteral("line one ==spans  \nthe break== after\n");
        raw += QStringLiteral("| ==c1== | x ==c2== |\n|---|---|\n| ==a== | b |\n");
        raw += QStringLiteral("> [!tip] Title ==marked==\n> body\n");
        const QColor markBg(QStringLiteral("#ffff00"));
        int checked = 0;
        auto verify = [&](const QString &html, const QVariantList &marks, const QString &where) {
            if (html.isEmpty()) {
                QVERIFY2(marks.isEmpty(), qPrintable(where));
                return;
            }
            QTextDocument doc;
            doc.setHtml(resolvePlaceholders(html));
            const int length = doc.characterCount() - 1;
            QList<bool> expected(length, false);
            for (const QVariant &v : marks) {
                const QVariantMap m = v.toMap();
                const int start = m.value(QStringLiteral("start")).toInt();
                const int len = m.value(QStringLiteral("length")).toInt();
                QVERIFY2(len > 0 && start >= 0 && start + len <= length, qPrintable(where));
                for (int p = start; p < start + len; ++p) {
                    expected[p] = true;
                }
                ++checked;
            }
            for (int p = 0; p < length; ++p) {
                if (doc.characterAt(p) == QChar::ParagraphSeparator || doc.characterAt(p) == QChar::LineSeparator) {
                    continue;
                }
                const bool painted = formatAt(doc, p).background().color() == markBg;
                QVERIFY2(painted == expected.at(p),
                         qPrintable(QStringLiteral("%1: position %2 (%3) background %4, marks say %5")
                                        .arg(where).arg(p).arg(doc.characterAt(p)).arg(painted).arg(expected.at(p))));
            }
        };
        std::function<void(const QVariantList &)> walk = [&](const QVariantList &blocks) {
            for (const QVariant &v : blocks) {
                const QVariantMap b = v.toMap();
                const QString where = QStringLiteral("L%1").arg(b.value(QStringLiteral("sourceLine")).toInt());
                verify(b.value(QStringLiteral("inlineHtml")).toString(), b.value(QStringLiteral("marks")).toList(), where);
                if (b.contains(QStringLiteral("titleHtml"))) {
                    verify(b.value(QStringLiteral("titleHtml")).toString(), b.value(QStringLiteral("titleMarks")).toList(), where + QStringLiteral(" title"));
                }
                if (b.contains(QStringLiteral("header"))) {
                    QVariantList rows = b.value(QStringLiteral("rows")).toList();
                    rows.prepend(QVariant(b.value(QStringLiteral("header")).toList()));
                    for (const QVariant &row : rows) {
                        for (const QVariant &c : row.toList()) {
                            const QVariantMap cell = c.toMap();
                            verify(cell.value(QStringLiteral("inlineHtml")).toString(), cell.value(QStringLiteral("marks")).toList(), where + QStringLiteral(" cell"));
                        }
                    }
                }
                walk(b.value(QStringLiteral("children")).toList());
            }
        };
        walk(MarkdownBlocks::parse(raw));
        // Torture fixture (3) + the lines above (3 + 1 wrapped + 3 cells + 1 title).
        QVERIFY2(checked >= 11, qPrintable(QString::number(checked)));
    }

    void testBlocksListShapes()
    {
        // A multi-line list item is ONE block joined with <br>.
        {
            const QVariantList b = MarkdownBlocks::parse(QStringLiteral("- first line\n  second line\nlazy third\n- next\n"));
            QCOMPARE(b.size(), 2);
            const QVariantMap item = b.at(0).toMap();
            QCOMPARE(item.value(QStringLiteral("endLine")).toInt(), 2);
            QCOMPARE(item.value(QStringLiteral("inlineHtml")).toString(), QStringLiteral("<p style=\"margin:0\">first line<br>second line<br>lazy third</p>"));
            QCOMPARE(item.value(QStringLiteral("text")).toString(), QStringLiteral("first line\nsecond line\nlazy third"));
            QCOMPARE(b.at(1).toMap().value(QStringLiteral("gap")).toString(), QStringLiteral("none"));
        }
        // A continuation paragraph after a blank line is its own inItem block.
        {
            const QStringList c = compactBlocks(QStringLiteral("- item\n\n  continued\n- next\n\nafter\n"));
            QCOMPARE(c,
                     (QStringList{QStringLiteral("bullet@0 d1 none pad0"), QStringLiteral("paragraph@2 d1 p pad1 inItem"), QStringLiteral("bullet@3 d1 none pad1"),
                                  QStringLiteral("paragraph@5 d0 p pad0")}));
            // After a paragraph the list's first item takes the paragraph gap.
            QCOMPARE(compactBlocks(QStringLiteral("para\n\n- a\n")).last(), QStringLiteral("bullet@2 d1 p pad1"));
        }
        // Ordered markers count from the list start whatever the source numbers say.
        {
            QStringList markers;
            for (const QVariantMap &b : allBlocks(QStringLiteral("3. a\n3. b\n3) c\n"))) {
                markers.append(b.value(QStringLiteral("markerText")).toString());
            }
            QCOMPARE(markers, (QStringList{QStringLiteral("3."), QStringLiteral("4."), QStringLiteral("3.")})); // ")" starts a new list
            markers.clear();
            for (const QVariantMap &b : allBlocks(QStringLiteral("3. a\n3. b\n3. [ ] c\n"))) {
                markers.append(b.value(QStringLiteral("markerText")).toString());
            }
            QCOMPARE(markers, (QStringList{QStringLiteral("3."), QStringLiteral("4."), QStringLiteral("5.")}));
        }
        // A checked task strikes its whole subtree.
        {
            const QStringList c = compactBlocks(QStringLiteral("- [x] done parent\n    - child\n        - [ ] grandchild\n\n    more\n- [ ] open\n    - kid\n"));
            QCOMPARE(c,
                     (QStringList{QStringLiteral("task@0 d1 none pad0 [x] checked toggleable struck"), QStringLiteral("bullet@1 d2 none pad0 struck"),
                                  QStringLiteral("task@2 d3 none pad2 [ ] toggleable struck"), QStringLiteral("paragraph@4 d1 p pad1 inItem struck"),
                                  QStringLiteral("task@5 d1 none pad0 [ ] toggleable"), QStringLiteral("bullet@6 d2 none pad2")}));
        }
        // Different bullet characters are different lists (CommonMark), tasks share a "-" list.
        QCOMPARE(compactBlocks(QStringLiteral("- a\n- [ ] b\n* c\n")),
                 (QStringList{QStringLiteral("bullet@0 d1 none pad1"), QStringLiteral("task@1 d1 none pad1 [ ] toggleable"), QStringLiteral("bullet@2 d1 p pad1")}));
    }

    void testBlocksOtherShapes()
    {
        // Setext headings, math, spacing tokens around tables and rules.
        QCOMPARE(compactBlocks(QStringLiteral("Title\n===\n\ntext\n\nSub\n---\n\n| a |\n|---|\n| 1 |\n\n# After table\n\n| b |\n|:-:|\n\n---\n\n| c |\n|--:|\n\n$$\nx^2\n$$\n")),
                 (QStringList{QStringLiteral("heading@0 d0 none pad0"), QStringLiteral("paragraph@3 d0 p pad0"), QStringLiteral("heading@5 d0 heading pad0"),
                              QStringLiteral("table@8 d0 table pad0"), QStringLiteral("heading@12 d0 table-heading pad0"), QStringLiteral("table@14 d0 table pad0"),
                              QStringLiteral("hr@17 d0 table-hr pad0"), QStringLiteral("table@19 d0 table-hr pad0"), QStringLiteral("code@22 d0 table pad0")}));
        const QVariantList other = MarkdownBlocks::parse(QStringLiteral("| a | b \\| c | d |\n|:--|:-:|--:|\n| `x` | y |\n\n$$\nx^2\n$$\n"));
        const QVariantMap table = other.at(0).toMap();
        QCOMPARE(table.value(QStringLiteral("align")).toStringList(), (QStringList{QStringLiteral("left"), QStringLiteral("center"), QStringLiteral("right")}));
        QCOMPARE(table.value(QStringLiteral("header")).toList().at(1).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("b | c"));
        const QVariantList row = table.value(QStringLiteral("rows")).toList().at(0).toList();
        QCOMPARE(row.size(), 3); // padded to the header width
        QCOMPARE(row.at(0).toMap().value(QStringLiteral("decorations")).toList().size(), 1);
        QCOMPARE(row.at(2).toMap().value(QStringLiteral("text")).toString(), QString());
        const QVariantMap math = other.at(1).toMap();
        QCOMPARE(math.value(QStringLiteral("language")).toString(), QStringLiteral("math"));
        QCOMPARE(math.value(QStringLiteral("codeText")).toString(), QStringLiteral("x^2"));

        // Callouts: alias normalisation, fold state, default title, first child gap.
        const QVariantList callouts = MarkdownBlocks::parse(QStringLiteral("> [!FAQ]- \n> body\n\n> [!caution]+ Custom *title*\n> text\n\n> plain quote\n> > nested\n"));
        QCOMPARE(callouts.size(), 3);
        const QVariantMap faq = callouts.at(0).toMap();
        QCOMPARE(faq.value(QStringLiteral("calloutType")).toString(), QStringLiteral("question"));
        QCOMPARE(faq.value(QStringLiteral("foldable")).toBool(), true);
        QCOMPARE(faq.value(QStringLiteral("folded")).toBool(), true);
        QCOMPARE(faq.value(QStringLiteral("titleHtml")).toString(), QStringLiteral("<p style=\"margin:0\">Faq</p>"));
        QCOMPARE(faq.value(QStringLiteral("children")).toList().at(0).toMap().value(QStringLiteral("gap")).toString(), QStringLiteral("p"));
        const QVariantMap caution = callouts.at(1).toMap();
        QCOMPARE(caution.value(QStringLiteral("calloutType")).toString(), QStringLiteral("warning"));
        QCOMPARE(caution.value(QStringLiteral("folded")).toBool(), false);
        QCOMPARE(caution.value(QStringLiteral("foldable")).toBool(), true);
        QCOMPARE(caution.value(QStringLiteral("titleHtml")).toString(), QStringLiteral("<p style=\"margin:0\">Custom <i>title</i></p>"));
        const QVariantMap quote = callouts.at(2).toMap();
        QCOMPARE(quote.value(QStringLiteral("kind")).toString(), QStringLiteral("blockquote"));
        const QVariantList qc = quote.value(QStringLiteral("children")).toList();
        QCOMPARE(qc.size(), 2); // "> > nested" interrupts the paragraph: a nested blockquote
        QCOMPARE(qc.at(0).toMap().value(QStringLiteral("gap")).toString(), QStringLiteral("none"));
        QCOMPARE(qc.at(1).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("blockquote"));
        QCOMPARE(qc.at(1).toMap().value(QStringLiteral("sourceLine")).toInt(), 7);

        const QStringList aliases = {QStringLiteral("summary:abstract"), QStringLiteral("tldr:abstract"), QStringLiteral("hint:tip"), QStringLiteral("important:tip"),
                                     QStringLiteral("check:success"), QStringLiteral("done:success"), QStringLiteral("help:question"), QStringLiteral("caution:warning"),
                                     QStringLiteral("attention:warning"), QStringLiteral("fail:failure"), QStringLiteral("missing:failure"), QStringLiteral("error:danger"),
                                     QStringLiteral("cite:quote"), QStringLiteral("bug:bug")};
        for (const QString &pair : aliases) {
            const QVariantMap c = MarkdownBlocks::parse(QStringLiteral("> [!%1]\n").arg(pair.section(QLatin1Char(':'), 0, 0))).at(0).toMap();
            QCOMPARE(c.value(QStringLiteral("calloutType")).toString(), pair.section(QLatin1Char(':'), 1, 1));
        }
    }
};

// QTextDocument (decoration offsets) needs a QGuiApplication; QtGui is linked, so
// QTEST_MAIN provides one. The test still runs headless (QT_QPA_PLATFORM=offscreen).
QTEST_MAIN(TstTaskMarkdown)
#include "tst_taskmarkdown.moc"
