# Obsidian Note

A modified version of the KDE Plasma **Sticky Note** widget
(`org.kde.plasma.notes`, from `kdeplasma-addons`) that reads and writes **real
Markdown files**. Point it at a note in your Obsidian vault and the checklists
you tick are saved straight into that `.md` file.

Applet id: `io.github.mrajster.obsidiannote` · Plasma 6 · GPL-2.0-or-later

## Screenshots

| | |
| --- | --- |
| ![View mode with a rendered checklist](docs/screenshots/01-view-checklist.png) | ![Edit mode showing the raw Markdown source](docs/screenshots/02-edit-raw-markdown.png) |
| **View mode.** The note is rendered; the checkboxes are clickable and write straight to the file. | **Edit mode.** One click drops you into the raw Markdown source, in a monospace editor. |
| ![The widget configuration page for choosing the Markdown file](docs/screenshots/03-config-file-page.png) | ![The widget sitting on the Plasma desktop](docs/screenshots/04-on-desktop.png) |
| **Configuration.** Bind the widget to any `.md` file; autosave delay and the file-name header live here too. | **Sticky-note sizing.** The same note at desktop-widget size; it behaves like the stock sticky note, except the note is a file in your vault. |

## Features

- **Markdown rendering in view mode.** The note is rendered rather than shown as
  source. Rendering uses Qt's `Text.MarkdownText` (CommonMark plus GFM tables),
  so Obsidian-only inline syntax such as `==highlight==` or `> [!note]` callouts
  is displayed as literal text rather than styled.
- **Click to edit, click out to save.** A left click on non-link text switches to
  a plain-text editor holding the exact file source; clicking anywhere outside
  the widget, or pressing `Esc`, saves and re-renders. `Ctrl+S` saves without
  leaving edit mode. An optional autosave timer (default 10 s, `Off` allowed)
  saves while you type.
- **Live checkbox toggling that rewrites exactly one line.** Clicking a rendered
  `- [ ]` / `- [x]` flips a single character in a single line; the rest of the
  file is not re-serialised. The toggle re-reads the file first, checks the line
  still says what the render thought it said, and refuses otherwise.
- **Opens any `.md` file anywhere.** Choose it in the config page, use
  *Open Markdown File…* from the toolbar or context menu, or drag a
  `text/markdown` / `text/plain` file onto the widget. The file does not have to
  be in a vault — the config page only warns when the name does not end in `.md`,
  because Obsidian will not index it.
- **Obsidian-vault-safe writing.** The widget never rewrites the file from a
  parsed representation: it never calls `QTextDocument::toMarkdown()` and only
  ever writes back the editor buffer or one toggled checkbox character. YAML
  frontmatter, `[[wikilinks]]`, `#tags`, callouts, embeds and code fences
  therefore survive byte-for-byte even though they are not part of the render
  model.
- **Preserves line endings, BOM and trailing newline.** The loaded file's line
  endings (LF/CRLF/CR, including mixed files, where unchanged lines keep their
  original ending) and a leading UTF-8 BOM are restored on every write, and a
  missing trailing newline is neither invented nor removed.
- **Detects external edits and reloads.** A `KDirWatch` on the file picks up
  writes from Obsidian or a sync client and reloads the view; a reload is
  deferred, not silently applied, while you are editing.
- **Refuses to overwrite a file that changed underneath.** Every load and write
  records size, mtime and a SHA-256 of the bytes. A write whose fingerprint no
  longer matches is aborted, your buffer is kept, and a conflict banner offers
  *Reload* or *Keep My Version*. Autosave stops writing entirely until you
  choose. A rewrite whose bytes are identical to what is on disk is skipped, so
  it never bumps the mtime and never triggers a pointless sync event.
- **Atomic writes.** `QSaveFile` with the direct-write fallback explicitly
  disabled, so a failed or interrupted write can never truncate the note. (A
  test asserts the fallback is not re-enabled in the source.)
- **Read-only mode for files that are not valid UTF-8.** Rewriting such a file
  would replace the undecodable bytes with U+FFFD, so the widget refuses every
  write and says why instead.
- **Wikilinks can hand off to Obsidian.** `[[Note]]`, `[[Note|Alias]]` and
  `![[Embed]]` render as clickable links that open
  `obsidian://open?file=…`; *Open in Obsidian* opens the bound file itself. This
  needs Obsidian installed and registered for the `obsidian://` scheme —
  otherwise nothing happens.

## Requirements

Plasma 6, Qt 6.7+, KDE Frameworks 6.0+, `extra-cmake-modules`, CMake 3.20+ and a
C++20 compiler.

**Arch Linux** (package names verified with `pacman -Qo` against the files CMake
actually looks for):

```bash
sudo pacman -S --needed base-devel cmake extra-cmake-modules \
    qt6-base qt6-declarative kcoreaddons ki18n kconfig libplasma
```

`plasma-sdk` is optional and only needed for `scripts/run-viewer.sh`
(`plasmoidviewer`).

**Debian / Ubuntu** (packages present in Debian trixie and newer):

```bash
sudo apt install build-essential cmake extra-cmake-modules \
    qt6-base-dev qt6-declarative-dev libkf6coreaddons-dev libkf6i18n-dev \
    libkf6config-dev libplasma-dev
```

**Fedora** (packages present in Fedora 43 and newer):

```bash
sudo dnf install gcc-c++ cmake extra-cmake-modules \
    qt6-qtbase-devel qt6-qtdeclarative-devel kf6-kcoreaddons-devel \
    kf6-ki18n-devel kf6-kconfig-devel libplasma-devel
```

## Installation

### TL;DR

```bash
scripts/install-user.sh          # rootless, survives logout/reboot (recommended)
# or
scripts/build.sh --system        # into Qt's own plugin dir, needs sudo
```

Then restart the shell once and add the widget:

```bash
scripts/build.sh --restart       # or: systemctl --user restart plasma-plasmashell.service
```

### Why there are two routes

A Plasma applet is an ordinary Qt plugin. `plasmashell` resolves it with
`KPluginMetaData::findPluginById("plasma/applets", …)`, which searches only
`QCoreApplication::libraryPaths()` — that is `$QT_PLUGIN_PATH` plus Qt's own
build-time plugin directory:

```console
$ /usr/lib/qt6/bin/qtpaths --query QT_INSTALL_PLUGINS
/usr/lib/qt6/plugins
```

(The binary is called `qtpaths6` on some distributions and plain `qtpaths` on
others; the scripts below try both, plus the `/usr/lib/qt6/bin` copies.)

So the applet must either be installed *inside* that directory, or its install
directory must be added to `QT_PLUGIN_PATH` for the whole session,
persistently. Exporting `QT_PLUGIN_PATH` from `~/.bashrc` or `~/.zshrc` is not
enough: `plasmashell` is started by `plasma-plasmashell.service`, which never
reads a shell rc file.

### Route A — user-local, no root (default)

```bash
scripts/install-user.sh
```

A thin wrapper around `scripts/build.sh --user`. It:

1. installs the plugin into
   `~/.local/lib/plugins/plasma/applets/io.github.mrajster.obsidiannote.so`;
2. installs a systemd environment drop-in at
   `~/.config/environment.d/60-obsidiannote-qt-plugin-path.conf`, generated by
   CMake from `scripts/environment.d.conf.in`:

   ```ini
   QT_PLUGIN_PATH=${HOME}/.local/lib/plugins${QT_PLUGIN_PATH:+:${QT_PLUGIN_PATH}}
   ```

   `systemd-environment-d-generator` runs at every login, so **this is what makes
   the widget survive a re-login**. The `${VAR:+…}` form is expanded by systemd
   itself, so an existing `QT_PLUGIN_PATH` is appended to, never clobbered;
3. mirrors the same value into the already-running session with
   `systemctl --user set-environment`, so you do not have to log out now;
4. runs `scripts/verify-install.sh`.

Re-running it just rewrites the same drop-in — it is idempotent. Every
`scripts/build.sh` flag is forwarded, e.g. `scripts/install-user.sh --clean --no-tests`.

Useful `scripts/build.sh` flags: `--user` / `--system`, `--clean`, `--no-tests`,
`--no-install`, `--no-dropin`, `--no-verify`, `--debug`, `--restart`,
`--prefix DIR`, `--uninstall`, `--help`.

### Route B — system-wide, needs sudo

```bash
scripts/build.sh --system
```

Configures with `-DCMAKE_INSTALL_PREFIX=/usr -DKDE_INSTALL_USE_QT_SYS_PATHS=ON`
and installs to
`/usr/lib/qt6/plugins/plasma/applets/io.github.mrajster.obsidiannote.so`. Qt
searches that directory by default, so no `QT_PLUGIN_PATH` change is made and no
drop-in is installed. This is the route for distro packages; under `DESTDIR` the
drop-in is skipped automatically so a packaging run never writes into `$HOME`.

### Verifying the install

`scripts/verify-install.sh` proves that a *fresh login* would find the applet. It
compiles `scripts/qtplugin-probe.cpp` (which makes the same
`KPluginMetaData::findPluginById()` call `plasmashell` makes) and runs it inside
`env -i` with only the variables a systemd user manager has at login, after
importing the output of systemd's own `30-systemd-environment-d-generator`.
Nothing is inherited from your shell.

```console
$ scripts/verify-install.sh
==> Control A -- pristine login env, environment.d NOT applied
  QT_PLUGIN_PATH = <unset>
  libraryPaths() = /usr/lib/qt6/plugins:/tmp/obsidiannote-verify-XXXXXX
  NOT-FOUND io.github.mrajster.obsidiannote
    PASS not found without environment.d, as expected

==> Test 1 -- pristine login env + systemd environment.d generator
  QT_PLUGIN_PATH = ~/.local/lib/plugins
  libraryPaths() = ~/.local/lib/plugins:/usr/lib/qt6/plugins:/tmp/obsidiannote-verify-XXXXXX
  FOUND     io.github.mrajster.obsidiannote -> ~/.local/lib/plugins/plasma/applets/io.github.mrajster.obsidiannote.so  (name: "Obsidian Note")
    PASS applet FOUND after a simulated fresh login

==> Test 2 -- differential control, bogus applet id
  NOT-FOUND io.github.mrajster.obsidiannote.definitely-not-installed
    PASS bogus id correctly NOT found

==> Test 3 -- environment.d append semantics
    PASS generator output: QT_PLUGIN_PATH=~/.local/lib/plugins:/nonexistent/pre-existing/plugin/dir

==> Summary
    PASS install is discoverable after a fresh login (0 failures)
```

Control A and Test 2 are what make the proof non-vacuous. `scripts/build.sh`
runs this automatically after every install (`--no-verify` to skip).

### Adding it to the desktop

After the shell restart, right-click the desktop (or the panel) → **Add
Widgets…** → search for **Obsidian Note**, then drag it onto the desktop or
double-click it. Drag a `.md` file from Dolphin onto the placed widget to bind
it, or use *Choose Markdown File…*.

To try it without installing anything:

```bash
scripts/run-viewer.sh --sample      # needs plasmoidviewer from plasma-sdk
```

### Uninstalling

```bash
scripts/uninstall.sh                 # applet + environment.d drop-in + live session
scripts/uninstall.sh --system        # also the root-owned copy in QT_INSTALL_PLUGINS
scripts/uninstall.sh --dry-run       # show what would go
scripts/uninstall.sh --keep-dropin   # leave the QT_PLUGIN_PATH config in place
```

Because the drop-in is recorded in `build/install_manifest.txt`, the CMake
target removes it too:

```bash
cmake --build build --target uninstall
```

## Usage

**Binding it to a file.** Any of: the *Markdown file* field (or *Choose Markdown
File…*) on the config page — e.g. `~/Vault/Note.md`; the *Open Markdown File…*
toolbar button or context menu entry; dropping a `.md` file onto the widget.
If *Create the file if it does not exist* is on (the default), a missing file —
and its parent directories — is created on demand.

**Clicking.** In view mode a left click on a link follows it; a left click
anywhere else enters edit mode with the cursor near where you clicked. A right
click opens the widget's own menu (Edit, Copy All, Reload from Disk, Open
Markdown File…, Open in Obsidian) rather than the desktop menu. Clicking outside
the widget leaves edit mode and saves.

**Checkboxes.** Rendered task lines show a ballot-box glyph, empty or ticked.
Clicking one flips that line in the file immediately — there is no edit mode
and no full rewrite. Task markers inside fenced or indented code blocks, and
inside YAML frontmatter, are not rendered as checkboxes and cannot be
toggled. A toggle is refused, with a message and without writing anything, if
the file changed since it was rendered.

**Banners.** *This file was changed outside the widget* offers **Reload**
(take the on-disk version, write nothing) or **Keep My Version** (force your
buffer over the file — the only path that overwrites a changed file). A
read-only banner appears for non-UTF-8 files; a confirmation banner appears
before a reload would discard unsaved changes.

**Config options** (right-click → *Configure Obsidian Note…*):

| Page | Option | Default |
| --- | --- | --- |
| File | Markdown file | *(empty)* |
| File | Create the file if it does not exist | on |
| File | Autosave delay (`0` = `Off`, up to 600 s) | 10 s |
| File | Show file name in the toolbar | on |
| Appearance | Text font size | theme default |
| Appearance | Text font | theme default |
| Appearance | Use a monospace font while editing | on |

**Keyboard shortcuts:**

| Key | Where | Action |
| --- | --- | --- |
| `Ctrl+E` | view mode | Enter edit mode |
| `Esc` | edit mode | Save and return to view mode |
| `Ctrl+S` | edit mode | Save and stay in edit mode |
| `Esc` | view mode, in a panel | Close the popup |
| `Ctrl+Z` / `Ctrl+Shift+Z` | edit mode | Undo / Redo |
| `Ctrl+X` / `Ctrl+C` / `Ctrl+V` / `Ctrl+A` | edit mode | Cut / Copy / Paste / Select All |

In a panel, the toolbar also has a **Keep Open** pin so the popup does not close
when it loses focus.

## Building and testing from source

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
    -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

```console
$ ctest --test-dir build --output-on-failure
    Start 1: appstreamtest
1/4 Test #1: appstreamtest ....................   Passed    0.01 sec
    Start 2: tst_taskmarkdown
2/4 Test #2: tst_taskmarkdown .................   Passed    3.36 sec
    Start 3: tst_markdownnote
3/4 Test #3: tst_markdownnote .................   Passed    0.08 sec
    Start 4: obsnote_qmlharness
4/4 Test #4: obsnote_qmlharness ...............   Passed    1.77 sec

100% tests passed out of 4
```

`scripts/build.sh` runs all of the above (plus install and verification) in one
step; `scripts/build.sh --no-install` builds and tests only.

What the suites cover:

- **`appstreamtest`** — metadata validation, contributed automatically by
  Plasma's `plasma_add_applet()` CMake macro.
- **`tst_taskmarkdown`** — the pure rendering/parsing layer: `splitLines`/
  `joinLines` round-trips, task-line recognition, single-character toggling
  (including CRLF lines), `obsnote:` link parsing, wikilink rewriting, embeds
  becoming links rather than images, frontmatter hiding, fenced and indented
  code producing no clickable tasks, fence-delimiter rules, render purity — plus
  functional checks against `tests/obsidian-torture.md`, a fixture full of
  Obsidian-flavoured edge cases (YAML scalars that look like tasks, fake fences,
  nested fences, unicode).
- **`tst_markdownnote`** — the file-safety policy: CRLF / CR-only / mixed line
  endings surviving an edit-and-commit, U+2028 surviving, the trailing newline
  being neither invented nor dropped, a BOM surviving a toggle, invalid UTF-8
  going read-only and refusing every write, saves and toggles aborting when the
  file changed underneath, an identical rewrite not counting as a conflict and
  not writing, autosave refusing while a conflict is pending, `reloadFromDisk()`
  never saving first, toggles being refused for fenced lines and for stale
  expected text, and a source-level assertion that `QSaveFile`'s in-place
  truncating fallback is never re-enabled.
- **`obsnote_qmlharness`** — a headless (`QT_QPA_PLATFORM=offscreen`) smoke test
  that instantiates the real `NoteView.qml` and `NoteEditor.qml` against the
  torture fixture and fails on any QML warning or error.

There is also a QML lint target:

```bash
cmake --build build --target io.github.mrajster.obsidiannote_qmllint
```

It currently reports `unqualified access` warnings for the `i18n()` calls, which
is expected for KDE QML and does not fail the build.

## How it differs from the original Sticky Note widget

| | Sticky Note (`org.kde.plasma.notes`) | Obsidian Note |
| --- | --- | --- |
| Where the text lives | in the widget's own private note storage managed by Plasma | in a `.md` file you choose, anywhere on disk |
| Format | rich text / HTML, with a bold–italic–underline toolbar | Markdown source, rendered read-only via `Text.MarkdownText` |
| Default mode | always an editor | renders the note; one click opens the raw source |
| Checkboxes | none | GFM task lists, clickable, one line rewritten per click |
| External edits | not applicable | watched, reloaded, and guarded by a fingerprint + conflict banner |
| Write strategy | writes the note it owns | atomic, byte-faithful; never regenerates Markdown from a parsed model |
| Appearance options | note colour themes (white, yellow, translucent, …) | follows the Plasma theme; font size/family/monospace only |
| Obsidian integration | none | `[[wikilinks]]` and *Open in Obsidian* via `obsidian://` |

Unchanged from upstream: it is still a Plasma applet, still behaves like a
sticky note on the desktop or as a panel popup, and still uses Plasma's own
widget chrome and configuration UI.

## License and attribution

Licensed under the **GPL-2.0-or-later**; see [`LICENSE`](LICENSE) and
[`LICENSES/`](LICENSES). Every source file carries an SPDX header.

This project is derived from the Plasma **notes** applet in
[kdeplasma-addons](https://invent.kde.org/plasma/kdeplasma-addons)
(`applets/notes`), which is also GPL-2.0-or-later. Credit to the upstream KDE
authors named in the SPDX headers of the files this work is derived from:

- **David Edmundson** (`davidedmundson@kde.org`)
- **Kai Uwe Broulik** (`kde@privat.broulik.de`)

and to the applet's original authors as listed in its upstream metadata,
**Davide Bettio** and **Lukas Kropatschek**.

`qml/ShortcutMenuItem.qml` is taken from a separate upstream and is
**LGPL-2.0-or-later**, copyright **Luca Carlon** (`carlon.luca@gmail.com`).

## Not affiliated

Not affiliated with, endorsed by, or supported by Obsidian (Dynalist Inc.) or the
KDE project. "Obsidian" and "KDE" are the trademarks of their respective owners.
