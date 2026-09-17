/*
    SPDX-FileCopyrightText: 2014 David Edmundson <davidedmundson@kde.org>
    SPDX-FileCopyrightText: 2014, 2015 Kai Uwe Broulik <kde@privat.broulik.de>
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors

    Derived from the Plasma "notes" applet (kdeplasma-addons, applets/notes).

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include <qqmlregistration.h>

class KDirWatch;

/**
 * One .md file on disk, bound to one applet instance.
 *
 * The raw BYTES on disk are the ONLY source of truth (policy S1): this class
 * never calls QTextDocument::toMarkdown() and never writes anything but
 * (a) the string QML hands to save()/saveBuffer() with the file's original line
 * endings and BOM restored, or (b) a single toggled checkbox character.
 *
 * File-safety policy implemented here:
 *  S2 byte fidelity   - remembered EOL (LF/CRLF/CR, mixed-aware) + BOM are put
 *                       back on every write; a file with undecodable bytes goes
 *                       read-only (readOnlyReason) instead of being rewritten
 *                       with U+FFFD.
 *  S3 no blind write  - every successful load/save records size+mtime+SHA-256.
 *                       Any write re-checks it and ABORTS on mismatch, setting
 *                       externalChangePending and emitting conflictDetected().
 *  S4 autosave        - save() refuses outright while externalChangePending.
 *  S5 reload          - reloadFromDisk() never saves first.
 *  S6 toggle          - toggleTask() re-reads the file, validates the expected
 *                       line text and runs a FENCE- and FRONTMATTER-aware scan,
 *                       so a literal "[x](obsnote:toggle/N)" in the note body
 *                       cannot make it flip an arbitrary line.
 *  S7 atomic          - QSaveFile with the direct-write fallback left OFF; a
 *                       failed write never truncates the user's note.
 */
class MarkdownNote : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /** Absolute local path of the bound file. Setting it loads + rewatches. Empty = unconfigured. */
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    /** Basename for display, e.g. "Groceries.md". Empty when path is empty. */
    Q_PROPERTY(QString fileName READ fileName NOTIFY pathChanged)
    /** Exact file contents as last read or last written. Source of truth. */
    Q_PROPERTY(QString rawText READ rawText NOTIFY rawTextChanged)
    /** rawText transformed by TaskMarkdown::render(); feed to Text.textFormat=MarkdownText. */
    Q_PROPERTY(QString renderedText READ renderedText NOTIFY renderedTextChanged)
    /** MarkdownBlocks::parse(rawText); recomputed at the same point as renderedText. */
    Q_PROPERTY(QVariantList blocks READ blocks NOTIFY renderedTextChanged)
    /** See Status. */
    Q_PROPERTY(MarkdownNote::Status status READ status NOTIFY statusChanged)
    /** Human-readable last error, "" when none. */
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    /** QML sets this true while the user is in EDIT mode. Suppresses auto-reload. */
    Q_PROPERTY(bool editing READ editing WRITE setEditing NOTIFY editingChanged)
    /** True when the file changed on disk while editing==true and we deliberately did not reload. */
    Q_PROPERTY(bool externalChangePending READ externalChangePending NOTIFY externalChangePendingChanged)

    /**
     * "" when the file may be written. Non-empty = a translated reason why this
     * note is READ-ONLY (currently: the file contains bytes that are not valid
     * UTF-8, so rewriting it would replace them with U+FFFD). QML must disable
     * editing and the checkbox links while this is non-empty. (S2)
     */
    Q_PROPERTY(QString readOnlyReason READ readOnlyReason NOTIFY readOnlyReasonChanged)
    /** rawText with every line ending normalised to "\n": what a QQuickTextEdit holds. (S2) */
    Q_PROPERTY(QString editorText READ editorText NOTIFY rawTextChanged)
    /** "lf" | "crlf" | "cr": the dominant line ending of the loaded file. (S2) */
    Q_PROPERTY(QString lineEnding READ lineEnding NOTIFY lineEndingChanged)
    /** True when the loaded file mixes line-ending styles. (S2) */
    Q_PROPERTY(bool mixedLineEndings READ mixedLineEndings NOTIFY lineEndingChanged)
    /** True when the loaded file started with a UTF-8 BOM (which is written back). (S2) */
    Q_PROPERTY(bool hasByteOrderMark READ hasByteOrderMark NOTIFY lineEndingChanged)

public:
    enum Status {
        NoPath = 0,   ///< path is empty; show the placeholder
        Ready = 1,    ///< file loaded (rawText valid)
        Missing = 2,  ///< path set but file does not exist
        LoadError = 3,///< exists but unreadable
        SaveError = 4 ///< last write failed; rawText is still the last good text
    };
    Q_ENUM(Status)

    explicit MarkdownNote(QObject *parent = nullptr);
    ~MarkdownNote() override;

    QString path() const;
    void setPath(const QString &path);

    QString fileName() const;
    QString rawText() const;
    QString renderedText() const;
    QVariantList blocks() const;
    Status status() const;
    QString errorString() const;

    bool editing() const;
    void setEditing(bool editing);

    bool externalChangePending() const;

    QString readOnlyReason() const;
    QString editorText() const;
    QString lineEnding() const;
    bool mixedLineEndings() const;
    bool hasByteOrderMark() const;

    /** Re-read the file from disk unconditionally, clearing externalChangePending. Never saves. */
    Q_INVOKABLE void reload();

    /**
     * S5: discard the in-memory buffer and take what is on disk. NEVER saves first,
     * not even implicitly. Clears externalChangePending and any stashed write.
     */
    Q_INVOKABLE void reloadFromDisk();

    /**
     * Resolve a conflict the user's way: force-write the buffer that was refused
     * (or, if none was stashed, the current rawText), bypassing the fingerprint
     * check, and clear externalChangePending. The ONLY method that may overwrite
     * a file that changed underneath us.
     *
     * When the bytes it would write are already byte-identical to what is on disk
     * it clears the conflict WITHOUT writing: no mtime bump (so no pointless sync
     * event for an Obsidian vault) and no misleading saved().
     */
    Q_INVOKABLE void keepMine();

    /**
     * Write @p text (UTF-8, BOM and line endings restored to the loaded file's, no
     * trailing-newline fixups).
     * No-ops and returns true when the resulting text == rawText (never bumps mtime).
     * Returns false without writing when the note is read-only (S2), when
     * externalChangePending is set (S4) or when the on-disk fingerprint moved (S3);
     * in the latter two cases conflictDetected() is emitted and the text is stashed
     * for keepMine().
     */
    Q_INVOKABLE bool save(const QString &text);

    /**
     * S2 entry point for the QML editor: @p editorBuffer is a QQuickTextEdit buffer
     * in which every CRLF/CR/U+2028/U+2029 has already been normalised to "\n".
     * The original per-line endings are restored HERE, in C++, before the write.
     * Otherwise identical to save().
     */
    Q_INVOKABLE bool saveBuffer(const QString &editorBuffer);
    /** Alias of saveBuffer() for callers that spell it this way. */
    Q_INVOKABLE bool saveFromEditor(const QString &editorBuffer);

    /** Alias of the editorText property, for callers that spell it this way. */
    Q_INVOKABLE QString editorBuffer() const;

    /**
     * Take an LF-only editor buffer and give back the text with the loaded file's
     * line endings restored (unchanged lines keep their exact original ending,
     * even in a mixed file; new/edited lines get the dominant one). Pure.
     */
    Q_INVOKABLE QString restoreLineEndings(const QString &editorBuffer) const;

    /**
     * S6: surgically flip "[ ]" <-> "[x]" on 0-based source line @p lineIndex.
     * Self-validating: re-reads the file, requires it to still match rawText,
     * requires @p expectedLineText (when non-empty) to equal that line, and
     * requires the line to be a task line under a FENCE- and FRONTMATTER-aware
     * scan. Any mismatch aborts with toggleRejected() and writes nothing.
     * Returns true only if the file was rewritten.
     */
    Q_INVOKABLE bool toggleTask(int lineIndex, const QString &expectedLineText);

    /**
     * Compatible with the previous signature; now equivalent to
     * toggleTask(lineIndex, QString()), i.e. it still validates the line with the
     * fence/frontmatter-aware scan instead of trusting the index.
     */
    Q_INVOKABLE bool toggleTaskAtLine(int lineIndex);

    /** The exact source line @p lineIndex of rawText, or "" when out of range. */
    Q_INVOKABLE QString lineTextAt(int lineIndex) const;

    /** True when line @p lineIndex of rawText is a REAL, clickable task line (fence/frontmatter aware). */
    Q_INVOKABLE bool isTaskLineAt(int lineIndex) const;

    /** "obsnote:toggle/12" -> 12; anything else -> -1. */
    Q_INVOKABLE int toggleLineForLink(const QString &link) const;

    /** "obsnote:wiki/Some%20Note" -> "Some Note"; anything else -> "". */
    Q_INVOKABLE QString wikilinkTargetForLink(const QString &link) const;

    /**
     * obsidian://open?path=<pct-encoded absolute path> when @p target is empty,
     * else obsidian://open?file=<pct-encoded target>.
     */
    Q_INVOKABLE QUrl obsidianUrl(const QString &target = QString()) const;

    /** Drop the in-editor buffer and take the on-disk version. Clears externalChangePending. */
    Q_INVOKABLE void acceptExternalChange();

    /** Keep editing; just stop nagging. Clears externalChangePending without reloading. */
    Q_INVOKABLE void dismissExternalChange();

    /** mkpath(parent) + create an empty file at path. Returns true on success (or if it exists). */
    Q_INVOKABLE bool createFile();

    /** file:///home/user/v/n.md -> /home/user/v/n.md ; a plain path is returned unchanged. */
    Q_INVOKABLE QString localPathFromUrl(const QUrl &url) const;

Q_SIGNALS:
    void pathChanged();
    void rawTextChanged();
    void renderedTextChanged();
    void statusChanged();
    void errorStringChanged();
    void editingChanged();
    void externalChangePendingChanged();
    void readOnlyReasonChanged();
    void lineEndingChanged();

    /** Emitted after every successful write (save() or toggleTask()). */
    void saved();
    /** Emitted after rawText was replaced from disk (reload/auto-reload/accept). */
    void reloaded();
    /** A toggle was not applied; @p reason is translated and user-presentable. */
    void toggleRejected(const QString &reason);
    /** Write failed; @p reason is translated. */
    void saveFailed(const QString &reason);
    /** S3: a write was REFUSED because the file changed underneath us. The buffer is kept. */
    void conflictDetected();

private:
    enum class Eol {
        Lf,
        CrLf,
        Cr,
    };

    void loadFromDisk();                        // sets m_raw, m_rendered, status, fingerprint
    bool writeToDisk(const QString &text);      // QSaveFile + watcher suspend + bookkeeping
    bool performWrite(const QString &text, bool force, bool viaToggle);
    bool diskUnchanged(QString *reason);        // S3 fingerprint re-check
    bool diskMatchesBytes(const QByteArray &bytes) const; // file already holds exactly these bytes?
    void setRawText(const QString &text);       // short-circuits on equality, re-renders
    void adoptBytes(const QByteArray &bytes, const QString &text);
    void rememberFingerprint(const QByteArray &bytes);
    void analyseBytes(const QByteArray &bytes, const QString &text);
    QByteArray encode(const QString &text) const;
    void setReadOnlyReason(const QString &reason);
    void setErrorString(const QString &error);
    void setStatus(Status status);
    void setExternalChangePending(bool pending);
    void stash(const QString &text);
    void onFileDirty(const QString &path);
    void rewatch();

    QString m_path;
    QString m_raw;
    QString m_rendered;
    QVariantList m_blocks;
    QString m_errorString;
    QString m_readOnlyReason;
    Status m_status = NoPath;
    bool m_editing = false;
    bool m_externalChangePending = false;
    QDateTime m_lastSeenMTime;
    qint64 m_lastSeenSize = -1;
    QByteArray m_lastSeenHash;
    Eol m_eol = Eol::Lf;
    bool m_mixedEol = false;
    bool m_hasBom = false;
    QString m_stashedWrite;   // the buffer a refused write wanted to store
    bool m_hasStashedWrite = false;
    KDirWatch *m_watcher; // owned, created in ctor with `this` as parent
};
