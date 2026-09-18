/*
    SPDX-FileCopyrightText: 2014 David Edmundson <davidedmundson@kde.org>
    SPDX-FileCopyrightText: 2014, 2015 Kai Uwe Broulik <kde@privat.broulik.de>
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors

    Derived from the Plasma "notes" applet (kdeplasma-addons, applets/notes).

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs

import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.components as PlasmaComponents3
import org.kde.plasma.extras as PlasmaExtras
import org.kde.plasma.plasmoid
import org.kde.kirigami as Kirigami
import org.kde.ksvg as KSvg

PlasmoidItem {
    id: root

    switchWidth: Kirigami.Units.gridUnit * 10
    switchHeight: Kirigami.Units.gridUnit * 10

    width: Kirigami.Units.gridUnit * 26 // default size for fullRepresentation
    height: Kirigami.Units.gridUnit * 30 // default size for fullRepresentation

    Plasmoid.icon: "text-markdown"
    // No Plasma frame: like upstream's notes applet, the full representation
    // draws the widgets/notes paper SVG itself (see `paper`).
    Plasmoid.backgroundHints: PlasmaCore.Types.NoBackground

    expandedOnDragHover: true
    preloadFullRepresentation: true

    toolTipMainText: i18n("Obsidian Note")
    toolTipSubText: note.path !== "" ? note.path : i18n("No file selected")

    /** THE state variable. false = VIEW (rendered), true = EDIT (raw markdown). */
    property bool editMode: false
    property bool scheduledForDestruction: false

    /**
     * Hard "do not write" latch (policy S5). Set around every path that must
     * reach the disk read without an implicit save in front of it -- reload,
     * conflict resolution, the discard prompt. commitEdit() is a no-op while it
     * is set, no matter who calls it or why.
     */
    property bool suppressCommit: false
    /** The discard-before-reload prompt is up; nothing may commit behind it. */
    property bool reloadPending: false
    /** Latched by conflictDetected() so the banner survives until the user chooses. */
    property bool conflictSeen: false

    /**
     * The single answer to "may commitEdit() write right now?".
     *
     * externalChangePending is in here because of policy S3/S4: once the file
     * moved underneath us the buffer belongs to the user, and only an explicit
     * Reload / Keep My Version may resolve it. This is also what makes the
     * conflict banner safe: Kirigami's InlineMessage action buttons take active
     * focus on mouse PRESS, so the editor's focus-out handler runs BEFORE
     * onTriggered -- without this guard, pressing "Reload" saved the editor
     * buffer over the very on-disk version it was about to load.
     */
    readonly property bool commitSuppressed: root.suppressCommit
        || root.reloadPending
        || note.externalChangePending

    /** Non-empty = the file must not be written; the string says why (policy S2). */
    readonly property string readOnlyReason: note.readOnlyReason
    readonly property bool readOnly: root.readOnlyReason !== ""

    /** Extensions we are willing to re-bind to on a drop. */
    readonly property var droppableSuffixes: [".md", ".markdown", ".mdown", ".mkd", ".mdwn", ".txt"]

    readonly property bool inPanel: [PlasmaCore.Types.TopEdge, PlasmaCore.Types.RightEdge, PlasmaCore.Types.BottomEdge, PlasmaCore.Types.LeftEdge].includes(Plasmoid.location)
    readonly property bool compactInPanel: inPanel && !!compactRepresentationItem?.visible
    /** Shown inside a panel popup: the popup dialog already has its own frame. */
    readonly property bool inPopup: root.inPanel && root.compactInPanel

    // this isn't a frameSVG, the default SVG margins take up around 7% of the frame size, so we use that
    // (upstream notes applet; Breeze's widgets/notes elements carry no hint-*-margin elements)
    readonly property int horizontalMargins: fullRep ? Math.round(fullRep.width * 0.07) : 0
    readonly property int verticalMargins: fullRep ? Math.round(fullRep.height * 0.07) : 0
    // In a panel when it is translucent, the panel background is used, so we use the normal text color, and remove any margins.
    property bool noBackground: false // binding in onCompleted to avoid binding loop

    // define colors used for icons in ToolButtons and for text in TextArea.
    // this is deliberately _NOT_ the theme color as we are over a known bright background!
    // except in a panel when it is translucent, the panel background is used, so we use the normal text color.
    // an unknown colour over a known colour is a bad move as you end up with white on yellow.
    readonly property color textIconColor: {
        if (noBackground) {
            return Kirigami.Theme.textColor;
        } else if (Plasmoid.configuration.color === "black" || Plasmoid.configuration.color === "translucent-light") {
            return "#dfdfdf";
        }
        return "#202020";
    }

    /**
     * The one colour source for everything drawn on the paper (rendered note,
     * raw editor, inline title, footer label). paperSample is filled in by the
     * full representation's sampler from the active theme's SVG element.
     */
    readonly property NotePalette notePalette: NotePalette {
        color: Plasmoid.configuration.color
        noBackground: root.noBackground
        textIconColor: root.textIconColor
    }
    // Dynamic lookups through this are expected to produce qmllint
    // "missing-property" warnings; the full representation is a Component.
    readonly property Item fullRep: fullRepresentationItem

    // KConfigPropertyMap has no onXChanged handlers, so mirror the one key whose
    // change has to be sequenced by hand (contract §5.3 rule 14).
    readonly property string configuredNotePath: Plasmoid.configuration.notePath
    onConfiguredNotePathChanged: root.syncNotePath()

    // Guards the create-if-missing one-shot so a failing createFile() cannot loop.
    property string createAttemptedFor: ""

    /**
     * Qualified handle on the backend.
     *
     * Inside NoteView's own scope the bare identifier `note` resolves to
     * NoteView's `required property MarkdownNote note` (the scope object wins
     * over an id declared in an enclosing component), which would silently make
     * `markdown: note.renderedText` a self-reference. Bindings written on a
     * NoteView must therefore go through root.noteBackend.
     */
    readonly property MarkdownNote noteBackend: note

    MarkdownNote {
        id: note

        // NOTE: path is assigned in syncNotePath(), not bound, so that the
        // in-editor buffer is always flushed to the OLD file first.
        editing: root.editMode

        onStatusChanged: {
            if (note.status === MarkdownNote.Missing
                && Plasmoid.configuration.createIfMissing
                && root.createAttemptedFor !== note.path) {
                root.createAttemptedFor = note.path;
                note.createFile();
            }
        }
        onSaveFailed: reason => root.reportError(reason)
        onToggleRejected: reason => root.reportError(reason)

        // S3: a write was refused because the file moved underneath us. Stop
        // autosave from hammering at it and latch the banner on.
        onConflictDetected: {
            autosaveTimer.stop();
            root.conflictSeen = true;
        }
        onReloaded: root.conflictSeen = false
        // A write that got through means the file and the buffer agree again.
        // Without this the banner LATCHES: toggleTask()'s "the file moved, I
        // reloaded the list" path emits conflictDetected() WITHOUT setting
        // externalChangePending, so nothing else ever clears conflictSeen and a
        // permanent, false "changed outside the widget" warning is left on
        // screen -- with a Reload button that throws away unsaved work.
        onSaved: root.conflictSeen = false
    }

    Timer {
        id: autosaveTimer
        interval: Plasmoid.configuration.autosaveInterval
        repeat: false
        running: false // armed only by the editor's textChanged
        onTriggered: root.commitEdit(false) // save, stay in EDIT
    }

    Timer {
        id: forceFocusTimer
        // FIXME (inherited from upstream): doing forceActiveFocus() directly in
        // onActivated does not work.
        interval: 1
        onTriggered: root.fullRep?.focusEditor()
    }

    Connections {
        target: Plasmoid
        function onActivated() {
            forceFocusTimer.restart();
        }
    }

    Plasmoid.onDestroyedChanged: destroyed => scheduledForDestruction = destroyed

    // A file was dropped onto the widget (or onto the desktop and routed here by
    // X-Plasma-DropMimeTypes). PlasmoidItem::externalData(mimetype, data).
    onExternalData: (mimetype, data) => root.handleExternalData(mimetype, data)

    // Click-out, part 2: the user clicked into a DIFFERENT window entirely.
    // On a panel popup this is what already closed the popup; on the desktop it
    // is the only thing that fires when the click lands on another application.
    readonly property bool hostWindowActive: root.Window.window !== null && root.Window.active

    /**
     * A menu of ours is up. Opening one can deactivate the host window on some
     * platforms, and an implicit commit fired by THAT is how "Reload from Disk"
     * ends up reloading the text it just saved instead of the on-disk version.
     */
    property bool menuOpen: false
    onContextualActionsAboutToShow: root.menuOpen = true

    onHostWindowActiveChanged: {
        if (root.hostWindowActive) {
            root.menuOpen = false; // we are back in front; any menu is long gone
        } else if (root.editMode && !root.menuOpen) {
            root.commitEdit(true);
        }
    }

    // Save on the way out, and NEVER delete the vault file (contract §10.8).
    // commitEdit() still refuses to write behind an unresolved conflict (S3).
    Component.onDestruction: root.commitEdit(true)

    Component.onCompleted: {
        if (!Plasmoid.configuration.fontSize) {
            // 12 pt = 16 px, Obsidian's default body size. Only ever written for
            // a fresh instance; a stored size is never rewritten.
            Plasmoid.configuration.fontSize = 12;
        }
        root.syncNotePath();

        // set imperatively to avoid binding loop during startup
        root.noBackground = Qt.binding(() => inPanel && (Plasmoid.configuration.color === "translucent" || Plasmoid.configuration.color === "translucent-light"))
    }

    //
    // ---- state machine entry points -------------------------------------
    //

    /** VIEW -> EDIT. sourcePos is an offset into the editor buffer, or -1 for "restore/end". */
    function enterEdit(sourcePos: int) {
        if (note.status === MarkdownNote.NoPath) {
            root.pickFile();
            return;
        }
        if (root.readOnly) {
            // S2: the file cannot be written back byte-faithfully. Say why.
            root.reportError(root.readOnlyReason);
            return;
        }
        if (root.editMode) {
            forceFocusTimer.restart();
            return;
        }
        root.editMode = true;
        Qt.callLater(() => {
            const rep = root.fullRep;
            if (rep) {
                rep.beginEdit(sourcePos);
            }
        });
    }

    /**
     * VIEW -> EDIT from a click on the rendered view: caret at the start of the
     * clicked block's source line, and that line kept at the block's height
     * (blockTopY, view coordinates) so the text under the pointer does not jump.
     * sourceLine -1 keeps the old restore/end behaviour.
     */
    function enterEditAtLine(sourceLine: int, blockTopY: real) {
        if (sourceLine < 0) {
            root.enterEdit(-1);
            return;
        }
        const lines = note.editorText.split("\n");
        let pos = 0;
        for (let i = 0; i < Math.min(sourceLine, lines.length); ++i) {
            pos += lines[i].length + 1;
        }
        const wasEditing = root.editMode;
        root.enterEdit(Math.min(pos, note.editorText.length));
        if (!wasEditing && root.editMode) {
            root.pendingEditAnchorY = blockTopY;
        }
    }

    /** View y the caret line should land on when beginEdit() runs; NaN when unanchored. */
    property real pendingEditAnchorY: NaN

    /** Flush the editor buffer to disk; leaveMode also drops back to VIEW. */
    function commitEdit(leaveMode: bool) {
        autosaveTimer.stop();
        if (root.commitSuppressed) {
            // Never write, and never drop the buffer either: it is the only copy
            // of the user's text until they resolve things themselves.
            return;
        }
        const ed = root.fullRep; // may be null during teardown
        // Only bounce the keyboard focus back to the rendered view when the
        // editor still had it -- i.e. the user pressed Esc / Done, rather than
        // clicking onto something else that now legitimately owns the focus.
        const refocusView = leaveMode && root.editMode && !!ed && ed.editorHasFocus;
        if (ed && root.editMode && ed.editorPrimed) {
            const pos = ed.editorCursorPosition;
            if (Plasmoid.configuration.cursorPosition !== pos) {
                Plasmoid.configuration.cursorPosition = pos;
            }
            if (note.status !== MarkdownNote.NoPath && !root.readOnly) {
                // S2: hand over the plain LF-normalised QQuickTextEdit buffer and
                // let C++ put the file's own line endings and BOM back.
                note.saveBuffer(ed.editorText); // no-ops when unchanged
            }
        }
        if (leaveMode) {
            root.editMode = false;
            if (refocusView) {
                Qt.callLater(root.focusView);
            }
        }
    }

    /** Puts the keyboard focus on the rendered view, where Ctrl+E / Esc live. */
    function focusView() {
        root.fullRep?.focusView();
    }

    function pickFile() {
        fileDialogLoader.open();
    }

    /**
     * Percent-encode a local path into a file:// URL.
     *
     * encodeURI() is NOT enough and plain concatenation is a bug: "#" starts a
     * URL fragment and "?" a query, so a vault folder called "Notes #2" sent the
     * file picker to "Notes " (or nowhere at all). Every segment is encoded
     * separately so the "/" separators survive.
     */
    function fileUrlForPath(path: string): string {
        if (path === "") {
            return "";
        }
        return "file://" + path.split("/").map(encodeURIComponent).join("/");
    }

    /**
     * S5. Discards the in-memory buffer and takes what is on disk. NEVER saves
     * first -- not even implicitly -- so the suppressCommit latch is raised
     * around the whole sequence, including the editMode change that drops the
     * editor's focus and would otherwise re-enter commitEdit().
     */
    function doReload() {
        root.suppressCommit = true;
        root.reloadPending = false;
        root.conflictSeen = false;
        root.editMode = false;
        note.reloadFromDisk();
        Qt.callLater(() => { root.suppressCommit = false; });
    }

    /** True when EDIT mode holds text that is not on disk. */
    function hasUnsavedEdits(): bool {
        const ed = root.fullRep;
        return !!ed && root.editMode && ed.editorPrimed && ed.editorText !== note.editorText;
    }

    /** User-facing "Reload from Disk": confirm first if that would throw work away. */
    function reloadFromDisk() {
        if (root.hasUnsavedEdits()) {
            root.reloadPending = true; // raises commitSuppressed too
            // The prompt lives in the full representation, so make sure it is on
            // screen. Collapsing the popup does NOT flush the buffer while a
            // conflict is unresolved -- an implicit commit refuses to write --
            // so "the popup is closed" must never mean "discard it silently".
            if (!root.expanded) {
                root.expanded = true;
            }
            return;
        }
        root.doReload();
    }

    /**
     * Conflict banner, "Reload": take the on-disk version, write nothing.
     * Routed through reloadFromDisk() so it cannot silently destroy an unsaved
     * editor buffer -- "the file changed outside" is not consent to discard the
     * paragraphs the user just typed; they get the Discard/Keep Editing prompt.
     */
    function resolveConflictReload() {
        root.reloadFromDisk();
    }

    /**
     * Conflict banner, "Keep My Version": force the buffer over the file.
     * The refused save() is deliberate -- it re-stashes the CURRENT buffer (the
     * user may have kept typing after the banner appeared) so that keepMine()
     * writes what is on screen and not what was in the editor minutes ago.
     */
    function resolveConflictKeepMine() {
        const ed = root.fullRep;
        if (ed && root.editMode && ed.editorPrimed && !root.readOnly) {
            note.saveBuffer(ed.editorText);
        }
        root.conflictSeen = false;
        note.keepMine();
    }

    function openInObsidian() {
        Qt.openUrlExternally(note.obsidianUrl());
    }

    /** A checkbox in the rendered view was clicked (S6: expected line text travels along). */
    function toggleTask(line: int, expected: string) {
        if (root.readOnly) {
            root.reportError(root.readOnlyReason);
            return;
        }
        note.toggleTask(line, expected); // stay in VIEW, do not focus
    }

    /** Single dispatch point for every link in the rendered document. */
    function handleLink(link: string) {
        if (link === "") {
            return;
        }
        const toggleLine = note.toggleLineForLink(link);
        if (toggleLine >= 0) {
            if (root.readOnly) {
                root.reportError(root.readOnlyReason);
                return;
            }
            // S6: hand the expected line text along so the backend can refuse a
            // stale index instead of flipping whatever now sits on that line.
            note.toggleTask(toggleLine, note.lineTextAt(toggleLine)); // stay in VIEW, do not focus
            return;
        }
        if (link.startsWith("obsnote:tag/")) {
            return; // tags are display-only pills; clicking one does nothing
        }
        const wikiTarget = note.wikilinkTargetForLink(link);
        if (wikiTarget !== "") {
            Qt.openUrlExternally(note.obsidianUrl(wikiTarget));
            return;
        }
        Qt.openUrlExternally(link);
    }

    function reportError(message: string) {
        root.fullRep?.showError(message);
    }

    /** Sequenced notePath change: flush to the old file, reset per-file state, rebind. */
    function syncNotePath() {
        if (note.path === root.configuredNotePath) {
            return;
        }
        root.commitEdit(true);
        root.editMode = false; // commitEdit is a no-op while a conflict is unresolved
        root.reloadPending = false;
        root.conflictSeen = false;
        if (Plasmoid.configuration.cursorPosition !== -1) {
            Plasmoid.configuration.cursorPosition = -1;
        }
        if (Plasmoid.configuration.scrollY !== 0) {
            Plasmoid.configuration.scrollY = 0;
        }
        note.path = root.configuredNotePath;
    }

    //
    // ---- drag and drop ----------------------------------------------------
    //
    // DECISION: metadata.json advertises this applet as a markdown drop target,
    // so a dropped .md REBINDS the note to that file. Inserting the dropped
    // text into the vault file instead would be a silent, unasked-for write to
    // the user's note (policy: data loss is the worst outcome); rebinding is
    // non-destructive and is undone by dropping the old file back.

    /** "/a/b/Note.MD" -> true. */
    function isDroppableFile(path: string): bool {
        const lower = path.toLowerCase();
        return root.droppableSuffixes.some(s => lower.endsWith(s));
    }

    /**
     * Rebind to the first droppable local file in @p candidates (URLs or paths).
     * Returns true when something was accepted.
     */
    function acceptDroppedPaths(candidates): bool {
        if (!candidates) {
            return false;
        }
        let sawFile = false;
        for (let i = 0; i < candidates.length; ++i) {
            const raw = candidates[i];
            if (raw === undefined || raw === null || String(raw) === "") {
                continue;
            }
            const path = note.localPathFromUrl(String(raw));
            if (path === "") {
                continue;
            }
            sawFile = true;
            if (!root.isDroppableFile(path)) {
                continue;
            }
            if (path !== Plasmoid.configuration.notePath) {
                root.commitEdit(true);
                root.editMode = false;
                Plasmoid.configuration.notePath = path;
            }
            return true;
        }
        if (sawFile) {
            root.reportError(i18n("Only Markdown or plain-text files can be dropped here."));
        }
        return false;
    }

    /** Split a text/uri-list (or a bare path) payload into candidates. */
    function candidatesFromText(text: string): var {
        return text.split(/[\r\n]+/).filter(line => line !== "" && !line.startsWith("#"));
    }

    /** Containment-level drop (X-Plasma-DropMimeTypes in metadata.json). */
    function handleExternalData(mimetype: string, data): bool {
        if (data === undefined || data === null) {
            return false;
        }
        if (Array.isArray(data)) {
            return root.acceptDroppedPaths(data);
        }
        return root.acceptDroppedPaths(root.candidatesFromText(String(data)));
    }

    function runContextualAction(actionId: string) {
        if (actionId === "open") {
            root.pickFile();
        } else if (actionId === "reload") {
            root.reloadFromDisk();
        } else if (actionId === "obsidian") {
            root.openInObsidian();
        }
    }

    //
    // ---- widget context menu (right click on the applet) -----------------
    //

    Instantiator {
        model: [
            { actionId: "open", text: i18nc("@action", "Open Markdown File…"), iconName: "document-open" },
            { actionId: "reload", text: i18nc("@action", "Reload from Disk"), iconName: "view-refresh" },
            { actionId: "obsidian", text: i18nc("@action", "Open in Obsidian"), iconName: "emblem-symbolic-link" },
        ]

        onObjectAdded: (index, object) => {
            Plasmoid.contextualActions.push(object);
        }

        PlasmaCore.Action {
            required text
            required property string iconName
            required property string actionId

            icon.name: iconName
            onTriggered: root.runContextualAction(actionId)
        }
    }

    // ---- note colour (upstream notes applet) ----
    PlasmaCore.ActionGroup {
        id: noteColorGroup
    }

    Instantiator {
        model: {
            let model = [
                {text: i18nc("@item:inmenu", "White"), color: "white"},
                {text: i18nc("@item:inmenu", "Black"), color: "black"},
                {text: i18nc("@item:inmenu", "Red"), color: "red"},
                {text: i18nc("@item:inmenu", "Orange"), color: "orange"},
                {text: i18nc("@item:inmenu", "Yellow"), color: "yellow"},
                {text: i18nc("@item:inmenu", "Green"), color: "green"},
                {text: i18nc("@item:inmenu", "Blue"), color: "blue"},
                {text: i18nc("@item:inmenu", "Pink"), color: "pink"},
                {text: i18nc("@item:inmenu", "Transparent"), color: "translucent"},
            ];
            // Explicit translucent light makes no sense in a panel since it will always be the popup background.
            if (!root.inPanel) {
                model.push({text: i18nc("@item:inmenu", "Transparent Light"), color: "translucent-light"});
            }
            return model;
        }

        onObjectAdded: (index, object) => {
            Plasmoid.contextualActions.push(object);
        }

        PlasmaCore.Action {
            required text
            required property string color

            icon.icon: NotesHelper.noteIcon(color)
            actionGroup: noteColorGroup
            checkable: true
            checked: (Plasmoid.configuration.color === color)
            // Pretend to be translucent if translucent light in a panel to allow roaming between panel and desktop.
                     || (root.inPanel && color === "translucent" && Plasmoid.configuration.color === "translucent-light")
            onTriggered: Plasmoid.configuration.color = color
        }
    }

    //
    // ---- file dialog ------------------------------------------------------
    //
    // Kept at root level (not inside the full representation) so the widget
    // context menu can open it while the popup is collapsed.
    Loader {
        id: fileDialogLoader

        active: false

        function open() {
            if (item) {
                item.syncFolder();
                item.open();
            } else {
                active = true;
            }
        }

        onLoaded: {
            item.syncFolder();
            item.open();
        }

        sourceComponent: FileDialog {
            id: fileDialog

            title: i18nc("@title:window", "Choose Markdown File")
            nameFilters: [i18n("Markdown files (*.md *.markdown)"), i18n("All files (*)")]
            fileMode: FileDialog.OpenFile

            // No `import QtCore`, so when there is no path yet we simply leave
            // currentFolder alone and let the platform pick the default.
            // Assigned imperatively: a `currentFolder: ... : currentFolder`
            // binding would be self-referential.
            function syncFolder() {
                const slash = note.path.lastIndexOf("/");
                if (slash > 0) {
                    // Percent-encoded: "#" and "?" in a folder name would
                    // otherwise truncate the URL and open the wrong directory.
                    fileDialog.currentFolder = root.fileUrlForPath(note.path.substring(0, slash));
                }
            }

            onAccepted: {
                const p = note.localPathFromUrl(selectedFile);
                if (p !== "" && p !== Plasmoid.configuration.notePath) {
                    root.commitEdit(true);
                    Plasmoid.configuration.notePath = p;
                }
            }
        }
    }

    //
    // ---- full representation ---------------------------------------------
    //

    fullRepresentation: Component {
        Item {
            id: fullRep

            // Magic property that makes the popup resizable. Keep.
            readonly property QtObject appletInterface: root

            property alias editorText: editor.text
            property alias editorCursorPosition: editor.cursorPosition
            /** True while the TextArea itself owns the keyboard focus. */
            property alias editorHasFocus: editor.editorActiveFocus
            /** False until beginEdit() has copied the buffer in; commitEdit() checks it. */
            property bool editorPrimed: false

            Layout.minimumWidth: root.inPanel && !root.compactInPanel ? -1 : Kirigami.Units.gridUnit * 10
            Layout.minimumHeight: root.inPanel && !root.compactInPanel ? -1 : Kirigami.Units.gridUnit * 8

            function focusEditor() {
                if (root.editMode) {
                    editor.forceEditorFocus();
                } else {
                    fullRep.focusView();
                }
            }

            /** Makes viewFocusScope the window's active focus item so Ctrl+E / Esc work. */
            function focusView() {
                viewFocusScope.forceActiveFocus();
            }

            function commit() {
                root.commitEdit(true);
            }

            /** Imperative, once, on entering EDIT -- never a Binding. */
            function beginEdit(sourcePos: int) {
                if (!root.editMode) {
                    return;
                }
                // S2: editorText is rawText with every line ending normalised to
                // "\n", which is the only thing a QQuickTextEdit can hold anyway.
                // C++ restores the file's own endings on the way back out.
                editor.text = note.editorText;
                editor.scrollToTop();
                fullRep.editorPrimed = true;
                const stored = sourcePos >= 0 ? sourcePos : Plasmoid.configuration.cursorPosition;
                editor.cursorPosition = stored < 0
                    ? editor.length
                    : Math.max(0, Math.min(stored, editor.length));
                editor.forceEditorFocus();
                const anchorY = root.pendingEditAnchorY;
                root.pendingEditAnchorY = NaN;
                if (sourcePos >= 0 && !isNaN(anchorY)) {
                    const pos = editor.cursorPosition;
                    // After the TextArea's own ensure-cursor-visible pass.
                    Qt.callLater(() => editor.alignPositionTo(pos, anchorY));
                }
            }

            function showError(message: string) {
                errorBanner.show(message);
            }

            function saveViewScroll() {
                if (root.editMode) {
                    return;
                }
                const y = noteView.saveScroll();
                if (Plasmoid.configuration.scrollY !== y) {
                    Plasmoid.configuration.scrollY = y;
                }
            }

            function restoreViewScroll() {
                noteView.restoreScroll(Plasmoid.configuration.scrollY);
            }

            Component.onCompleted: Qt.callLater(fullRep.restoreViewScroll)
            Component.onDestruction: fullRep.saveViewScroll()

            Connections {
                target: root

                function onExpandedChanged(expanded) {
                    if (expanded) {
                        // Don't autofocus when we're on the desktop.
                        if (Plasmoid.formFactor === PlasmaCore.Types.Vertical
                            || Plasmoid.formFactor === PlasmaCore.Types.Horizontal) {
                            fullRep.focusEditor();
                        }
                    } else {
                        fullRep.saveViewScroll();
                        root.commitEdit(true);
                    }
                }

                function onEditModeChanged() {
                    if (!root.editMode) {
                        fullRep.editorPrimed = false;
                        root.reloadPending = false;
                        Qt.callLater(fullRep.restoreViewScroll);
                    }
                }
            }

            // The sticky-note paper (upstream notes applet): the widgets/notes
            // element of the configured colour, drawn by the Plasma theme.
            KSvg.SvgItem {
                id: paper

                anchors.fill: parent
                imagePath: root.noBackground ? "" : "widgets/notes"
                elementId: Plasmoid.configuration.color + "-notes"
            }

            // Samples the active theme's paper element (top, middle, bottom of
            // its gradient) into root.notePalette.paperSample, so the contrast floors
            // hold for themes whose notes.svg is not Breeze's. Invisible: an
            // opacity-0 parent does not stop grabToImage() from rendering it.
            Item {
                id: paperSampler

                width: 48
                height: 48
                opacity: 0
                enabled: false

                readonly property string element: root.noBackground ? "" : paperSampleSvg.elementId

                KSvg.SvgItem {
                    id: paperSampleSvg

                    anchors.fill: parent
                    imagePath: "widgets/notes"
                    elementId: Plasmoid.configuration.color + "-notes"
                }

                Canvas {
                    id: paperSampleCanvas

                    anchors.fill: parent
                    property url grabbed
                    property string forElement

                    onImageLoaded: paperSampleCanvas.requestPaint()
                    onPaint: {
                        if (paperSampleCanvas.grabbed.toString() === ""
                            || !paperSampleCanvas.isImageLoaded(paperSampleCanvas.grabbed)) {
                            return;
                        }
                        const ctx = paperSampleCanvas.getContext("2d");
                        ctx.clearRect(0, 0, width, height);
                        ctx.drawImage(paperSampleCanvas.grabbed, 0, 0, width, height);
                        const d = ctx.getImageData(0, 0, width, height).data;
                        const stops = [];
                        // Rows at 15 / 50 / 85 %, averaged over the middle half of the width.
                        for (const fy of [0.15, 0.5, 0.85]) {
                            const y = Math.floor(height * fy);
                            let r = 0, g = 0, b = 0, n = 0;
                            for (let x = Math.floor(width / 4); x < Math.ceil(width * 3 / 4); ++x) {
                                const i = (y * width + x) * 4;
                                if (d[i + 3] < 250) {
                                    // Not an opaque paper (translucent element, or missing): keep the table.
                                    root.notePalette.paperSample = null;
                                    return;
                                }
                                r += d[i]; g += d[i + 1]; b += d[i + 2]; ++n;
                            }
                            stops.push(Qt.rgba(r / n / 255, g / n / 255, b / n / 255, 1));
                        }
                        if (paperSampleCanvas.forElement === paperSampler.element) {
                            root.notePalette.paperSample = stops;
                        }
                    }
                }

                function sample() {
                    if (paperSampler.element === "") {
                        root.notePalette.paperSample = null;
                        return;
                    }
                    const forElement = paperSampler.element;
                    paperSampleSvg.grabToImage(result => {
                        paperSampleCanvas.forElement = forElement;
                        if (paperSampleCanvas.grabbed.toString() !== "") {
                            paperSampleCanvas.unloadImage(paperSampleCanvas.grabbed);
                        }
                        paperSampleCanvas.grabbed = result.url;
                        paperSampleCanvas.loadImage(result.url);
                        if (paperSampleCanvas.isImageLoaded(result.url)) {
                            paperSampleCanvas.requestPaint();
                        }
                    });
                }

                onElementChanged: {
                    // The table is right for Breeze until the new sample lands.
                    root.notePalette.paperSample = null;
                    sampleTimer.restart();
                }
                Component.onCompleted: sampleTimer.restart()

                Timer {
                    id: sampleTimer
                    interval: 250
                    onTriggered: paperSampler.sample()
                }
            }

            ColumnLayout {
                id: mainColumn

                anchors {
                    fill: parent
                    leftMargin: root.noBackground ? 0 : root.horizontalMargins
                    rightMargin: root.noBackground ? 0 : root.horizontalMargins
                    topMargin: root.noBackground ? 0 : root.verticalMargins
                    bottomMargin: root.noBackground ? 0 : root.verticalMargins
                }
                spacing: 0

                // Everything on the paper that is drawn with theme colours
                // (banners, placeholder, footer label) gets the paper's palette
                // instead: dark theme text on yellow paper is unreadable.
                Kirigami.Theme.textColor: root.notePalette.text
                Kirigami.Theme.disabledTextColor: root.notePalette.mutedText
                Kirigami.Theme.linkColor: root.notePalette.link
                Kirigami.Theme.visitedLinkColor: root.notePalette.link

                Kirigami.InlineMessage {
                    id: conflictBanner

                    Layout.fillWidth: true
                    position: Kirigami.InlineMessage.Position.Header
                    type: Kirigami.MessageType.Warning
                    visible: note.externalChangePending || root.conflictSeen
                    text: i18n("This file was changed outside the widget.")

                    // NOTE: these buttons take active focus on mouse PRESS, which
                    // runs the editor's focus-out handler BEFORE onTriggered. That
                    // is safe only because root.commitSuppressed is true whenever
                    // this banner can be on screen -- do not weaken that guard.
                    actions: [
                        Kirigami.Action {
                            text: i18nc("@action:button", "Reload")
                            icon.name: "view-refresh"
                            onTriggered: root.resolveConflictReload()
                        },
                        Kirigami.Action {
                            text: i18nc("@action:button", "Keep My Version")
                            icon.name: "document-save"
                            onTriggered: {
                                root.resolveConflictKeepMine();
                                fullRep.focusEditor();
                            }
                        }
                    ]
                }

                Kirigami.InlineMessage {
                    id: readOnlyBanner

                    Layout.fillWidth: true
                    position: Kirigami.InlineMessage.Position.Header
                    type: Kirigami.MessageType.Warning
                    visible: root.readOnly
                    text: root.readOnlyReason
                }

                Kirigami.InlineMessage {
                    id: discardBanner

                    Layout.fillWidth: true
                    position: Kirigami.InlineMessage.Position.Header
                    type: Kirigami.MessageType.Warning
                    visible: root.reloadPending
                    text: i18n("Reloading will throw away the changes you have not saved yet.")

                    actions: [
                        Kirigami.Action {
                            text: i18nc("@action:button", "Discard and Reload")
                            icon.name: "view-refresh"
                            onTriggered: root.doReload()
                        },
                        Kirigami.Action {
                            text: i18nc("@action:button", "Keep Editing")
                            icon.name: "dialog-cancel"
                            onTriggered: {
                                root.reloadPending = false;
                                fullRep.focusEditor();
                            }
                        }
                    ]
                }

                Kirigami.InlineMessage {
                    id: errorBanner

                    Layout.fillWidth: true
                    position: Kirigami.InlineMessage.Position.Header
                    type: Kirigami.MessageType.Error
                    visible: false

                    function show(message: string) {
                        errorBanner.text = message;
                        errorBanner.visible = true;
                        hideTimer.restart();
                    }

                    Timer {
                        id: hideTimer
                        interval: 8000
                        onTriggered: errorBanner.visible = false
                    }
                }

                // PlasmaExtras.PlaceholderMessage's own shape, rebuilt so that
                // every glyph takes its colour from notePalette.
                //
                // It CANNOT be the stock component: inside a plasmoid the
                // Kirigami.Theme attached object is Plasma's PlasmaTheme, which
                // re-derives every colour from the Plasma colour scheme per item
                // instead of inheriting a custom colour from an ancestor. The
                // Kirigami.Theme.* block above therefore reaches mainColumn only
                // (measured: mainColumn textColor #dfdfdf, its Heading/Label
                // descendants #232629), which left the stock placeholder drawing
                // dark theme text on black paper at 1.05 : 1.
                ColumnLayout {
                    id: placeholder

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: Kirigami.Units.gridUnit
                    spacing: Kirigami.Units.gridUnit

                    visible: note.status === MarkdownNote.NoPath
                        || note.status === MarkdownNote.Missing
                        || note.status === MarkdownNote.LoadError

                    readonly property string title: {
                        switch (note.status) {
                        case MarkdownNote.Missing:
                            return i18n("The Markdown file does not exist");
                        case MarkdownNote.LoadError:
                            return i18n("The Markdown file could not be read");
                        default:
                            return i18n("No Markdown file selected");
                        }
                    }
                    readonly property string explanation: note.status === MarkdownNote.NoPath
                        ? i18n("Pick a note from your Obsidian vault to show and edit it here.")
                        : (note.errorString !== "" ? note.errorString : note.path)

                    Item {
                        Layout.fillHeight: true
                    }

                    Kirigami.Icon {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: Math.round(Kirigami.Units.iconSizes.huge * 1.5)
                        Layout.preferredHeight: Layout.preferredWidth

                        source: "text-markdown"
                        // text-markdown is a mimetype icon (hardcoded #6c7a89):
                        // drawn as a mask it follows the paper like every other glyph.
                        isMask: true
                        color: root.notePalette.mutedText
                    }

                    PlasmaExtras.Heading {
                        Layout.fillWidth: true
                        horizontalAlignment: Qt.AlignHCenter
                        wrapMode: Text.Wrap
                        type: PlasmaExtras.Heading.Type.Primary
                        text: placeholder.title
                        color: root.notePalette.text
                    }

                    PlasmaComponents3.Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Qt.AlignHCenter
                        wrapMode: Text.Wrap
                        visible: placeholder.explanation !== ""
                        text: placeholder.explanation
                        color: root.notePalette.text
                        linkColor: root.notePalette.link
                    }

                    PlasmaComponents3.Button {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: Kirigami.Units.gridUnit

                        text: i18nc("@action:button", "Choose Markdown File…")
                        icon.name: "document-open"
                        onClicked: root.pickFile()
                    }

                    Item {
                        Layout.fillHeight: true
                    }
                }

                StackLayout {
                    id: contentStack

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: !placeholder.visible
                    currentIndex: root.editMode ? 1 : 0

                    FocusScope {
                        id: viewFocusScope

                        Keys.onPressed: event => {
                            if (event.key === Qt.Key_Escape) {
                                // Standard Plasma popup dismissal.
                                root.expanded = false;
                                event.accepted = true;
                            } else if (event.key === Qt.Key_E && (event.modifiers & Qt.ControlModifier)) {
                                root.enterEdit(-1);
                                event.accepted = true;
                            }
                        }

                        NoteView {
                            id: noteView

                            anchors.fill: parent
                            focus: true

                            // Must be qualified; see root.noteBackend.
                            note: root.noteBackend
                            notePalette: root.notePalette
                            basePointSize: Plasmoid.configuration.fontSize || 12
                            fontFamily: Plasmoid.configuration.fontFamily !== ""
                                ? Plasmoid.configuration.fontFamily
                                : Kirigami.Theme.defaultFont.family
                            inlineTitle: root.noteBackend.fileName.replace(/\.md$/i, "")
                            showInlineTitle: Plasmoid.configuration.showInlineTitle
                            showProperties: Plasmoid.configuration.showProperties
                            readOnly: root.readOnly

                            onEditRequested: (sourceLine, blockTopY) => root.enterEditAtLine(sourceLine, blockTopY)
                            onTaskToggleRequested: (line, expected) => root.toggleTask(line, expected)
                            onLinkClicked: link => root.handleLink(link)
                            onContextMenuRequested: contextMenu.popup()
                        }
                    }

                    FocusScope {
                        id: editFocusScope

                        NoteEditor {
                            id: editor

                            anchors.fill: parent
                            focus: true
                            notePalette: root.notePalette

                            // NoteEditor deliberately does not import
                            // org.kde.plasma.plasmoid, so the configuration
                            // lookups live here instead.
                            // Kirigami.Theme exposes only defaultFont and smallFont --
                            // there is no fixedFont -- so use the generic family that
                            // fontconfig resolves to the system monospace face. This is
                            // what Plasma's own QML (AppletError.qml) does.
                            font.family: Plasmoid.configuration.monospaceInEditMode
                                ? "monospace"
                                : (Plasmoid.configuration.fontFamily !== ""
                                    ? Plasmoid.configuration.fontFamily
                                    : Kirigami.Theme.defaultFont.family)
                            // Same base size and padding as the view: VIEW <-> EDIT must not jump.
                            font.pointSize: noteView.basePointSize
                            contentPadding: noteView.metrics.containerPadding
                            headerSpacing: noteView.metrics.inlineTitleMarginBottom
                            header: noteView.showInlineTitle && noteView.inlineTitle !== "" ? editorTitle : null

                            Component {
                                id: editorTitle

                                InlineText {
                                    width: parent ? parent.width : 0
                                    html: "<p style=\"margin:0\">" + noteView.metrics.escapeHtml(noteView.inlineTitle) + "</p>"
                                    decorations: []
                                    metrics: noteView.metrics
                                    fontPx: noteView.metrics.inlineTitleSize
                                    fontWeight: noteView.metrics.headingWeight(1)
                                    fontMetrics: noteView.metrics.headingMetrics(1)
                                    letterSpacing: noteView.metrics.headingLetterSpacing(1)
                                    lineHeightPx: noteView.metrics.inlineTitleLineHeight
                                    struck: false
                                    availableWidth: width
                                }
                            }

                            // THE click-out path on a desktop containment. Armed
                            // only in EDIT mode; "inside" is the whole widget, so
                            // the toolbar and the banner action buttons do not
                            // count as clicking out.
                            watchOutsidePress: root.editMode
                            outsideBoundsItem: fullRep

                            onEscapePressed: root.commitEdit(true)
                            onSavePressed: root.commitEdit(false)
                            onContextMenuRequested: contextMenu.popup()
                            onOutsidePressed: root.commitEdit(true)

                            onTextChanged: {
                                if (root.editMode && fullRep.editorPrimed
                                    && autosaveTimer.interval > 0 && !root.commitSuppressed) {
                                    autosaveTimer.restart();
                                }
                            }

                            onEditorActiveFocusChanged: {
                                const window = Window.window;
                                if (editor.editorActiveFocus) {
                                    if (window && (window.flags & Qt.WindowDoesNotAcceptFocus)) {
                                        Plasmoid.status = PlasmaCore.Types.AcceptingInputStatus;
                                    }
                                } else {
                                    Plasmoid.status = PlasmaCore.Types.ActiveStatus;
                                    if (root.editMode && !contextMenu.visible) {
                                        root.commitEdit(true);
                                    }
                                }
                            }
                        }
                    }
                }

                // Footer. Deliberately NOT a PlasmaExtras.PlasmoidHeading: that paints
                // Plasma's translucent footer SVG and gives itself negative insets so it
                // reaches the edge of the standard applet frame. With NoBackground there
                // is no frame, so it overhung the opaque card by the frame margins.
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    color: root.notePalette.border
                }

                RowLayout {
                    id: toolbarRow

                    Layout.fillWidth: true
                    // Line the file name up with the note text column.
                    Layout.leftMargin: noteView.metrics.containerPadding
                    Layout.rightMargin: Kirigami.Units.smallSpacing
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    Layout.bottomMargin: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing


                    PlasmaComponents3.Label {
                        id: fileLabel

                        Layout.fillWidth: true
                        visible: Plasmoid.configuration.showFileName
                        text: note.fileName
                        color: root.notePalette.text
                        elide: Text.ElideMiddle
                        textFormat: Text.PlainText

                        PlasmaComponents3.ToolTip {
                            text: note.path
                            visible: fileLabel.hovered && note.path !== ""
                        }

                        HoverHandler {
                            id: fileLabelHover
                        }

                        property bool hovered: fileLabelHover.hovered
                    }

                    Item { // spacer, keeps the buttons right-aligned when the label is hidden
                        Layout.fillWidth: !fileLabel.visible
                    }

                    PlasmaComponents3.ToolButton {
                        id: modeButton

                        focusPolicy: Qt.TabFocus
                        icon.color: root.textIconColor
                        display: PlasmaComponents3.AbstractButton.IconOnly
                        icon.name: root.editMode ? "document-save" : "document-edit"
                        text: root.editMode ? i18nc("@action:button", "Done") : i18nc("@action:button", "Edit")
                        enabled: (note.status === MarkdownNote.Ready && !root.readOnly) || root.editMode
                        onClicked: {
                            if (root.editMode) {
                                root.commitEdit(true);
                            } else {
                                root.enterEdit(-1);
                            }
                        }

                        PlasmaComponents3.ToolTip {
                            text: modeButton.text
                        }
                    }

                    PlasmaComponents3.ToolButton {
                        id: reloadButton

                        focusPolicy: Qt.TabFocus
                        icon.color: root.textIconColor
                        display: PlasmaComponents3.AbstractButton.IconOnly
                        icon.name: "view-refresh"
                        text: i18nc("@action:button", "Reload from Disk")
                        enabled: note.status !== MarkdownNote.NoPath
                        onClicked: root.reloadFromDisk()

                        PlasmaComponents3.ToolTip {
                            text: reloadButton.text
                        }
                    }

                    PlasmaComponents3.ToolButton {
                        id: openButton

                        focusPolicy: Qt.TabFocus
                        icon.color: root.textIconColor
                        display: PlasmaComponents3.AbstractButton.IconOnly
                        icon.name: "document-open"
                        text: i18nc("@action:button", "Open Markdown File…")
                        onClicked: root.pickFile()

                        PlasmaComponents3.ToolTip {
                            text: openButton.text
                        }
                    }

                    PlasmaComponents3.ToolButton {
                        id: obsidianButton

                        focusPolicy: Qt.TabFocus
                        icon.color: root.textIconColor
                        display: PlasmaComponents3.AbstractButton.IconOnly
                        visible: note.status === MarkdownNote.Ready
                        icon.name: "emblem-symbolic-link"
                        text: i18nc("@action:button", "Open in Obsidian")
                        onClicked: root.openInObsidian()

                        PlasmaComponents3.ToolTip {
                            text: obsidianButton.text
                        }
                    }

                    PlasmaComponents3.ToolButton {
                        id: pinButton

                        focusPolicy: Qt.TabFocus
                        icon.color: root.textIconColor
                        display: PlasmaComponents3.AbstractButton.IconOnly
                        visible: root.compactInPanel
                        checkable: true
                        checked: Plasmoid.configuration.pinOpen
                        icon.name: "window-pin"
                        text: i18nc("@action:button pin popup for panel widget", "Keep Open")
                        onToggled: Plasmoid.configuration.pinOpen = checked

                        PlasmaComponents3.ToolTip {
                            text: pinButton.text
                        }

                        Binding {
                            target: root
                            property: "hideOnWindowDeactivate"
                            value: !Plasmoid.configuration.pinOpen
                            restoreMode: Binding.RestoreNone
                        }
                    }

                    PlasmaComponents3.ToolButton {
                        id: settingsButton

                        focusPolicy: Qt.TabFocus
                        icon.color: root.textIconColor
                        display: PlasmaComponents3.AbstractButton.IconOnly
                        icon.name: "configure"
                        text: Plasmoid.internalAction("configure").text
                        onClicked: Plasmoid.internalAction("configure").trigger()

                        PlasmaComponents3.ToolTip {
                            text: settingsButton.text
                        }
                    }
                }
            }

            // Drop a .md onto the widget to point it at that file. Matches what
            // X-Plasma-DropMimeTypes in metadata.json advertises. Only drag
            // events go through a DropArea, so this cannot shadow any click.
            DropArea {
                id: noteDropArea

                anchors.fill: parent
                keys: ["text/uri-list", "text/markdown", "text/x-markdown", "text/plain"]

                onEntered: drag => {
                    drag.accepted = drag.hasUrls || drag.hasText;
                }

                onDropped: drop => {
                    const accepted = drop.hasUrls
                        ? root.acceptDroppedPaths(drop.urls)
                        : root.acceptDroppedPaths(root.candidatesFromText(drop.text ?? ""));
                    if (accepted) {
                        drop.acceptProposedAction();
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    visible: noteDropArea.containsDrag
                    color: "transparent"
                    radius: Kirigami.Units.cornerRadius
                    border.width: 2
                    border.color: root.notePalette.accent
                }
            }

            // Holds rawText off-screen purely so "Copy All" has something to
            // put on the clipboard (the rendered Text is not selectable).
            TextEdit {
                id: clipboardHelper

                visible: false
                width: 0
                height: 0
                textFormat: TextEdit.PlainText
                text: note.rawText

                function copyAll() {
                    clipboardHelper.selectAll();
                    clipboardHelper.copy();
                    clipboardHelper.deselect();
                }
            }

            QQC2.Menu {
                id: contextMenu

                popupType: QQC2.Menu.Window

                readonly property bool shortcutsEnabled: contextMenu.visible

                onAboutToShow: root.menuOpen = true
                onClosed: root.menuOpen = false

                function retFocus(f) {
                    f();
                    if (root.editMode) {
                        editor.forceEditorFocus();
                    }
                }

                // ---- EDIT mode ----
                ShortcutMenuItem {
                    _sequence: StandardKey.Undo
                    _enabled: contextMenu.shortcutsEnabled && editor.canUndo
                    _iconName: "edit-undo"
                    _text: i18n("Undo")
                    visible: root.editMode
                    height: visible ? implicitHeight : 0
                    onTriggered: contextMenu.retFocus(() => editor.undo())
                }

                ShortcutMenuItem {
                    _sequence: StandardKey.Redo
                    _enabled: contextMenu.shortcutsEnabled && editor.canRedo
                    _iconName: "edit-redo"
                    _text: i18n("Redo")
                    visible: root.editMode
                    height: visible ? implicitHeight : 0
                    onTriggered: contextMenu.retFocus(() => editor.redo())
                }

                QQC2.MenuSeparator {
                    visible: root.editMode
                    height: visible ? implicitHeight : 0
                }

                ShortcutMenuItem {
                    _sequence: StandardKey.Cut
                    _enabled: contextMenu.shortcutsEnabled && editor.selectedText.length > 0
                    _iconName: "edit-cut"
                    _text: i18n("Cut")
                    visible: root.editMode
                    height: visible ? implicitHeight : 0
                    onTriggered: contextMenu.retFocus(() => editor.cut())
                }

                ShortcutMenuItem {
                    _sequence: StandardKey.Copy
                    _enabled: contextMenu.shortcutsEnabled && editor.selectedText.length > 0
                    _iconName: "edit-copy"
                    _text: i18n("Copy")
                    visible: root.editMode
                    height: visible ? implicitHeight : 0
                    onTriggered: contextMenu.retFocus(() => editor.copy())
                }

                ShortcutMenuItem {
                    _sequence: StandardKey.Paste
                    _enabled: contextMenu.shortcutsEnabled && editor.canPaste
                    _iconName: "edit-paste"
                    _text: i18n("Paste")
                    visible: root.editMode
                    height: visible ? implicitHeight : 0
                    onTriggered: contextMenu.retFocus(() => editor.paste())
                }

                QQC2.MenuSeparator {
                    visible: root.editMode
                    height: visible ? implicitHeight : 0
                }

                ShortcutMenuItem {
                    _sequence: StandardKey.SelectAll
                    _enabled: contextMenu.shortcutsEnabled && editor.length > 0
                    _iconName: "edit-select-all"
                    _text: i18n("Select All")
                    visible: root.editMode
                    height: visible ? implicitHeight : 0
                    onTriggered: contextMenu.retFocus(() => editor.selectAll())
                }

                QQC2.MenuSeparator {
                    visible: root.editMode
                    height: visible ? implicitHeight : 0
                }

                ShortcutMenuItem {
                    _enabled: contextMenu.shortcutsEnabled
                    _iconName: "document-save"
                    _text: i18nc("@action:inmenu", "Done Editing")
                    visible: root.editMode
                    height: visible ? implicitHeight : 0
                    onTriggered: root.commitEdit(true)
                }

                // ---- VIEW mode ----
                ShortcutMenuItem {
                    _enabled: contextMenu.shortcutsEnabled
                        && note.status !== MarkdownNote.NoPath
                        && !root.readOnly
                    _iconName: "document-edit"
                    _text: i18nc("@action:inmenu", "Edit")
                    visible: !root.editMode
                    height: visible ? implicitHeight : 0
                    onTriggered: root.enterEdit(-1)
                }

                ShortcutMenuItem {
                    _enabled: contextMenu.shortcutsEnabled && note.rawText.length > 0
                    _iconName: "edit-copy"
                    _text: i18nc("@action:inmenu", "Copy All")
                    visible: !root.editMode
                    height: visible ? implicitHeight : 0
                    onTriggered: clipboardHelper.copyAll()
                }

                QQC2.MenuSeparator {
                    visible: !root.editMode
                    height: visible ? implicitHeight : 0
                }

                ShortcutMenuItem {
                    _enabled: contextMenu.shortcutsEnabled && note.status !== MarkdownNote.NoPath
                    _iconName: "view-refresh"
                    _text: i18nc("@action:inmenu", "Reload from Disk")
                    visible: !root.editMode
                    height: visible ? implicitHeight : 0
                    onTriggered: root.reloadFromDisk()
                }

                ShortcutMenuItem {
                    _enabled: contextMenu.shortcutsEnabled
                    _iconName: "document-open"
                    _text: i18nc("@action:inmenu", "Open Markdown File…")
                    visible: !root.editMode
                    height: visible ? implicitHeight : 0
                    onTriggered: root.pickFile()
                }

                ShortcutMenuItem {
                    _enabled: contextMenu.shortcutsEnabled && note.status === MarkdownNote.Ready
                    _iconName: "emblem-symbolic-link"
                    _text: i18nc("@action:inmenu", "Open in Obsidian")
                    visible: !root.editMode
                    height: visible ? implicitHeight : 0
                    onTriggered: root.openInObsidian()
                }
            }
        }
    }
}
