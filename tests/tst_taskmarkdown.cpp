/*
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
    SPDX-License-Identifier: GPL-2.0-or-later

    Guiless functional smoke test for the Obsidian Note applet backend.
    Runs against tests/obsidian-torture.md (a copy of /tmp/obsidian-torture.md).
*/

#include "markdownnote.h"
#include "taskmarkdown.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QTest>

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
};

QTEST_GUILESS_MAIN(TstTaskMarkdown)
#include "tst_taskmarkdown.moc"
