/*
    SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
    SPDX-License-Identifier: GPL-2.0-or-later

    Drives the real NoteView.qml / NoteEditor.qml (compiled into this harness
    module, so the unqualified MarkdownNote type resolves exactly as it does in
    the applet) against the torture fixture. No plasmoid, no shell, no display.

    Three kinds of assertion live here:

      1. functional  -- real components, real backend, real synthesised mouse and
                        key events delivered to a real QQuickWindow. This is how
                        the desktop click-out fix and the Ctrl+E focus fix are
                        verified: a plain window with no popup and no focus
                        hand-off is exactly what a Planar containment gives us.
      2. wiring      -- the backend really exposes every member main.qml calls.
      3. source      -- qml/main.qml itself cannot be instantiated without a
                        plasmashell, so the invariants that live in it are
                        asserted against its exact text (handed over by main.cpp),
                        and its pure helpers are extracted and executed.
*/

import QtCore
import QtQuick
import QtQuick.Window

Window {
    id: harness

    width: 800
    height: 900
    visible: true

    /** Set false by any failed assertion below. */
    property bool ok: true
    property string verdict: ""

    // Functional counters, driven by real events.
    property int outsidePressCount: 0
    property int viewClickCount: 0
    property int ctrlECount: 0
    property int escapeCount: 0
    property int editRequestCount: 0
    property int toggleRequestCount: 0
    property int lastToggleLine: -1
    property string lastToggleExpected: ""

    // Ownership probe for the window-level click-out watcher.
    property var probeEditor: null
    property int contentChildrenBaseline: -1

    function check(label, condition) {
        if (!condition) {
            harness.ok = false;
            harness.verdict += "FAILED: " + label + "\n";
        } else {
            harness.verdict += "ok: " + label + "\n";
        }
    }

    /** Body of `function <name>(...)` in @p src, brace-matched. "" when absent. */
    function functionBody(src, name) {
        const sig = src.indexOf("function " + name + "(");
        if (sig < 0) {
            return "";
        }
        const open = src.indexOf("{", sig);
        if (open < 0) {
            return "";
        }
        let depth = 0;
        for (let i = open; i < src.length; ++i) {
            const c = src.charAt(i);
            if (c === "{") {
                depth += 1;
            } else if (c === "}") {
                depth -= 1;
                if (depth === 0) {
                    return src.substring(open + 1, i);
                }
            }
        }
        return "";
    }

    MarkdownNote {
        id: note
        path: torturePath
    }

    // S2 editor round-trip: a scratch note (never the fixture) and a REAL
    // plain-text TextEdit, i.e. exactly what EDIT mode puts the buffer through.
    MarkdownNote {
        id: fidelityNote
    }
    TextEdit {
        id: fidelityEdit
        visible: false
        textFormat: TextEdit.PlainText
    }

    // Mirrors main.qml's viewFocusScope + NoteView pair exactly, including the
    // Keys handler that owns Ctrl+E (enter EDIT) and Esc (dismiss).
    FocusScope {
        id: viewFocusScope

        anchors.fill: parent

        Keys.onPressed: event => {
            if (event.key === Qt.Key_Escape) {
                harness.escapeCount += 1;
                event.accepted = true;
            } else if (event.key === Qt.Key_E && (event.modifiers & Qt.ControlModifier)) {
                harness.ctrlECount += 1;
                event.accepted = true;
            }
        }

        // The real read-only view, fed the real rendered torture document.
        NoteView {
            id: view

            anchors.fill: parent
            focus: true

            note: note
            basePointSize: 10
            fontFamily: "sans-serif"
            readOnly: false

            onEditRequested: {
                harness.viewClickCount += 1;
                harness.editRequestCount += 1;
            }
            onLinkClicked: harness.viewClickCount += 1
            // Recorded only: the harness must never write the torture fixture.
            onTaskToggleRequested: (line, expected) => {
                harness.toggleRequestCount += 1;
                harness.lastToggleLine = line;
                harness.lastToggleExpected = expected;
            }
        }
    }

    // The real editor, fed the real raw torture source. Deliberately smaller
    // than the window so there is somewhere to "click out" to.
    NoteEditor {
        id: editor

        x: 0
        y: 0
        width: 300
        height: 300
        visible: false
        text: note.editorText
        font.family: "monospace"
        font.pointSize: 10
        contentPadding: view.metrics.containerPadding

        onOutsidePressed: harness.outsidePressCount += 1
    }

    Component.onCompleted: {
        harness.check("note is Ready", note.status === MarkdownNote.Ready);
        harness.check("fileName resolved", note.fileName.length > 0);
        harness.check("rawText non-empty", note.rawText.length > 100);
        harness.check("renderedText non-empty", note.renderedText.length > 100);

        // The view really received the block model.
        // A QVariantList property yields a fresh JS array on every read, so
        // identity (===) can never hold; compare the full content instead.
        harness.check("view.blocks is note.blocks (deep-equal) && note.blocks.length > 20",
                      note.blocks.length > 20 && view.blocks.length === note.blocks.length
                      && JSON.stringify(view.blocks) === JSON.stringify(note.blocks));
        harness.check("editor.text == editorText", editor.text === note.editorText);
        harness.check("editor.length matches", editor.length === note.editorText.length);

        // Frontmatter is hidden from the render but still in the source.
        harness.check("frontmatter hidden from render", note.renderedText.indexOf("not_a_task:") === -1);
        harness.check("frontmatter still in source", note.rawText.indexOf("not_a_task:") > -1);

        // A real task became a clickable toggle link; a fenced one did not.
        harness.check("real task is a toggle link", note.renderedText.indexOf("obsnote:toggle/128)") > -1);
        harness.check("fenced task is NOT a toggle link", note.renderedText.indexOf("obsnote:toggle/196)") === -1);
        harness.check("indented-code task is NOT a toggle link", note.renderedText.indexOf("obsnote:toggle/225)") === -1);

        // ... and the block model the view draws agrees with the toggle links.
        const task128 = harness.findBlock(note.blocks, b => b.kind === "task" && b.sourceLine === 128);
        harness.check("block model: line 128 is a task block", task128 !== null);
        harness.check("block model: line 128 task is toggleable", task128 !== null && task128.toggleable === true);
        harness.check("block model: line 128 carries the exact raw line",
                      task128 !== null && task128.expectedLineText === note.rawText.split("\n")[128]);
        harness.check("block model: no task block for fenced line 196",
                      harness.findBlock(note.blocks, b => b.kind === "task" && b.sourceLine === 196) === null);
        harness.check("block model: no task block for indented-code line 225",
                      harness.findBlock(note.blocks, b => b.kind === "task" && b.sourceLine === 225) === null);

        // Link round-trip through the C++ parsers, as main.qml's handleLink does.
        harness.check("toggleLineForLink round-trips", note.toggleLineForLink("obsnote:toggle/128") === 128);
        harness.check("wikilinkTargetForLink round-trips", note.wikilinkTargetForLink("obsnote:wiki/Some%20Note") === "Some Note");
        harness.check("non-link yields -1", note.toggleLineForLink("https://kde.org") === -1);

        // Checkbox glyphs made it into the rendered document.
        harness.check("unchecked glyph present", note.renderedText.indexOf("☐") > -1);
        harness.check("checked glyph present", note.renderedText.indexOf("☑") > -1);

        // Exercise the editor's forwarders (these touch the real TextArea).
        editor.selectAll();
        harness.check("selectAll selected the buffer", editor.selectedText.length === note.editorText.length);
        editor.deselect();
        harness.check("deselect cleared the selection", editor.selectedText.length === 0);

        // Exercise NoteView's scroll helpers.
        view.restoreScroll(0);
        harness.check("saveScroll returns a number", typeof view.saveScroll() === "number");

        // obsidian:// URL building.
        harness.check("obsidianUrl is an obsidian:// url", note.obsidianUrl().toString().indexOf("obsidian://open?path=") === 0);

        harness.checkBackendWiring();
        harness.checkEditorByteFidelity();
        harness.checkSourceInvariants();
        harness.checkFileUrlEncoding();
        harness.checkDropAdvertisement();
    }

    /** Depth-first search of a block list (and callout/blockquote children). */
    function findBlock(blocks, pred) {
        const bs = blocks || [];
        for (let i = 0; i < bs.length; ++i) {
            if (pred(bs[i])) {
                return bs[i];
            }
            const inner = harness.findBlock(bs[i].children, pred);
            if (inner !== null) {
                return inner;
            }
        }
        return null;
    }

    /** Depth-first search of the live item tree under @p item. */
    function findItem(item, pred) {
        const kids = item && item.children ? item.children : [];
        for (let i = 0; i < kids.length; ++i) {
            if (pred(kids[i])) {
                return kids[i];
            }
            const inner = harness.findItem(kids[i], pred);
            if (inner !== null) {
                return inner;
            }
        }
        return null;
    }

    //
    // ---- 2. wiring: every backend member main.qml calls must exist ---------
    //

    /**
     * REGRESSION: entering and leaving EDIT mode without typing must write NOTHING.
     * QQuickTextEdit hands back NBSP as ' ' and U+FDD0/U+FDD1 as "\n"; MarkdownNote
     * used to see that as an edit and silently rewrote the note. This runs the
     * real TextEdit, so a Qt change to that normalisation set fails here too.
     */
    function checkEditorByteFidelity() {
        const dir = StandardPaths.writableLocation(StandardPaths.TempLocation).toString().replace(/^file:\/\//, "");
        const path = dir + "/obsnote-harness-fidelity.md"; // overwritten every run
        const nb = "\u00a0";
        const original = "\ufeff---\r\ntitle: a" + nb + "b\r\n---\r\nPrice:" + nb + "100\r\n- [ ] task" + nb + "one\r\n"
            + "x\ufdd0y\ufdd1z\u2028w\r\nend" + nb;
        harness.check("fidelity: scratch note written", harnessHelper.writeTextFile(path, original));
        fidelityNote.path = path;
        harness.check("fidelity: scratch note Ready", fidelityNote.status === MarkdownNote.Ready);
        const raw = fidelityNote.rawText;

        fidelityEdit.text = fidelityNote.editorText; // main.qml beginEdit()
        harness.check("fidelity: TextEdit holds exactly editorText (no phantom unsaved edits)",
                      fidelityEdit.text === fidelityNote.editorText);
        let saves = 0;
        const onSaved = () => { saves += 1; };
        fidelityNote.saved.connect(onSaved);
        harness.check("fidelity: untouched commit succeeds", fidelityNote.saveBuffer(fidelityEdit.text)); // commitEdit()
        harness.check("fidelity: untouched commit wrote nothing", saves === 0 && fidelityNote.rawText === raw);

        fidelityEdit.text = fidelityEdit.text.replace("one", "ONE");
        harness.check("fidelity: one-word edit saves", fidelityNote.saveBuffer(fidelityEdit.text));
        harness.check("fidelity: one-word edit changed only that word",
                      saves === 1 && fidelityNote.rawText === raw.replace("one", "ONE"));
        fidelityNote.saved.disconnect(onSaved);
        fidelityNote.path = "";
    }

    function checkBackendWiring() {
        harness.check("backend has saveBuffer()", typeof note.saveBuffer === "function");
        harness.check("backend has reloadFromDisk()", typeof note.reloadFromDisk === "function");
        harness.check("backend has keepMine()", typeof note.keepMine === "function");
        harness.check("backend has toggleTask()", typeof note.toggleTask === "function");
        harness.check("backend has lineTextAt()", typeof note.lineTextAt === "function");
        harness.check("backend has localPathFromUrl()", typeof note.localPathFromUrl === "function");
        harness.check("backend exposes readOnlyReason", note.readOnlyReason !== undefined);
        harness.check("backend exposes editorText", note.editorText !== undefined);
        harness.check("backend exposes externalChangePending", note.externalChangePending !== undefined);

        // The torture fixture is writable UTF-8, so nothing is read-only.
        harness.check("readOnlyReason empty for a good file", note.readOnlyReason === "");
        // S2: what the QQuickTextEdit holds never contains a CR.
        harness.check("editorText is LF-only", note.editorText.indexOf("\r") === -1);

        // S6: the expected-line-text argument main.qml passes really is the line.
        const expected = note.lineTextAt(128);
        harness.check("lineTextAt(128) is a task line", expected.indexOf("[") > -1);
        harness.check("lineTextAt matches rawText line 128", expected === note.rawText.split("\n")[128].replace(/\r$/, ""));

        // S6: a stale/lying expected line must abort the toggle, writing nothing.
        const before = note.rawText;
        harness.check("toggleTask refuses a mismatched expected line",
                      note.toggleTask(128, "- [ ] this is not what is on that line") === false);
        harness.check("refused toggle wrote nothing", note.rawText === before);
        harness.check("toggleTask refuses a non-task line",
                      note.toggleTask(0, note.lineTextAt(0)) === false);
        harness.check("refused non-task toggle wrote nothing", note.rawText === before);
    }

    //
    // ---- 3. source invariants of qml/main.qml ------------------------------
    //

    function checkSourceInvariants() {
        const src = mainQmlSource;
        harness.check("main.qml source was readable", src.length > 1000);

        // S5: the reload paths must not be able to save first.
        const doReload = harness.functionBody(src, "doReload");
        harness.check("doReload() exists", doReload.length > 0);
        harness.check("doReload() never calls commitEdit", doReload.indexOf("commitEdit") === -1);
        harness.check("doReload() never calls save", /\.save\w*\s*\(/.test(doReload) === false);
        harness.check("doReload() raises the suppressCommit latch", doReload.indexOf("suppressCommit = true") > -1);
        harness.check("doReload() calls reloadFromDisk on the backend", doReload.indexOf("note.reloadFromDisk()") > -1);

        const reload = harness.functionBody(src, "reloadFromDisk");
        harness.check("reloadFromDisk() exists", reload.length > 0);
        harness.check("reloadFromDisk() never calls commitEdit", reload.indexOf("commitEdit") === -1);

        const conflictReload = harness.functionBody(src, "resolveConflictReload");
        harness.check("resolveConflictReload() exists", conflictReload.length > 0);
        harness.check("resolveConflictReload() never calls commitEdit", conflictReload.indexOf("commitEdit") === -1);
        harness.check("resolveConflictReload() never calls save", /\.save\w*\s*\(/.test(conflictReload) === false);

        // The focus-out auto-commit must be neutralised while a conflict is up.
        const commit = harness.functionBody(src, "commitEdit");
        harness.check("commitEdit() exists", commit.length > 0);
        harness.check("commitEdit() bails out on commitSuppressed", commit.indexOf("commitSuppressed") > -1);
        harness.check("commitEdit() checks the guard before writing",
                      commit.indexOf("commitSuppressed") < commit.indexOf("saveBuffer"));
        harness.check("commitSuppressed covers externalChangePending",
                      /commitSuppressed[\s\S]{0,300}note\.externalChangePending/.test(src));
        harness.check("commitSuppressed covers the reload prompt",
                      /commitSuppressed[\s\S]{0,300}reloadPending/.test(src));

        // S2: the editor buffer round-trips through the C++ EOL restoration.
        harness.check("beginEdit primes from editorText", src.indexOf("editor.text = note.editorText") > -1);
        harness.check("commitEdit writes through saveBuffer", commit.indexOf("note.saveBuffer(") > -1);
        harness.check("main.qml never calls the raw save()", /note\.save\s*\(/.test(src) === false);

        // S6: the toggle always carries the expected line text.
        harness.check("toggles go through toggleTask(line, expectedText)",
                      src.indexOf("note.toggleTask(toggleLine, note.lineTextAt(toggleLine))") > -1);
        // ... including the checkbox route, which carries the block's own raw line.
        const toggleFn = harness.functionBody(src, "toggleTask");
        harness.check("main.qml has toggleTask(line, expected)", toggleFn.length > 0);
        harness.check("toggleTask() refuses a read-only note", toggleFn.indexOf("root.readOnly") > -1);
        harness.check("toggleTask() hands the expected line to the backend",
                      /\.toggleTask\(\s*line\s*,\s*expected\s*\)/.test(toggleFn));
        harness.check("NoteView checkbox clicks are wired to toggleTask",
                      /onTaskToggleRequested:[^\n]*toggleTask\(/.test(src));

        // Opaque widget, no scrollbars, wrap instead of horizontal overflow.
        harness.check("main.qml draws no translucent Plasma background (NoBackground)",
                      src.indexOf("PlasmaCore.Types.NoBackground") > -1);
        harness.check("NoteView uses no ScrollView", noteViewSource.indexOf("ScrollView") === -1);
        harness.check("NoteView uses no ScrollBar", noteViewSource.indexOf("ScrollBar") === -1);
        harness.check("NoteView pins contentWidth to the view width",
                      noteViewSource.indexOf("contentWidth: width") > -1
                      || noteViewSource.indexOf("contentWidth: flick.width") > -1);
        harness.check("NoteView stops at bounds", noteViewSource.indexOf("StopAtBounds") > -1);
        harness.check("NoteEditor vertical scrollbar AlwaysOff",
                      /ScrollBar\.vertical\.policy:\s*QQC2\.ScrollBar\.AlwaysOff/.test(noteEditorSource));
        harness.check("NoteEditor horizontal scrollbar AlwaysOff",
                      /ScrollBar\.horizontal\.policy:\s*QQC2\.ScrollBar\.AlwaysOff/.test(noteEditorSource));
        harness.check("no bare toggleTaskAtLine call left", src.indexOf("toggleTaskAtLine") === -1);

        // S2: read-only files may be neither edited nor toggled.
        harness.check("readOnlyReason is wired up", src.indexOf("note.readOnlyReason") > -1);
        harness.check("enterEdit refuses a read-only note",
                      harness.functionBody(src, "enterEdit").indexOf("root.readOnly") > -1);
        harness.check("handleLink refuses a read-only toggle",
                      harness.functionBody(src, "handleLink").indexOf("root.readOnly") > -1);

        // S3: the conflict signal is handled.
        harness.check("conflictDetected() is handled", src.indexOf("onConflictDetected") > -1);
        harness.check("keepMine() is wired to the banner", src.indexOf("note.keepMine()") > -1);

        // The desktop click-out mechanism is actually connected.
        harness.check("editor's outside-press watch is armed by EDIT mode",
                      src.indexOf("watchOutsidePress: root.editMode") > -1);
        harness.check("outside-press commits and leaves EDIT",
                      src.indexOf("onOutsidePressed: root.commitEdit(true)") > -1);
        harness.check("outside bounds are the whole widget, not just the editor",
                      src.indexOf("outsideBoundsItem: fullRep") > -1);
        harness.check("leaving the host window also commits",
                      src.indexOf("onHostWindowActiveChanged") > -1);
        harness.check("a menu opening cannot fire the window-deactivation commit",
                      src.indexOf("!root.menuOpen") > -1
                      && src.indexOf("onAboutToShow: root.menuOpen = true") > -1);

        // Ctrl+E / Esc need the view to be focusable.
        harness.check("NoteView takes focus on press",
                      noteViewSource.indexOf("viewRoot.forceActiveFocus()") > -1);
        harness.check("main.qml can focus the view back", src.indexOf("function focusView()") > -1);

        // The click-out watch must keep its passive grab, or it eats every click.
        harness.check("the click-out probe is a passive PointHandler, never a TapHandler",
                      noteEditorSource.indexOf("PointHandler {") > -1
                      && noteEditorSource.indexOf("TapHandler {") === -1);
    }

    //
    // ---- the file:// URL builder, executed straight out of main.qml --------
    //

    function checkFileUrlEncoding() {
        const body = harness.functionBody(mainQmlSource, "fileUrlForPath");
        harness.check("fileUrlForPath() exists in main.qml", body.length > 0);
        if (body.length === 0) {
            return;
        }
        const fileUrlForPath = eval("(function(path) {" + body + "})");

        harness.check("fileUrlForPath: plain path",
                      fileUrlForPath("/home/user/Vault/Note.md") === "file:///home/user/Vault/Note.md");
        harness.check("fileUrlForPath: '#' is percent-encoded",
                      fileUrlForPath("/home/user/My#Vault") === "file:///home/user/My%23Vault");
        harness.check("fileUrlForPath: '?' is percent-encoded",
                      fileUrlForPath("/home/user/what?now") === "file:///home/user/what%3Fnow");
        harness.check("fileUrlForPath: spaces are percent-encoded",
                      fileUrlForPath("/home/user/My Notes") === "file:///home/user/My%20Notes");
        harness.check("fileUrlForPath: non-ASCII is percent-encoded",
                      fileUrlForPath("/home/user/Beležke") === "file:///home/user/Bele%C5%BEke");
        harness.check("fileUrlForPath: separators survive",
                      fileUrlForPath("/a/b/c").split("/").length === 6);
        harness.check("fileUrlForPath: empty stays empty", fileUrlForPath("") === "");

        // The bug this replaced: naive concatenation truncates at '#'.
        harness.check("fileUrlForPath is not naive concatenation",
                      fileUrlForPath("/home/user/My#Vault") !== "file:///home/user/My#Vault");
    }

    //
    // ---- metadata.json advertises drops, so drops must be implemented ------
    //

    function checkDropAdvertisement() {
        const meta = JSON.parse(metadataJsonSource);
        const mimes = meta["X-Plasma-DropMimeTypes"];
        harness.check("metadata.json advertises drop mime types",
                      Array.isArray(mimes) && mimes.length > 0);
        harness.check("metadata.json advertises text/markdown", mimes.indexOf("text/markdown") > -1);

        // ... and the advertisement must be honoured on both routes.
        harness.check("main.qml handles containment drops (onExternalData)",
                      mainQmlSource.indexOf("onExternalData:") > -1);
        harness.check("main.qml has a DropArea", mainQmlSource.indexOf("DropArea {") > -1);
        harness.check("DropArea accepts text/uri-list",
                      /keys:\s*\[[^\]]*"text\/uri-list"/.test(mainQmlSource));
        for (let i = 0; i < mimes.length; ++i) {
            harness.check("DropArea accepts advertised " + mimes[i],
                          mainQmlSource.indexOf("\"" + mimes[i] + "\"") > -1);
        }

        // A drop rebinds the note path; it never writes into the user's file.
        const accept = harness.functionBody(mainQmlSource, "acceptDroppedPaths");
        harness.check("acceptDroppedPaths() exists", accept.length > 0);
        harness.check("a drop rebinds notePath", accept.indexOf("Plasmoid.configuration.notePath = path") > -1);
        harness.check("a drop never writes the note", /\.save\w*\s*\(/.test(accept) === false);

        const droppable = harness.functionBody(mainQmlSource, "isDroppableFile");
        const suffixMatch = /droppableSuffixes:\s*(\[[^\]]*\])/.exec(mainQmlSource);
        harness.check("isDroppableFile() exists", droppable.length > 0);
        harness.check("droppableSuffixes is declared", suffixMatch !== null);
        if (droppable.length === 0 || suffixMatch === null) {
            return;
        }
        const suffixes = eval("(" + suffixMatch[1] + ")");
        const isDroppableFile = eval("(function(path) { const root = { droppableSuffixes: "
                                     + JSON.stringify(suffixes) + " };" + droppable + "})");
        harness.check("isDroppableFile: .md", isDroppableFile("/v/Note.md") === true);
        harness.check("isDroppableFile: .MARKDOWN (case-insensitive)", isDroppableFile("/v/Note.MARKDOWN") === true);
        harness.check("isDroppableFile: .png rejected", isDroppableFile("/v/Note.png") === false);
        harness.check("isDroppableFile: extensionless rejected", isDroppableFile("/v/Note") === false);
    }

    //
    // ---- 1. functional: real events into a real window ---------------------
    //
    // Deferred so the window is exposed and everything has a geometry.

    Timer {
        interval: 400
        running: true
        onTriggered: harness.runEventChecks()
    }

    function runEventChecks() {
        harness.check("harness helper is available", typeof harnessHelper.pressAt === "function");
        harness.check("editor has a real geometry", editor.width === 300 && editor.height === 300);

        //
        // Ctrl+E / Esc: unreachable until something gives the view active focus.
        //
        harness.check("nothing has focus before the first click", view.activeFocus === false);
        harnessHelper.pressAt(harness, 40, 500);
        harness.check("pressing the note focuses the view", view.activeFocus === true);
        harness.check("the focus scope around the view is active", viewFocusScope.activeFocus === true);

        harnessHelper.keyPress(harness, Qt.Key_E, Qt.ControlModifier, "e");
        harness.check("Ctrl+E reaches the view's Keys handler", harness.ctrlECount === 1);
        harnessHelper.keyPress(harness, Qt.Key_Escape, Qt.NoModifier, "");
        harness.check("Esc reaches the view's Keys handler", harness.escapeCount === 1);

        //
        // The desktop click-out. No popup, no window deactivation, no focus
        // hand-off -- just a press somewhere else in the same window, which is
        // all a Planar containment ever gives us.
        //
        harness.check("click-out watch is disarmed by default", editor.watchOutsidePress === false);
        harnessHelper.pressAt(harness, 700, 800);
        harness.check("disarmed watch stays quiet", harness.outsidePressCount === 0);

        editor.watchOutsidePress = true;

        const clicksBefore = harness.viewClickCount;
        harnessHelper.pressAt(harness, 700, 800); // outside the editor's 300x300
        harness.check("press outside the editor fires outsidePressed", harness.outsidePressCount === 1);
        harness.check("the click-out watch does NOT swallow the press",
                      harness.viewClickCount === clicksBefore + 1);

        harnessHelper.pressAt(harness, 150, 150); // inside the editor
        harness.check("press inside the editor does not fire outsidePressed",
                      harness.outsidePressCount === 1);

        harnessHelper.pressAt(harness, 299, 299); // last pixel inside
        harness.check("press on the editor's edge counts as inside",
                      harness.outsidePressCount === 1);

        harnessHelper.pressAt(harness, 301, 150); // just past the right edge
        harness.check("press one pixel outside fires outsidePressed",
                      harness.outsidePressCount === 2);

        // The bounds item is configurable, which is what keeps the toolbar and
        // the conflict banner from counting as "outside" in the applet.
        editor.outsideBoundsItem = viewFocusScope; // now the whole window is "inside"
        harnessHelper.pressAt(harness, 700, 800);
        harness.check("a wider outsideBoundsItem makes that press inside",
                      harness.outsidePressCount === 2);
        editor.outsideBoundsItem = editor;

        editor.watchOutsidePress = false;
        harnessHelper.pressAt(harness, 700, 800);
        harness.check("disarming the watch stops it again", harness.outsidePressCount === 2);

        harness.runViewChecks();
        harness.startOwnershipCheck();
    }

    //
    // ---- the new block view: real checkbox, real empty space, real geometry --
    //
    function runViewChecks() {
        const flick = harness.findItem(view, o => ("flickableDirection" in o) && ("contentY" in o));
        harness.check("NoteView has a Flickable", flick !== null);
        if (flick === null) {
            return;
        }
        harness.check("runtime: horizontal flicking impossible (contentWidth == width)",
                      Math.abs(flick.contentWidth - flick.width) < 0.5);
        harness.check("runtime: no ScrollBar item anywhere in the view",
                      harness.findItem(view, o => harnessHelper.className(o).indexOf("ScrollBar") > -1) === null);
        flick.contentY = 0;

        // The first block top equals the editor's first text line top: both 2em.
        const pad = view.metrics.containerPadding;
        const firstDelegate = harness.findItem(view, o => ("modelData" in o) && ("availableWidth" in o)
                                               && o.modelData && typeof o.modelData.kind === "string");
        harness.check("view has block delegates", firstDelegate !== null);
        if (firstDelegate !== null) {
            const loaded = harness.findItem(firstDelegate, o => ("block" in o) && ("availableWidth" in o));
            const top = (loaded !== null ? loaded : firstDelegate).mapToItem(view, 0, 0).y;
            harness.check("first block top is 2em (" + top.toFixed(2) + " vs " + pad.toFixed(2) + ")",
                          Math.abs(top - pad) <= 0.5);
            const area = harness.findItem(editor, o => ("cursorRectangle" in o) && ("topPadding" in o)
                                          && typeof o.positionToRectangle === "function");
            harness.check("editor has a TextArea", area !== null);
            if (area !== null) {
                // Setting the text scrolled the editor to the cursor; measure from the top.
                const editorFlick = harness.findItem(editor, o => ("flickableDirection" in o) && ("contentY" in o));
                if (editorFlick !== null) {
                    editorFlick.contentY = -editorFlick.topMargin; // the 2em is a Flickable margin
                }
                const editorTop = area.mapToItem(editor, 0, area.positionToRectangle(0).y).y;
                harness.check("editor text top is 2em (" + editorTop.toFixed(2) + ")", Math.abs(editorTop - pad) <= 0.5);
                harness.check("view first block top == editor text top", Math.abs(top - editorTop) <= 0.5);
            }
        }

        // Click the REAL checkbox drawn for source line 128.
        const del128 = harness.findItem(view, o => ("modelData" in o) && ("availableWidth" in o) && o.modelData
                                        && o.modelData.kind === "task" && o.modelData.sourceLine === 128);
        const box = del128 !== null ? harness.findItem(del128, o => o.isTaskCheckbox === true) : null;
        harness.check("line 128 has a TaskCheckbox item", box !== null);
        if (box !== null) {
            harness.check("line 128 checkbox is interactive", box.interactive === true);
            let c = box.mapToItem(view, box.width / 2, box.height / 2);
            flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, flick.contentY + c.y - flick.height / 2));
            c = box.mapToItem(harness.contentItem, box.width / 2, box.height / 2);
            const before = note.rawText;
            const toggles = harness.toggleRequestCount;
            const edits = harness.editRequestCount;
            harnessHelper.pressAt(harness, c.x, c.y);
            harness.check("clicking the checkbox emits taskToggleRequested once", harness.toggleRequestCount === toggles + 1);
            harness.check("taskToggleRequested carries line 128", harness.lastToggleLine === 128);
            harness.check("taskToggleRequested carries the exact raw line",
                          harness.lastToggleExpected === note.rawText.split("\n")[128]);
            harness.check("a checkbox click is not an edit request", harness.editRequestCount === edits);
            harness.check("the view itself never writes the file", note.rawText === before);
        }

        // Click empty space (the 2em top padding) -> edit request.
        flick.contentY = 0;
        const edits = harness.editRequestCount;
        const toggles = harness.toggleRequestCount;
        const empty = view.mapToItem(harness.contentItem, view.width - pad / 2, pad / 2);
        harnessHelper.pressAt(harness, empty.x, empty.y);
        harness.check("clicking empty space emits editRequested", harness.editRequestCount === edits + 1);
        harness.check("clicking empty space toggles nothing", harness.toggleRequestCount === toggles);
        harness.check("clicking empty space focuses the view", view.activeFocus === true);
    }

    //
    // The click-out watcher is reparented onto the WINDOW's contentItem, i.e.
    // into plasmashell's own scene. It must therefore die with the editor that
    // created it; a leaked one would keep probing every press on the desktop
    // forever, for every note widget the user ever removed.
    //
    function startOwnershipCheck() {
        harness.contentChildrenBaseline = harness.contentItem.children.length;
        const component = Qt.createComponent("NoteEditor.qml");
        harness.check("NoteEditor component loads standalone", component.status === Component.Ready);
        if (component.status !== Component.Ready) {
            return;
        }
        harness.probeEditor = component.createObject(harness.contentItem, { width: 10, height: 10 });
        harness.check("a second NoteEditor adds itself AND its window-level watcher",
                      harness.contentItem.children.length === harness.contentChildrenBaseline + 2);
        harness.probeEditor.destroy();
        ownershipTimer.restart();
    }

    Timer {
        id: ownershipTimer
        interval: 300
        onTriggered: harness.check("destroying an editor takes its window-level watcher with it",
                                   harness.contentItem.children.length === harness.contentChildrenBaseline);
    }
}
