/*
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
    SPDX-License-Identifier: GPL-2.0-or-later

    File-safety tests for MarkdownNote. One test per rule of the pinned policy:

      S2 byte fidelity  - CRLF / CR / mixed / U+2028 line endings, UTF-8 BOM,
                          trailing newline, invalid UTF-8 -> read-only.
      S3 no blind write - a save aborts when the fingerprint moved underneath.
      S4 autosave       - no write while externalChangePending.
      S5 reload         - reloadFromDisk() never saves first.
      S6 toggle         - self-validating: stale expected text, a fenced line and
                          a literal obsnote:toggle/N link in the body are refused.
      S7 atomic         - no in-place-truncate fallback, ever.
*/

#include "markdownnote.h"
#include "taskmarkdown.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace
{

QByteArray readBytes(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return QByteArray("<unreadable>");
    }
    return f.readAll();
}

bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    const bool ok = f.write(bytes) == bytes.size();
    f.close();
    return ok;
}

/** What a QQuickTextEdit does to a buffer: every line ending becomes "\n". */
QString normaliseLikeTextEdit(const QString &text)
{
    QString out = text;
    out.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    out.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    out.replace(QChar(0x2028), QLatin1Char('\n'));
    out.replace(QChar(0x2029), QLatin1Char('\n'));
    return out;
}

} // namespace

class TstMarkdownNote : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString makeFile(const QString &name, const QByteArray &bytes)
    {
        const QString path = m_dir.filePath(name);
        if (!writeBytes(path, bytes)) {
            return QString();
        }
        return path;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY2(m_dir.isValid(), qPrintable(m_dir.errorString()));
    }

    // ------------------------------------------------------------------ S2 EOL

    /**
     * A CRLF file round-trips through an EDIT-mode commit byte-identically apart
     * from the one line the user actually retyped. The QML editor hands over an
     * LF-only buffer; the restoration happens in C++.
     */
    void testCrlfSurvivesEditCommit()
    {
        const QByteArray original = "# Title\r\n\r\n- [ ] one\r\n- [x] two\r\n";
        const QString path = makeFile(QStringLiteral("crlf.md"), original);
        QVERIFY(!path.isEmpty());

        MarkdownNote note;
        note.setPath(path);
        QCOMPARE(note.status(), MarkdownNote::Ready);
        QCOMPARE(note.lineEnding(), QStringLiteral("crlf"));
        QVERIFY(!note.mixedLineEndings());
        QCOMPARE(note.rawText().toUtf8(), original);

        // 1. The editor buffer is pure LF, exactly like QQuickTextEdit's.
        const QString buffer = note.editorText();
        QCOMPARE(buffer, normaliseLikeTextEdit(note.rawText()));
        QVERIFY(!buffer.contains(QLatin1Char('\r')));
        QCOMPARE(buffer.toUtf8(), QByteArray("# Title\n\n- [ ] one\n- [x] two\n"));

        // 2. Entering and leaving EDIT without typing writes nothing at all.
        QSignalSpy savedSpy(&note, &MarkdownNote::saved);
        const QDateTime before = QFileInfo(path).lastModified();
        QVERIFY(note.saveBuffer(buffer));
        QCOMPARE(savedSpy.count(), 0);
        QCOMPARE(readBytes(path), original);
        QCOMPARE(QFileInfo(path).lastModified(), before);

        // 3. One retyped line: every CRLF is still a CRLF.
        QString edited = buffer;
        edited.replace(QStringLiteral("- [x] two"), QStringLiteral("- [x] two and a half"));
        QVERIFY(note.saveBuffer(edited));
        QCOMPARE(savedSpy.count(), 1);
        QCOMPARE(readBytes(path), QByteArray("# Title\r\n\r\n- [ ] one\r\n- [x] two and a half\r\n"));
        QCOMPARE(note.rawText().toUtf8(), readBytes(path));

        // 4. The legacy save() entry point (what the old QML calls) is protected too.
        QString again = note.editorText();
        again.replace(QStringLiteral("# Title"), QStringLiteral("# Header"));
        QVERIFY(note.save(again));
        QCOMPARE(readBytes(path), QByteArray("# Header\r\n\r\n- [ ] one\r\n- [x] two and a half\r\n"));
    }

    /** A CR-only (classic Mac) file likewise. */
    void testCrOnlySurvivesEditCommit()
    {
        const QByteArray original = "# Title\r\r- [ ] one\r- [x] two\r";
        const QString path = makeFile(QStringLiteral("cr.md"), original);
        QVERIFY(!path.isEmpty());

        MarkdownNote note;
        note.setPath(path);
        QCOMPARE(note.status(), MarkdownNote::Ready);
        QCOMPARE(note.lineEnding(), QStringLiteral("cr"));
        QCOMPARE(note.rawText().toUtf8(), original);

        const QString buffer = note.editorText();
        QCOMPARE(buffer.toUtf8(), QByteArray("# Title\n\n- [ ] one\n- [x] two\n"));

        QVERIFY(note.saveBuffer(buffer));
        QCOMPARE(readBytes(path), original); // no-op, byte-identical

        QString edited = buffer;
        edited.replace(QStringLiteral("- [ ] one"), QStringLiteral("- [ ] uno"));
        QVERIFY(note.saveBuffer(edited));
        QCOMPARE(readBytes(path), QByteArray("# Title\r\r- [ ] uno\r- [x] two\r"));
    }

    /** A mixed file keeps each UNCHANGED line's own ending; only the edited line moves. */
    void testMixedLineEndingsKeepPerLineFidelity()
    {
        const QByteArray original = "alpha\r\nbeta\ngamma\r\ndelta\n";
        const QString path = makeFile(QStringLiteral("mixed.md"), original);
        MarkdownNote note;
        note.setPath(path);
        QCOMPARE(note.status(), MarkdownNote::Ready);
        QVERIFY(note.mixedLineEndings());

        QVERIFY(note.saveBuffer(note.editorText()));
        QCOMPARE(readBytes(path), original);

        QString edited = note.editorText();
        edited.replace(QStringLiteral("beta"), QStringLiteral("BETA"));
        QVERIFY(note.saveBuffer(edited));
        // alpha/gamma/delta untouched; the retyped line gets the dominant ending.
        QCOMPARE(readBytes(path), QByteArray("alpha\r\nBETA\r\ngamma\r\ndelta\n"));
    }

    /** U+2028, which QQuickTextEdit also flattens to "\n", survives an untouched line. */
    void testUnicodeLineSeparatorSurvives()
    {
        const QByteArray original = QStringLiteral("alpha beta\nomega\n").toUtf8();
        const QString path = makeFile(QStringLiteral("ls.md"), original);
        MarkdownNote note;
        note.setPath(path);
        QCOMPARE(note.status(), MarkdownNote::Ready);

        const QString buffer = note.editorText();
        QVERIFY(!buffer.contains(QChar(0x2028)));
        QVERIFY(note.saveBuffer(buffer));
        QCOMPARE(readBytes(path), original);

        QString edited = buffer;
        edited.replace(QStringLiteral("omega"), QStringLiteral("OMEGA"));
        QVERIFY(note.saveBuffer(edited));
        QCOMPARE(readBytes(path), QStringLiteral("alpha beta\nOMEGA\n").toUtf8());
    }

    /** A file without a trailing newline never grows one, and never loses one. */
    void testTrailingNewlineIsNeitherInventedNorDropped()
    {
        const QByteArray original = "- [ ] no trailing newline\r\nsecond";
        const QString path = makeFile(QStringLiteral("notrail.md"), original);
        MarkdownNote note;
        note.setPath(path);
        QCOMPARE(note.rawText().toUtf8(), original);

        QString edited = note.editorText();
        edited.replace(QStringLiteral("second"), QStringLiteral("2nd"));
        QVERIFY(note.saveBuffer(edited));
        const QByteArray after = readBytes(path);
        QCOMPARE(after, QByteArray("- [ ] no trailing newline\r\n2nd"));
        QVERIFY(!after.endsWith('\n'));

        // and the other way round: adding one is honoured with the file's own ending
        QVERIFY(note.saveBuffer(note.editorText() + QStringLiteral("\n")));
        QCOMPARE(readBytes(path), QByteArray("- [ ] no trailing newline\r\n2nd\r\n"));
    }

    // ------------------------------------------------------------------ S2 BOM

    /** A UTF-8 BOM is remembered and written back by the checkbox toggle. */
    void testBomSurvivesToggle()
    {
        const QByteArray original = QByteArray("\xEF\xBB\xBF") + "- [ ] alpha\n- [ ] beta\n";
        const QString path = makeFile(QStringLiteral("bom.md"), original);
        MarkdownNote note;
        note.setPath(path);
        QCOMPARE(note.status(), MarkdownNote::Ready);
        QVERIFY(note.hasByteOrderMark());
        // The BOM is metadata, not text: it must not leak into the buffer...
        QVERIFY(!note.rawText().startsWith(QChar(0xFEFF)));
        QCOMPARE(note.rawText(), QStringLiteral("- [ ] alpha\n- [ ] beta\n"));

        QVERIFY(note.toggleTaskAtLine(0));
        const QByteArray after = readBytes(path);
        // ...but it must still be the first three bytes of the file.
        QVERIFY2(after.startsWith("\xEF\xBB\xBF"), "the BOM was stripped by the toggle");
        QCOMPARE(after, QByteArray("\xEF\xBB\xBF") + "- [x] alpha\n- [ ] beta\n");
        QCOMPARE(after.size(), original.size());

        // A buffer save keeps it too.
        QVERIFY(note.saveBuffer(note.editorText() + QStringLiteral("- [ ] gamma\n")));
        QVERIFY(readBytes(path).startsWith("\xEF\xBB\xBF"));
    }

    // -------------------------------------------------------- S2 invalid UTF-8

    /** Undecodable bytes put the note read-only; no write path may touch it. */
    void testInvalidUtf8IsReadOnlyAndRefusesEveryWrite()
    {
        const QByteArray original = QByteArray("# caf\xE9 notes\n- [ ] item \xFE\xFF here\n");
        const QString path = makeFile(QStringLiteral("badutf8.md"), original);
        MarkdownNote note;
        note.setPath(path);

        QVERIFY2(!note.readOnlyReason().isEmpty(), "invalid UTF-8 did not trigger read-only mode");
        QCOMPARE(note.errorString(), note.readOnlyReason());

        QSignalSpy failSpy(&note, &MarkdownNote::saveFailed);
        QSignalSpy rejectSpy(&note, &MarkdownNote::toggleRejected);

        QVERIFY(!note.save(QStringLiteral("anything at all\n")));
        QCOMPARE(failSpy.count(), 1);
        QVERIFY(!note.saveBuffer(note.editorText()));
        QCOMPARE(failSpy.count(), 2);
        QVERIFY(!note.toggleTaskAtLine(1));
        QCOMPARE(rejectSpy.count(), 1);
        note.keepMine();
        QCOMPARE(failSpy.count(), 3);

        // The 0xE9 / 0xFE / 0xFF bytes are still exactly where they were.
        QCOMPARE(readBytes(path), original);

        // Pointing at a healthy file lifts the read-only mode again.
        const QString good = makeFile(QStringLiteral("good.md"), QByteArray("# fine\n"));
        note.setPath(good);
        QCOMPARE(note.readOnlyReason(), QString());
        QVERIFY(note.save(QStringLiteral("# fine, edited\n")));
    }

    // ------------------------------------------------------------- S3 / S4

    /** A save aborts when size/mtime/hash moved underneath, and keeps the buffer. */
    void testSaveAbortsWhenFileChangedUnderneath()
    {
        const QByteArray original = "- [ ] mine\n";
        const QString path = makeFile(QStringLiteral("conflict.md"), original);
        MarkdownNote note;
        note.setPath(path);
        QCOMPARE(note.status(), MarkdownNote::Ready);

        QSignalSpy conflictSpy(&note, &MarkdownNote::conflictDetected);
        QSignalSpy failSpy(&note, &MarkdownNote::saveFailed);
        QSignalSpy savedSpy(&note, &MarkdownNote::saved);

        // Obsidian (or a sync client) rewrites the file behind our back. No event
        // loop runs here, so KDirWatch has not told us yet -- exactly the race the
        // 10 s autosave used to lose.
        const QByteArray theirs = "- [x] theirs, from Obsidian\n";
        QVERIFY(writeBytes(path, theirs));

        QVERIFY2(!note.save(QStringLiteral("- [ ] mine, edited\n")), "save() overwrote an external change");
        QCOMPARE(conflictSpy.count(), 1);
        QCOMPARE(failSpy.count(), 1);
        QCOMPARE(savedSpy.count(), 0);
        QCOMPARE(readBytes(path), theirs);        // their bytes are intact
        QVERIFY(note.externalChangePending());    // the banner goes up
        QCOMPARE(note.rawText().toUtf8(), original); // our buffer is kept in memory

        // And the user picks "keep mine": the ONLY path allowed to overwrite.
        note.keepMine();
        QCOMPARE(savedSpy.count(), 1);
        QCOMPARE(readBytes(path), QByteArray("- [ ] mine, edited\n"));
        QVERIFY(!note.externalChangePending());
    }

    /**
     * keepMine() must resolve the conflict, not perform a pointless write.
     *
     * The converged-edit case: the user edits the buffer, and a sync client (or
     * Obsidian on another machine) independently writes EXACTLY the same text.
     * save() still refuses -- the fingerprint moved -- so the banner goes up with
     * the buffer stashed. When the user then picks "keep mine", the bytes we
     * would write are byte-identical to what is already there, so the write must
     * be skipped entirely: no mtime bump (which would wake every sync client in
     * the vault for nothing) and no saved() for a no-op.
     */
    void testKeepMineWithIdenticalBytesDoesNotWrite()
    {
        const QByteArray original = "- [ ] mine\n";
        const QString path = makeFile(QStringLiteral("keepmine-identical.md"), original);
        MarkdownNote note;
        note.setPath(path);
        QCOMPARE(note.status(), MarkdownNote::Ready);

        // Both sides land on the same text. Their write moves size/mtime/hash.
        const QByteArray converged = "- [x] mine, done\n";
        QVERIFY(writeBytes(path, converged));

        QSignalSpy savedSpy(&note, &MarkdownNote::saved);
        QSignalSpy conflictSpy(&note, &MarkdownNote::conflictDetected);
        QVERIFY2(!note.save(QString::fromUtf8(converged)), "save() wrote over an external change");
        QCOMPARE(conflictSpy.count(), 1);
        QCOMPARE(savedSpy.count(), 0);
        QVERIFY(note.externalChangePending());

        // Pin the mtime far enough in the past that ANY write, however fast,
        // has to move it. This is the write detector.
        const QDateTime pinned = QDateTime::currentDateTime().addSecs(-3600);
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadWrite));
            QVERIFY(f.setFileTime(pinned, QFileDevice::FileModificationTime));
        }
        const QDateTime before = QFileInfo(path).lastModified();
        QCOMPARE(before, pinned);

        note.keepMine();

        QFileInfo after(path);
        after.refresh();
        QCOMPARE(after.lastModified(), before);            // nothing was written
        QCOMPARE(savedSpy.count(), 0);                     // and nothing was announced
        QCOMPARE(readBytes(path), converged);              // bytes untouched
        QVERIFY2(!note.externalChangePending(), "keepMine() left the conflict banner up");
        QCOMPARE(note.rawText().toUtf8(), converged);      // the buffer is now the truth
        QCOMPARE(note.status(), MarkdownNote::Ready);
        QCOMPARE(note.errorString(), QString());

        // The note is not wedged: a real edit afterwards still saves normally,
        // which proves the fingerprint was adopted rather than left stale.
        QVERIFY2(note.save(QStringLiteral("- [x] mine, done\n- [ ] next\n")), "the note was wedged by the no-op keepMine()");
        QCOMPARE(savedSpy.count(), 1);
        QCOMPARE(readBytes(path), QByteArray("- [x] mine, done\n- [ ] next\n"));
        QVERIFY(QFileInfo(path).lastModified() > before);
    }

    /** A toggle is refused the same way, and never guesses a line. */
    void testToggleAbortsWhenFileChangedUnderneath()
    {
        const QByteArray original = "- [ ] a\n- [ ] b\n";
        const QString path = makeFile(QStringLiteral("conflict-toggle.md"), original);
        MarkdownNote note;
        note.setPath(path);

        QSignalSpy rejectSpy(&note, &MarkdownNote::toggleRejected);
        QSignalSpy reloadSpy(&note, &MarkdownNote::reloaded);

        const QByteArray theirs = "- [ ] a\n- [ ] b\n- [ ] c added elsewhere\n";
        QVERIFY(writeBytes(path, theirs));

        QVERIFY(!note.toggleTaskAtLine(1));
        QCOMPARE(rejectSpy.count(), 1);
        QCOMPARE(reloadSpy.count(), 1);
        QCOMPARE(readBytes(path), theirs); // nothing was written
        QCOMPARE(note.rawText().toUtf8(), theirs);

        // After the reload the same click works, against the CURRENT document.
        QVERIFY(note.toggleTaskAtLine(1));
        QCOMPARE(readBytes(path), QByteArray("- [ ] a\n- [x] b\n- [ ] c added elsewhere\n"));
    }

    /**
     * A sync client that rewrites IDENTICAL bytes only moves the mtime. That is
     * not a conflict (nothing can be lost), so the save is allowed through --
     * deliberate, documented relaxation of the size+mtime+hash fingerprint.
     */
    void testIdenticalRewriteIsNotAConflict()
    {
        const QByteArray original = "- [ ] same\n";
        const QString path = makeFile(QStringLiteral("touch.md"), original);
        MarkdownNote note;
        note.setPath(path);

        QVERIFY(writeBytes(path, original));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(3), QFileDevice::FileModificationTime));
        f.close();
        QVERIFY(QFileInfo(path).lastModified() != QDateTime());

        QSignalSpy conflictSpy(&note, &MarkdownNote::conflictDetected);
        QVERIFY(note.save(QStringLiteral("- [x] same\n")));
        QCOMPARE(conflictSpy.count(), 0);
        QCOMPARE(readBytes(path), QByteArray("- [x] same\n"));
    }

    /** S4: the 10 s autosave must not write while a conflict is unresolved. */
    void testAutosaveRefusesWhileExternalChangePending()
    {
        const QByteArray original = "- [ ] mine\n";
        const QString path = makeFile(QStringLiteral("autosave.md"), original);
        MarkdownNote note;
        note.setPath(path);
        note.setEditing(true);

        // Put the note into the pending state the way the real widget does.
        const QByteArray theirs = "- [x] theirs\n";
        QVERIFY(writeBytes(path, theirs));
        QVERIFY(!note.save(QStringLiteral("- [ ] mine, tick 1\n")));
        QVERIFY(note.externalChangePending());

        QSignalSpy conflictSpy(&note, &MarkdownNote::conflictDetected);
        QSignalSpy savedSpy(&note, &MarkdownNote::saved);

        // Three more autosave ticks while the banner is up: all refused, and none
        // of them clears the pending state (only the user may do that).
        for (int tick = 2; tick <= 4; ++tick) {
            QVERIFY2(!note.save(QStringLiteral("- [ ] mine, tick %1\n").arg(tick)), "autosave wrote while a conflict was pending");
            QVERIFY(note.externalChangePending());
        }
        QCOMPARE(conflictSpy.count(), 3);
        QCOMPARE(savedSpy.count(), 0);
        QCOMPARE(readBytes(path), theirs);

        // Even a toggle click is refused while the banner is up.
        QSignalSpy rejectSpy(&note, &MarkdownNote::toggleRejected);
        QVERIFY(!note.toggleTaskAtLine(0));
        QCOMPARE(rejectSpy.count(), 1);
        QCOMPARE(readBytes(path), theirs);

        // The pending flag alone is enough to stop the autosave: even once the
        // file on disk matches our fingerprint again (the other side undid its
        // edit), the write stays refused until the USER resolves the banner.
        QVERIFY(writeBytes(path, original));
        QVERIFY2(!note.save(QStringLiteral("- [ ] mine, tick 5\n")), "autosave wrote while a conflict was pending");
        QCOMPARE(savedSpy.count(), 0);
        QCOMPARE(readBytes(path), original);
        QVERIFY(note.externalChangePending());
        QVERIFY(!note.toggleTaskAtLine(0));
        QCOMPARE(readBytes(path), original);

        // Only an explicit user decision resolves it.
        note.dismissExternalChange();
        QVERIFY(!note.externalChangePending());
        QVERIFY(note.save(QStringLiteral("- [ ] mine, tick 6\n")));
        QCOMPARE(savedSpy.count(), 1);
        QCOMPARE(readBytes(path), QByteArray("- [ ] mine, tick 6\n"));
    }

    // ------------------------------------------------------------------ S5

    /** reloadFromDisk() drops the buffer and NEVER writes it out first. */
    void testReloadFromDiskNeverSaves()
    {
        const QByteArray original = "- [ ] on disk\n";
        const QString path = makeFile(QStringLiteral("reload.md"), original);
        MarkdownNote note;
        note.setPath(path);
        note.setEditing(true);

        const QByteArray theirs = "- [x] changed by Obsidian\n";
        QVERIFY(writeBytes(path, theirs));
        QVERIFY(!note.save(QStringLiteral("- [ ] my unsaved buffer\n"))); // stashes the buffer
        QVERIFY(note.externalChangePending());

        QSignalSpy savedSpy(&note, &MarkdownNote::saved);
        QSignalSpy reloadSpy(&note, &MarkdownNote::reloaded);
        note.reloadFromDisk();

        QCOMPARE(savedSpy.count(), 0);
        QCOMPARE(reloadSpy.count(), 1);
        QCOMPARE(readBytes(path), theirs);
        QCOMPARE(note.rawText().toUtf8(), theirs);
        QVERIFY(!note.externalChangePending());

        // The discarded buffer is really gone: keepMine() now has nothing stale
        // to resurrect, so it can only rewrite what is already there.
        note.keepMine();
        QCOMPARE(readBytes(path), theirs);
    }

    // ------------------------------------------------------------------ S6

    /**
     * The attack the old code allowed: a literal "[x](obsnote:toggle/N)" typed
     * into the note survives render() verbatim, so a click on it must NOT be
     * trusted to flip line N -- especially not a line inside a code fence.
     */
    void testToggleOfFencedLineIsRefused()
    {
        const QByteArray original =
            "---\n"
            "title: t\n"
            "- [ ] frontmatter decoy\n"
            "---\n"
            "\n"
            "- [ ] real task\n"
            "\n"
            "```text\n"
            "- [ ] fenced task\n"
            "```\n"
            "\n"
            "A literal [x](obsnote:toggle/8) link, hand-typed.\n"
            "\n"
            "    - [ ] indented code task\n";
        const QString path = makeFile(QStringLiteral("fence.md"), original);
        MarkdownNote note;
        note.setPath(path);
        QCOMPARE(note.status(), MarkdownNote::Ready);

        const QStringList lines = TaskMarkdown::splitLines(note.rawText());
        QCOMPARE(lines.value(2), QStringLiteral("- [ ] frontmatter decoy"));
        QCOMPARE(lines.value(5), QStringLiteral("- [ ] real task"));
        QCOMPARE(lines.value(8), QStringLiteral("- [ ] fenced task"));
        QCOMPARE(lines.value(13), QStringLiteral("    - [ ] indented code task"));

        // The forged link really does reach QML: this is why the index alone can
        // never be trusted.
        QVERIFY2(note.renderedText().contains(QStringLiteral("obsnote:toggle/8")),
                 "the literal link no longer survives render(); the guard is still required");
        QCOMPARE(note.toggleLineForLink(QStringLiteral("obsnote:toggle/8")), 8);

        QSignalSpy rejectSpy(&note, &MarkdownNote::toggleRejected);

        QVERIFY2(!note.toggleTaskAtLine(8), "a fenced code line was toggled");
        QVERIFY2(!note.toggleTask(8, QStringLiteral("- [ ] fenced task")), "a fenced code line was toggled");
        QVERIFY2(!note.toggleTaskAtLine(2), "a frontmatter line was toggled");
        QVERIFY2(!note.toggleTaskAtLine(13), "an indented-code line was toggled");
        QVERIFY2(!note.toggleTaskAtLine(7), "the fence marker itself was toggled");
        QCOMPARE(rejectSpy.count(), 5);
        QCOMPARE(readBytes(path), original); // not one byte moved

        QVERIFY(!note.isTaskLineAt(8));
        QVERIFY(!note.isTaskLineAt(2));
        QVERIFY(!note.isTaskLineAt(13));
        QVERIFY(note.isTaskLineAt(5));

        // The one genuinely clickable task still works.
        QVERIFY(note.toggleTask(5, note.lineTextAt(5)));
        QByteArray expected = original;
        expected.replace("- [ ] real task", "- [x] real task");
        QCOMPARE(readBytes(path), expected);
    }

    /** A stale expectedLineText aborts the toggle instead of flipping the wrong line. */
    void testToggleWithStaleExpectedTextAborts()
    {
        const QByteArray original = "- [ ] alpha\n- [ ] beta\n";
        const QString path = makeFile(QStringLiteral("stale.md"), original);
        MarkdownNote note;
        note.setPath(path);

        QSignalSpy rejectSpy(&note, &MarkdownNote::toggleRejected);

        QVERIFY2(!note.toggleTask(1, QStringLiteral("- [ ] alpha")), "a stale expected line was accepted");
        QCOMPARE(rejectSpy.count(), 1);
        QCOMPARE(readBytes(path), original);

        QVERIFY2(!note.toggleTask(0, QStringLiteral("- [ ] alpha with a typo")), "a stale expected line was accepted");
        QCOMPARE(rejectSpy.count(), 2);
        QCOMPARE(readBytes(path), original);

        // Out of range and non-task lines are still refused.
        QVERIFY(!note.toggleTask(99, QString()));
        QVERIFY(!note.toggleTask(-1, QString()));
        QCOMPARE(readBytes(path), original);

        // The matching expectation goes through.
        QVERIFY(note.toggleTask(1, QStringLiteral("- [ ] beta")));
        QCOMPARE(readBytes(path), QByteArray("- [ ] alpha\n- [x] beta\n"));
    }

    /** On a CRLF note the expected text may be given with or without its CR. */
    void testToggleExpectedTextToleratesCarriageReturn()
    {
        const QByteArray original = "- [ ] alpha\r\n- [ ] beta\r\n";
        const QString path = makeFile(QStringLiteral("stale-crlf.md"), original);
        MarkdownNote note;
        note.setPath(path);

        QVERIFY(note.toggleTask(0, QStringLiteral("- [ ] alpha")));
        QCOMPARE(readBytes(path), QByteArray("- [x] alpha\r\n- [ ] beta\r\n"));
        QVERIFY(note.toggleTask(1, QStringLiteral("- [ ] beta\r")));
        QCOMPARE(readBytes(path), QByteArray("- [x] alpha\r\n- [x] beta\r\n"));
    }

    // ------------------------------------------------------------------ setPath

    /** Switching to a missing/unreadable file must not keep serving the old note. */
    void testSetPathToMissingFileClearsRawText()
    {
        const QString path = makeFile(QStringLiteral("first.md"), QByteArray("# first note\n- [ ] secret\n"));
        MarkdownNote note;
        note.setPath(path);
        QCOMPARE(note.status(), MarkdownNote::Ready);
        QVERIFY(!note.rawText().isEmpty());

        const QString missing = m_dir.filePath(QStringLiteral("nope/second.md"));
        note.setPath(missing);
        QCOMPARE(note.status(), MarkdownNote::Missing);
        QCOMPARE(note.rawText(), QString());
        QCOMPARE(note.renderedText(), QString());
        QCOMPARE(note.editorText(), QString());

        // ...and the first note's text is not what lands in the new file.
        QVERIFY(note.save(QStringLiteral("# second note\n")));
        QCOMPARE(readBytes(missing), QByteArray("# second note\n"));
        QCOMPARE(readBytes(path), QByteArray("# first note\n- [ ] secret\n"));

        // An unreadable file behaves the same way.
        const QString unreadable = makeFile(QStringLiteral("locked.md"), QByteArray("# locked\n"));
        QVERIFY(QFile::setPermissions(unreadable, QFileDevice::WriteOwner));
        MarkdownNote other;
        other.setPath(path);
        QVERIFY(!other.rawText().isEmpty());
        other.setPath(unreadable);
        QCOMPARE(other.status(), MarkdownNote::LoadError);
        QCOMPARE(other.rawText(), QString());
        QFile::setPermissions(unreadable, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }

    // ------------------------------------------------------------------ S7

    /**
     * When the temp file cannot be created the write FAILS: it must never fall
     * back to truncating the user's note in place. Made real by taking write
     * permission off the directory while leaving it on the file.
     */
    void testNoInPlaceTruncateFallback()
    {
        const QString sub = m_dir.filePath(QStringLiteral("ro-dir"));
        QVERIFY(QDir().mkpath(sub));
        const QByteArray original = "# precious\n- [ ] do not lose me\n";
        const QString path = sub + QStringLiteral("/note.md");
        QVERIFY(writeBytes(path, original));

        MarkdownNote note;
        note.setPath(path);
        QCOMPARE(note.status(), MarkdownNote::Ready);

        // r-x: the file stays writable, but no sibling temp file can be created.
        QVERIFY(QFile::setPermissions(sub, QFileDevice::ReadOwner | QFileDevice::ExeOwner));

        QSignalSpy failSpy(&note, &MarkdownNote::saveFailed);
        const bool ok = note.save(QStringLiteral("# replacement\n"));

        QFile::setPermissions(sub, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

        QVERIFY2(!ok, "save() reported success without an atomic write");
        QCOMPARE(failSpy.count(), 1);
        QCOMPARE(note.status(), MarkdownNote::SaveError);
        QVERIFY2(readBytes(path) == original, "the note was truncated in place by the direct-write fallback");
        QVERIFY(!note.errorString().isEmpty());

        // No stray temp file survived either.
        QCOMPARE(QDir(sub).entryList(QStringList{QStringLiteral("note.md.*")}, QDir::Files | QDir::Hidden), QStringList());

        // And the source really does leave QSaveFile's fallback off.
        const QByteArray source = readBytes(QStringLiteral(MARKDOWNNOTE_CPP));
        QVERIFY2(!source.isEmpty(), MARKDOWNNOTE_CPP);
        QVERIFY2(source.contains("QSaveFile"), "the atomic write disappeared");
        QVERIFY2(!source.contains("setDirectWriteFallback(true)"), "setDirectWriteFallback(true) is back");
    }
};

QTEST_GUILESS_MAIN(TstMarkdownNote)
#include "tst_markdownnote.moc"
