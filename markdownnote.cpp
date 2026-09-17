/*
    SPDX-FileCopyrightText: 2014 David Edmundson <davidedmundson@kde.org>
    SPDX-FileCopyrightText: 2014, 2015 Kai Uwe Broulik <kde@privat.broulik.de>
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors

    Derived from the Plasma "notes" applet (kdeplasma-addons, applets/notes).

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "markdownnote.h"

#include "taskmarkdown.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringConverter>
#include <QStringList>
#include <QStringView>

#include <KDirWatch>
#include <KLocalizedString>

namespace
{

constexpr char16_t kBomChar = 0xFEFF;
constexpr char16_t kLineSeparator = 0x2028;
constexpr char16_t kParagraphSeparator = 0x2029;

const char kBomBytes[] = "\xEF\xBB\xBF";

/** Assign and report whether anything actually changed, so signals stay edge-triggered. */
template<typename T>
bool assign(T &slot, const T &value)
{
    if (slot == value) {
        return false;
    }
    slot = value;
    return true;
}

/** The line without its trailing carriage return, so CRLF behaves like LF. */
QStringView lineCore(const QString &line)
{
    QStringView v(line);
    if (v.endsWith(QLatin1Char('\r'))) {
        v.chop(1);
    }
    return v;
}

/** One physical line: its content plus the exact terminator that followed it. */
struct PhysicalLine {
    QString content;
    QString terminator; // "", "\n", "\r\n", "\r", U+2028 or U+2029
};

/**
 * Split on EVERY character QQuickTextEdit collapses into "\n" (CRLF, CR, LF,
 * U+2028, U+2029), keeping the terminator so it can be put back verbatim.
 * "a\nb\n" -> [{a,"\n"},{b,"\n"}];  "a\nb" -> [{a,"\n"},{b,""}];  "" -> [].
 */
QList<PhysicalLine> splitPhysical(const QString &text)
{
    QList<PhysicalLine> out;
    const qsizetype n = text.size();
    qsizetype start = 0;
    for (qsizetype i = 0; i < n; ++i) {
        const char16_t c = text.at(i).unicode();
        QString term;
        if (c == u'\r') {
            term = (i + 1 < n && text.at(i + 1) == QLatin1Char('\n')) ? QStringLiteral("\r\n") : QStringLiteral("\r");
        } else if (c == u'\n') {
            term = QStringLiteral("\n");
        } else if (c == kLineSeparator || c == kParagraphSeparator) {
            term = QString(QChar(c));
        } else {
            continue;
        }
        out.append(PhysicalLine{text.mid(start, i - start), term});
        i += term.size() - 1;
        start = i + 1;
    }
    if (start < n) {
        out.append(PhysicalLine{text.mid(start), QString()});
    }
    return out;
}

QString joinPhysical(const QList<PhysicalLine> &lines)
{
    QString out;
    qsizetype size = 0;
    for (const PhysicalLine &l : lines) {
        size += l.content.size() + l.terminator.size();
    }
    out.reserve(size);
    for (const PhysicalLine &l : lines) {
        out += l.content;
        out += l.terminator;
    }
    return out;
}

QByteArray sha256(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

} // namespace

MarkdownNote::MarkdownNote(QObject *parent)
    : QObject(parent)
    // Per-instance watcher: removeFile() on KDirWatch::self() would silently
    // unwatch every other consumer of the same path.
    , m_watcher(new KDirWatch(this))
{
    connect(m_watcher, &KDirWatch::dirty, this, &MarkdownNote::onFileDirty);
    connect(m_watcher, &KDirWatch::created, this, &MarkdownNote::onFileDirty);
    connect(m_watcher, &KDirWatch::deleted, this, [this](const QString &path) {
        if (m_path.isEmpty() || path != m_path) {
            return;
        }
        // The buffer is deliberately kept: a later save() recreates the file.
        m_lastSeenMTime = QDateTime();
        m_lastSeenSize = -1;
        m_lastSeenHash.clear();
        setErrorString(i18n("The file was deleted on disk."));
        setStatus(Missing);
        if (m_editing) {
            setExternalChangePending(true);
        }
    });
}

MarkdownNote::~MarkdownNote() = default;

QString MarkdownNote::path() const
{
    return m_path;
}

void MarkdownNote::setPath(const QString &path)
{
    if (path == m_path) {
        return;
    }

    if (!m_path.isEmpty()) {
        m_watcher->removeFile(m_path);
    }

    m_path = path;
    m_lastSeenMTime = QDateTime();
    m_lastSeenSize = -1;
    m_lastSeenHash.clear();
    m_stashedWrite.clear();
    m_hasStashedWrite = false;
    setExternalChangePending(false);
    setReadOnlyReason(QString());
    Q_EMIT pathChanged();

    // The PREVIOUS note's text must never survive into a new path: if the new
    // file is Missing or unreadable, rawText/renderedText would otherwise keep
    // serving it and the next save() would write it into the new file.
    setRawText(QString());
    m_hasBom = false;
    if (assign(m_eol, Eol::Lf) | assign(m_mixedEol, false)) {
        Q_EMIT lineEndingChanged();
    }

    if (m_path.isEmpty()) {
        setErrorString(QString());
        setStatus(NoPath);
        return;
    }

    loadFromDisk();
    rewatch();
}

QString MarkdownNote::fileName() const
{
    if (m_path.isEmpty()) {
        return QString();
    }
    return QFileInfo(m_path).fileName();
}

QString MarkdownNote::rawText() const
{
    return m_raw;
}

QString MarkdownNote::renderedText() const
{
    return m_rendered;
}

MarkdownNote::Status MarkdownNote::status() const
{
    return m_status;
}

QString MarkdownNote::errorString() const
{
    return m_errorString;
}

bool MarkdownNote::editing() const
{
    return m_editing;
}

void MarkdownNote::setEditing(bool editing)
{
    if (!assign(m_editing, editing)) {
        return;
    }
    Q_EMIT editingChanged();
}

bool MarkdownNote::externalChangePending() const
{
    return m_externalChangePending;
}

QString MarkdownNote::readOnlyReason() const
{
    return m_readOnlyReason;
}

QString MarkdownNote::editorText() const
{
    // Exactly what a QQuickTextEdit ends up holding once it has normalised the text.
    QList<PhysicalLine> lines = splitPhysical(m_raw);
    for (PhysicalLine &l : lines) {
        if (!l.terminator.isEmpty()) {
            l.terminator = QStringLiteral("\n");
        }
    }
    return joinPhysical(lines);
}

QString MarkdownNote::editorBuffer() const
{
    return editorText();
}

QString MarkdownNote::lineEnding() const
{
    switch (m_eol) {
    case Eol::CrLf:
        return QStringLiteral("crlf");
    case Eol::Cr:
        return QStringLiteral("cr");
    case Eol::Lf:
        break;
    }
    return QStringLiteral("lf");
}

bool MarkdownNote::mixedLineEndings() const
{
    return m_mixedEol;
}

bool MarkdownNote::hasByteOrderMark() const
{
    return m_hasBom;
}

void MarkdownNote::reload()
{
    reloadFromDisk();
}

void MarkdownNote::reloadFromDisk()
{
    // S5: never, under any circumstance, write before reading.
    m_stashedWrite.clear();
    m_hasStashedWrite = false;
    if (!m_path.isEmpty()) {
        loadFromDisk();
        rewatch();
    }
    setExternalChangePending(false);
}

void MarkdownNote::keepMine()
{
    // Goes through performWrite() -- NOT straight to writeToDisk() -- so that the
    // "identical bytes are never written back" rule applies here too. The empty
    // path and read-only guards, and the force flag that skips the S3 fingerprint
    // check, all live in performWrite().
    const QString text = m_hasStashedWrite ? m_stashedWrite : m_raw;
    if (performWrite(text, true, false)) {
        m_stashedWrite.clear();
        m_hasStashedWrite = false;
        setExternalChangePending(false);
    }
}

bool MarkdownNote::save(const QString &text)
{
    // A buffer that lost every CR to QQuickTextEdit's normalisation is repaired
    // here too, so an old QML caller that still calls save() cannot flatten the
    // line endings of a CRLF/CR note (S2).
    const bool looksNormalised = !text.contains(QLatin1Char('\r'))
        && (m_raw.contains(QLatin1Char('\r')) || m_raw.contains(QChar(kLineSeparator)) || m_raw.contains(QChar(kParagraphSeparator)));
    return performWrite(looksNormalised ? restoreLineEndings(text) : text, false, false);
}

bool MarkdownNote::saveBuffer(const QString &editorBuffer)
{
    return performWrite(restoreLineEndings(editorBuffer), false, false);
}

bool MarkdownNote::saveFromEditor(const QString &editorBuffer)
{
    return saveBuffer(editorBuffer);
}

QString MarkdownNote::restoreLineEndings(const QString &editorBuffer) const
{
    const QString dominant = m_eol == Eol::CrLf ? QStringLiteral("\r\n") : (m_eol == Eol::Cr ? QStringLiteral("\r") : QStringLiteral("\n"));

    const QList<PhysicalLine> original = splitPhysical(m_raw);
    QList<PhysicalLine> buffer = splitPhysical(editorBuffer);
    if (buffer.isEmpty()) {
        return editorBuffer;
    }

    // Unchanged lines keep their EXACT original terminator, which is what keeps a
    // mixed-ending file (and a stray U+2028) intact; only lines the user actually
    // touched -- and lines that are new -- get the dominant ending.
    qsizetype prefix = 0;
    while (prefix < original.size() && prefix < buffer.size() && original.at(prefix).content == buffer.at(prefix).content) {
        ++prefix;
    }
    qsizetype suffix = 0;
    while (suffix < original.size() - prefix && suffix < buffer.size() - prefix
           && original.at(original.size() - 1 - suffix).content == buffer.at(buffer.size() - 1 - suffix).content) {
        ++suffix;
    }

    const bool sameLineCount = original.size() == buffer.size();
    for (qsizetype i = 0; i < buffer.size(); ++i) {
        if (buffer.at(i).terminator.isEmpty()) {
            continue; // the last line has no ending: never invent one
        }
        const PhysicalLine *match = nullptr;
        if (i < prefix) {
            match = &original.at(i);
        } else if (i >= buffer.size() - suffix) {
            match = &original.at(original.size() - (buffer.size() - i));
        } else if (sameLineCount && original.at(i).content == buffer.at(i).content) {
            match = &original.at(i);
        }
        buffer[i].terminator = (match && !match->terminator.isEmpty()) ? match->terminator : dominant;
    }

    return joinPhysical(buffer);
}

bool MarkdownNote::toggleTaskAtLine(int lineIndex)
{
    return toggleTask(lineIndex, QString());
}

bool MarkdownNote::toggleTask(int lineIndex, const QString &expectedLineText)
{
    if (m_status == NoPath || m_path.isEmpty()) {
        Q_EMIT toggleRejected(i18n("No file is open."));
        return false;
    }
    if (!m_readOnlyReason.isEmpty()) {
        Q_EMIT toggleRejected(m_readOnlyReason);
        return false;
    }
    if (m_externalChangePending) {
        // S3/S4: an unresolved conflict is never resolved by a checkbox click.
        Q_EMIT conflictDetected();
        Q_EMIT toggleRejected(i18n("The file changed on disk. Resolve that first."));
        return false;
    }
    if (m_status != Ready) {
        Q_EMIT toggleRejected(m_errorString.isEmpty() ? i18n("The note is not loaded.") : m_errorString);
        return false;
    }

    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        const QString reason = file.errorString();
        setErrorString(reason);
        setStatus(LoadError);
        Q_EMIT toggleRejected(reason);
        return false;
    }
    const QByteArray bytes = file.readAll();
    const bool readFailed = file.error() != QFileDevice::NoError;
    file.close();

    if (readFailed) {
        const QString reason = file.errorString();
        setErrorString(reason);
        setStatus(LoadError);
        Q_EMIT toggleRejected(reason);
        return false;
    }

    QStringDecoder decoder(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
    const QString disk = decoder.decode(bytes);
    if (decoder.hasError()) {
        // S2: never rewrite bytes we cannot decode.
        const QString reason = i18n("This file contains bytes that are not valid UTF-8; it is open read-only so they are not destroyed.");
        setReadOnlyReason(reason);
        setErrorString(reason);
        Q_EMIT toggleRejected(reason);
        return false;
    }

    if (disk != m_raw) {
        // Somebody edited the file since we rendered it, so line numbers we were
        // handed refer to a document that no longer exists. Never guess.
        adoptBytes(bytes, disk);
        Q_EMIT reloaded();
        Q_EMIT conflictDetected();
        Q_EMIT toggleRejected(i18n("The file changed on disk; the list was reloaded. Try again."));
        return false;
    }
    // Content is byte-for-byte what we know; re-arm the fingerprint so a pure
    // mtime touch by a sync client does not look like a conflict a moment later.
    rememberFingerprint(bytes);

    QStringList lines = TaskMarkdown::splitLines(disk);
    if (lineIndex < 0 || lineIndex >= lines.size()) {
        Q_EMIT toggleRejected(i18n("That line is no longer part of the note."));
        return false;
    }

    if (!expectedLineText.isEmpty()) {
        // S6: the caller states what it believed the line said. CR-terminated
        // lines are accepted with or without their carriage return.
        const QString actual = lines.at(lineIndex);
        const QString expected = expectedLineText;
        if (actual != expected && lineCore(actual) != lineCore(expected)) {
            Q_EMIT toggleRejected(i18n("That line changed; the click was ignored."));
            return false;
        }
    }

    // S6: a literal "[x](obsnote:toggle/N)" in the body must not be able to flip
    // a line the renderer never made clickable (inside a fence, for instance).
    //
    // This MUST go through TaskMarkdown::isTaskLineInDocument(), which runs the
    // very same block scan render() uses. A second, "simpler" scanner here is a
    // hole: every line the two disagree on is either a checkbox the user can see
    // but cannot click, or -- far worse -- a line inside a code fence or inside
    // YAML frontmatter that a hand-written toggle link can still flip.
    if (!TaskMarkdown::isTaskLineInDocument(disk, lineIndex)) {
        Q_EMIT toggleRejected(i18n("That line is no longer a checklist item."));
        return false;
    }

    QString line = lines.at(lineIndex);
    if (!TaskMarkdown::toggleTaskLine(line)) {
        Q_EMIT toggleRejected(i18n("That line is no longer a checklist item."));
        return false;
    }
    lines[lineIndex] = line;

    // Every byte outside this single line is exactly what was on disk.
    return performWrite(TaskMarkdown::joinLines(lines), false, true);
}

QString MarkdownNote::lineTextAt(int lineIndex) const
{
    const QStringList lines = TaskMarkdown::splitLines(m_raw);
    if (lineIndex < 0 || lineIndex >= lines.size()) {
        return QString();
    }
    return lines.at(lineIndex);
}

bool MarkdownNote::isTaskLineAt(int lineIndex) const
{
    return TaskMarkdown::isTaskLineInDocument(m_raw, lineIndex);
}

int MarkdownNote::toggleLineForLink(const QString &link) const
{
    return TaskMarkdown::parseToggleLink(link);
}

QString MarkdownNote::wikilinkTargetForLink(const QString &link) const
{
    return TaskMarkdown::parseWikiLink(link);
}

QUrl MarkdownNote::obsidianUrl(const QString &target) const
{
    QString url = QStringLiteral("obsidian://open?");
    if (target.isEmpty()) {
        url += QLatin1String("path=");
        url += QString::fromLatin1(QUrl::toPercentEncoding(m_path));
    } else {
        url += QLatin1String("file=");
        url += QString::fromLatin1(QUrl::toPercentEncoding(target));
    }
    return QUrl(url, QUrl::StrictMode);
}

void MarkdownNote::acceptExternalChange()
{
    reloadFromDisk();
}

void MarkdownNote::dismissExternalChange()
{
    setExternalChangePending(false);
}

bool MarkdownNote::createFile()
{
    if (m_path.isEmpty()) {
        return false;
    }

    const QFileInfo info(m_path);
    if (info.exists()) {
        rewatch();
        return true;
    }

    const QString parent = info.absolutePath();
    if (!parent.isEmpty() && !QDir().mkpath(parent)) {
        const QString reason = i18n("Could not create the folder %1.", parent);
        setErrorString(reason);
        setStatus(SaveError);
        Q_EMIT saveFailed(reason);
        return false;
    }

    m_watcher->removeFile(m_path);

    QFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        const QString reason = file.errorString();
        setErrorString(reason);
        setStatus(SaveError);
        Q_EMIT saveFailed(reason);
        rewatch();
        return false;
    }
    file.close();

    loadFromDisk();
    rewatch();
    return m_status == Ready;
}

QString MarkdownNote::localPathFromUrl(const QUrl &url) const
{
    if (url.isLocalFile()) {
        return url.toLocalFile();
    }
    // A bare path arrives as a scheme-less QUrl; hand it back untouched.
    return url.toString(QUrl::PreferLocalFile);
}

void MarkdownNote::loadFromDisk()
{
    if (m_path.isEmpty()) {
        return;
    }

    const QFileInfo info(m_path);
    if (!info.exists()) {
        m_lastSeenMTime = QDateTime();
        m_lastSeenSize = -1;
        m_lastSeenHash.clear();
        // A missing file has no contents: keep serving the previous note's text
        // and the next write would copy it into this path.
        setRawText(QString());
        setReadOnlyReason(QString());
        setErrorString(i18n("File does not exist."));
        setStatus(Missing);
        return;
    }

    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastSeenMTime = QDateTime();
        m_lastSeenSize = -1;
        m_lastSeenHash.clear();
        setRawText(QString());
        setErrorString(file.errorString());
        setStatus(LoadError);
        return;
    }

    // UTF-8, no newline translation: whatever is in the file is what we hand out,
    // so writing it straight back is byte-identical. A leading BOM is remembered
    // (m_hasBom) and put back on every write instead of being silently dropped.
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        setErrorString(file.errorString());
        setStatus(LoadError);
        return;
    }
    file.close();

    QStringDecoder decoder(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
    const QString text = decoder.decode(bytes);
    const bool undecodable = decoder.hasError();

    adoptBytes(bytes, text);

    if (undecodable) {
        // S2: the file is shown (with U+FFFD where the bad bytes are) but every
        // write path refuses, so those bytes are never replaced on disk.
        const QString reason = i18n("This file contains bytes that are not valid UTF-8; it is open read-only so they are not destroyed.");
        setReadOnlyReason(reason);
        setErrorString(reason);
    } else {
        setReadOnlyReason(QString());
        setErrorString(QString());
    }
    setStatus(Ready);
    Q_EMIT reloaded();
}

void MarkdownNote::adoptBytes(const QByteArray &bytes, const QString &text)
{
    analyseBytes(bytes, text);
    setRawText(text);
    rememberFingerprint(bytes);
}

void MarkdownNote::analyseBytes(const QByteArray &bytes, const QString &text)
{
    const bool bom = bytes.startsWith(kBomBytes);

    int crlf = 0;
    int cr = 0;
    int lf = 0;
    const qsizetype n = text.size();
    for (qsizetype i = 0; i < n; ++i) {
        const char16_t c = text.at(i).unicode();
        if (c == u'\r') {
            if (i + 1 < n && text.at(i + 1) == QLatin1Char('\n')) {
                ++crlf;
                ++i;
            } else {
                ++cr;
            }
        } else if (c == u'\n') {
            ++lf;
        }
    }

    Eol eol = Eol::Lf;
    if (crlf > 0 && crlf >= lf && crlf >= cr) {
        eol = Eol::CrLf;
    } else if (cr > 0 && cr > lf && cr >= crlf) {
        eol = Eol::Cr;
    }
    const int kinds = (crlf > 0 ? 1 : 0) + (cr > 0 ? 1 : 0) + (lf > 0 ? 1 : 0);

    m_hasBom = bom;
    if (assign(m_eol, eol) | assign(m_mixedEol, kinds > 1)) {
        Q_EMIT lineEndingChanged();
    }
}

void MarkdownNote::rememberFingerprint(const QByteArray &bytes)
{
    const QFileInfo fresh(m_path);
    m_lastSeenMTime = fresh.lastModified();
    m_lastSeenSize = bytes.size();
    m_lastSeenHash = sha256(bytes);
}

QByteArray MarkdownNote::encode(const QString &text) const
{
    QByteArray bytes;
    if (m_hasBom && !text.startsWith(QChar(kBomChar))) {
        bytes += QByteArray::fromRawData(kBomBytes, 3);
    }
    bytes += text.toUtf8();
    return bytes;
}

bool MarkdownNote::diskUnchanged(QString *reason)
{
    QFileInfo info(m_path);
    info.refresh();

    if (!info.exists()) {
        if (m_lastSeenSize < 0) {
            return true; // we never saw a file here; creating one is not a conflict
        }
        *reason = i18n("The file was deleted on disk.");
        return false;
    }
    if (m_lastSeenSize < 0) {
        // Something put a file where we had none (or where we failed to read one).
        *reason = i18n("A file appeared at this path since the note was last read.");
        return false;
    }
    if (info.size() == m_lastSeenSize && info.lastModified() == m_lastSeenMTime) {
        return true;
    }

    // size/mtime moved: compare the real bytes before refusing, so a sync client
    // that rewrote identical content does not block the user forever.
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        *reason = file.errorString();
        return false;
    }
    const QByteArray bytes = file.readAll();
    const bool failed = file.error() != QFileDevice::NoError;
    file.close();
    if (failed) {
        *reason = i18n("The file could not be re-read before saving.");
        return false;
    }
    if (sha256(bytes) == m_lastSeenHash) {
        m_lastSeenSize = bytes.size();
        m_lastSeenMTime = info.lastModified();
        return true;
    }

    *reason = i18n("The file changed on disk since it was loaded.");
    return false;
}

/**
 * True when the file at m_path exists and already holds EXACTLY @p bytes, so a
 * write of those bytes would only bump the mtime. Any doubt (missing, unreadable,
 * short read) answers false, so the caller falls through and really writes.
 */
bool MarkdownNote::diskMatchesBytes(const QByteArray &bytes) const
{
    QFileInfo info(m_path);
    info.refresh();
    if (!info.exists() || !info.isFile() || info.size() != bytes.size()) {
        return false;
    }

    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray onDisk = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        return false;
    }
    return onDisk == bytes;
}

void MarkdownNote::stash(const QString &text)
{
    m_stashedWrite = text;
    m_hasStashedWrite = true;
}

bool MarkdownNote::performWrite(const QString &text, bool force, bool viaToggle)
{
    if (m_path.isEmpty()) {
        const QString reason = i18n("No file is open.");
        setErrorString(reason);
        if (viaToggle) {
            Q_EMIT toggleRejected(reason);
        } else {
            Q_EMIT saveFailed(reason);
        }
        return false;
    }

    // S2: a file we could not decode is never rewritten.
    if (!m_readOnlyReason.isEmpty()) {
        setErrorString(m_readOnlyReason);
        if (viaToggle) {
            Q_EMIT toggleRejected(m_readOnlyReason);
        } else {
            Q_EMIT saveFailed(m_readOnlyReason);
        }
        return false;
    }

    // S4: autosave (and every other implicit write) stops dead while a conflict
    // is on screen. Only keepMine() / reloadFromDisk() may resolve it.
    if (!force && m_externalChangePending) {
        const QString reason = i18n("The file changed on disk; your changes were not saved.");
        stash(text);
        setErrorString(reason);
        Q_EMIT conflictDetected();
        if (viaToggle) {
            Q_EMIT toggleRejected(reason);
        } else {
            Q_EMIT saveFailed(reason);
        }
        return false;
    }

    // Identical text is never written back: touching the mtime would wake up
    // every Obsidian sync client for nothing.
    //
    // exists() is part of the condition, not an optimisation: if the file was
    // deleted behind our back and KDirWatch has not delivered that yet, skipping
    // the write here would report a SUCCESSFUL save while the note stays gone
    // from disk. Falling through re-creates it (diskUnchanged() treats a missing
    // file we HAVE seen as a conflict, so the user is asked first).
    //
    // A forced write (keepMine()) cannot use m_raw as a stand-in for the file:
    // the whole premise there is that something else touched the file, so m_raw
    // may be stale in either direction. It compares the REAL bytes instead, and
    // when they already match it adopts the buffer + fingerprint silently: no
    // write, no mtime bump, no sync storm and no misleading saved().
    if (force) {
        const QByteArray bytes = encode(text);
        if (diskMatchesBytes(bytes)) {
            setRawText(text);
            rememberFingerprint(bytes);
            setErrorString(QString());
            setStatus(Ready);
            return true;
        }
    } else if (m_status == Ready && text == m_raw && QFileInfo::exists(m_path)) {
        return true;
    }

    // S3: re-stat + re-hash immediately before the write. No blind overwrite.
    if (!force) {
        QString why;
        if (!diskUnchanged(&why)) {
            stash(text);
            setErrorString(why);
            setExternalChangePending(true);
            Q_EMIT conflictDetected();
            if (viaToggle) {
                Q_EMIT toggleRejected(why);
            } else {
                Q_EMIT saveFailed(why);
            }
            return false;
        }
    }

    return writeToDisk(text);
}

bool MarkdownNote::writeToDisk(const QString &text)
{
    if (m_path.isEmpty()) {
        return false;
    }

    // Suspend the watch for the whole write so our own rename does not bounce back.
    m_watcher->removeFile(m_path);

    const QString parent = QFileInfo(m_path).absolutePath();
    if (!parent.isEmpty()) {
        QDir().mkpath(parent);
    }

    const QByteArray bytes = encode(text);

    QString failure;
    QSaveFile file(m_path);
    // S7: setDirectWriteFallback() is deliberately NOT enabled. Without a temp
    // file QSaveFile would truncate the user's note in place, so a crash or a
    // short write would leave a mangled file behind. Failing loudly is correct.
    if (!file.open(QIODevice::WriteOnly)) {
        failure = file.errorString();
    } else {
        if (file.write(bytes) != bytes.size()) {
            failure = file.errorString();
            file.cancelWriting();
            file.commit();
        } else if (!file.commit()) {
            failure = file.errorString();
        }
    }

    if (!failure.isEmpty()) {
        // m_raw keeps the last good text; the buffer in QML is untouched.
        stash(text);
        setErrorString(failure);
        setStatus(SaveError);
        rewatch();
        Q_EMIT saveFailed(failure);
        return false;
    }

    setRawText(text);
    rememberFingerprint(bytes);
    m_stashedWrite.clear();
    m_hasStashedWrite = false;

    setErrorString(QString());
    setStatus(Ready);
    setExternalChangePending(false);

    rewatch();
    Q_EMIT saved();
    return true;
}

void MarkdownNote::setRawText(const QString &text)
{
    if (text == m_raw) {
        return;
    }
    m_raw = text;
    m_rendered = TaskMarkdown::render(m_raw);
    Q_EMIT rawTextChanged();
    Q_EMIT renderedTextChanged();
}

void MarkdownNote::setReadOnlyReason(const QString &reason)
{
    if (assign(m_readOnlyReason, reason)) {
        Q_EMIT readOnlyReasonChanged();
    }
}

void MarkdownNote::setErrorString(const QString &error)
{
    if (assign(m_errorString, error)) {
        Q_EMIT errorStringChanged();
    }
}

void MarkdownNote::setStatus(Status status)
{
    if (assign(m_status, status)) {
        Q_EMIT statusChanged();
    }
}

void MarkdownNote::setExternalChangePending(bool pending)
{
    if (assign(m_externalChangePending, pending)) {
        Q_EMIT externalChangePendingChanged();
    }
}

void MarkdownNote::onFileDirty(const QString &path)
{
    if (m_path.isEmpty() || path != m_path) {
        return;
    }

    const QFileInfo info(m_path);
    if (!info.exists()) {
        m_lastSeenMTime = QDateTime();
        m_lastSeenSize = -1;
        m_lastSeenHash.clear();
        setErrorString(i18n("File does not exist."));
        setStatus(Missing);
        if (m_editing) {
            setExternalChangePending(true);
        }
        return;
    }

    // Belt and braces: our own write already suspended the watch, but a
    // notification that matches what we last wrote is never interesting.
    if (info.lastModified() == m_lastSeenMTime && info.size() == m_lastSeenSize) {
        return;
    }

    if (m_editing) {
        // Never clobber what the user is typing.
        setExternalChangePending(true);
        return;
    }

    loadFromDisk();
}

void MarkdownNote::rewatch()
{
    if (m_path.isEmpty()) {
        return;
    }
    if (!m_watcher->contains(m_path)) {
        m_watcher->addFile(m_path);
    }
}
