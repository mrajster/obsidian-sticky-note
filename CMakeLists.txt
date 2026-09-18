# SPDX-FileCopyrightText: 2014 David Edmundson <davidedmundson@kde.org>
# SPDX-FileCopyrightText: 2014, 2015 Kai Uwe Broulik <kde@privat.broulik.de>
# SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
#
# Derived from the Plasma "notes" applet (kdeplasma-addons, applets/notes),
# originally by Davide Bettio and Lukas Kropatschek.
#
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.20)

project(obsidiannote VERSION 1.0.0)

set(QT_MIN_VERSION 6.7.0)
set(KF6_MIN_VERSION 6.0.0)

find_package(ECM ${KF6_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMQmlModule)
include(FeatureSummary)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Quick)
find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS CoreAddons Config I18n Svg)
find_package(Plasma ${KF6_MIN_VERSION} REQUIRED)

add_definitions(-DTRANSLATION_DOMAIN="plasma_applet_io.github.mrajster.obsidiannote")

# qml/NotePalette.qml (per-paper colour palette) is added by a separate change;
# include it as soon as it exists. Depending on the qml/ directory itself makes
# adding the file re-run cmake, so it never silently stays out of the build.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/qml")
set(OBSIDIANNOTE_PALETTE_QML "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/qml/NotePalette.qml")
    set(OBSIDIANNOTE_PALETTE_QML qml/NotePalette.qml)
endif()

plasma_add_applet(io.github.mrajster.obsidiannote GENERATE_APPLET_CLASS
    QML_SOURCES
        qml/main.qml
        qml/NoteView.qml
        qml/NoteEditor.qml
        qml/ShortcutMenuItem.qml
        qml/config.qml
        qml/configNote.qml
        qml/configAppearance.qml
        qml/ObsidianMetrics.qml
        qml/BlockList.qml
        qml/BlockDelegate.qml
        qml/InlineText.qml
        qml/HeadingBlock.qml
        qml/ParagraphBlock.qml
        qml/ListItemBlock.qml
        qml/TaskCheckbox.qml
        qml/BlockquoteBlock.qml
        qml/CalloutBlock.qml
        qml/CodeBlock.qml
        qml/TableBlock.qml
        qml/RuleBlock.qml
        qml/PropertiesBlock.qml
        qml/IndentGuides.qml
        ${OBSIDIANNOTE_PALETTE_QML}
    CPP_SOURCES
        markdownnote.cpp
        taskmarkdown.cpp
        markdownblocks.cpp
        noteiconengine.cpp
        noteshelper.cpp
    RESOURCES
        main.xml
)

target_link_libraries(io.github.mrajster.obsidiannote PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    KF6::CoreAddons
    KF6::I18n
    KF6::Svg
)

# ---------------------------------------------------------------------------
# Install discoverability
#
# plasma_add_applet() installs exactly one artefact:
#     ${KDE_INSTALL_FULL_QTPLUGINDIR}/plasma/applets/io.github.mrajster.obsidiannote.so
# plasmashell resolves applets with KPluginMetaData::findPluginById(), which
# only looks at QCoreApplication::libraryPaths() -- i.e. $QT_PLUGIN_PATH plus
# Qt's own QT_INSTALL_PLUGINS. Installing anywhere else without also extending
# QT_PLUGIN_PATH produces a build that "works" only until the next login.
#
# Two supported routes:
#   system-wide  cmake -DCMAKE_INSTALL_PREFIX=/usr -DKDE_INSTALL_USE_QT_SYS_PATHS=ON
#                -> lands in QT_INSTALL_PLUGINS, nothing else needed (needs root)
#   user-local   cmake -DCMAKE_INSTALL_PREFIX=$HOME/.local   (the default here)
#                -> lands outside Qt's search path, so we additionally install a
#                   persistent ~/.config/environment.d drop-in.
# ---------------------------------------------------------------------------

include(ECMQueryQt)
ecm_query_qt(OBSIDIANNOTE_QT_SYSTEM_PLUGIN_DIR QT_INSTALL_PLUGINS)

# The directory that has to be on Qt's plugin search path (the parent of
# "plasma/applets"), as an absolute, normalised path.
get_filename_component(OBSIDIANNOTE_PLUGIN_ROOT "${KDE_INSTALL_FULL_QTPLUGINDIR}" ABSOLUTE)
get_filename_component(_obsidiannote_qt_plugin_dir "${OBSIDIANNOTE_QT_SYSTEM_PLUGIN_DIR}" ABSOLUTE)

if(OBSIDIANNOTE_PLUGIN_ROOT STREQUAL _obsidiannote_qt_plugin_dir)
    set(_obsidiannote_dropin_default OFF)
    set(_obsidiannote_route "system-wide (inside QT_INSTALL_PLUGINS)")
else()
    set(_obsidiannote_dropin_default ON)
    set(_obsidiannote_route "user-local (outside QT_INSTALL_PLUGINS)")
endif()

option(INSTALL_ENVIRONMENT_DROPIN
    "Install a ~/.config/environment.d drop-in that puts the install prefix on QT_PLUGIN_PATH"
    ${_obsidiannote_dropin_default})

set(OBSIDIANNOTE_DROPIN_NAME "60-obsidiannote-qt-plugin-path.conf")

if(DEFINED ENV{XDG_CONFIG_HOME} AND NOT "$ENV{XDG_CONFIG_HOME}" STREQUAL "")
    set(OBSIDIANNOTE_DROPIN_DIR "$ENV{XDG_CONFIG_HOME}/environment.d")
else()
    set(OBSIDIANNOTE_DROPIN_DIR "$ENV{HOME}/.config/environment.d")
endif()
set(OBSIDIANNOTE_DROPIN_FILE "${OBSIDIANNOTE_DROPIN_DIR}/${OBSIDIANNOTE_DROPIN_NAME}")

# Keep the generated drop-in relocatable: write a literal ${HOME} (which systemd
# expands) instead of a hardcoded home directory when the prefix lives under it.
# Done with a plain prefix comparison, not a regex, so a home directory
# containing regex metacharacters (+, (, ., ...) cannot corrupt the result.
set(OBSIDIANNOTE_PLUGIN_ROOT_TEMPLATED "${OBSIDIANNOTE_PLUGIN_ROOT}")
set(_obsidiannote_home "$ENV{HOME}")
string(REGEX REPLACE "/+$" "" _obsidiannote_home "${_obsidiannote_home}")
if(NOT _obsidiannote_home STREQUAL "")
    string(LENGTH "${_obsidiannote_home}" _obsidiannote_home_len)
    string(LENGTH "${OBSIDIANNOTE_PLUGIN_ROOT}" _obsidiannote_root_len)
    if(NOT _obsidiannote_root_len LESS _obsidiannote_home_len)
        string(SUBSTRING "${OBSIDIANNOTE_PLUGIN_ROOT}" 0 ${_obsidiannote_home_len} _obsidiannote_head)
        string(SUBSTRING "${OBSIDIANNOTE_PLUGIN_ROOT}" ${_obsidiannote_home_len} -1 _obsidiannote_tail)
        # "" (prefix is $HOME itself) or "/..." -- never "/home/bobby" for HOME=/home/bob
        if(_obsidiannote_head STREQUAL _obsidiannote_home
           AND (_obsidiannote_tail STREQUAL "" OR _obsidiannote_tail MATCHES "^/"))
            set(_obsidiannote_dollar "$")   # keep "${HOME}" literal in the .conf
            set(OBSIDIANNOTE_PLUGIN_ROOT_TEMPLATED
                "${_obsidiannote_dollar}{HOME}${_obsidiannote_tail}")
        endif()
    endif()
endif()
unset(_obsidiannote_home)
unset(_obsidiannote_home_len)
unset(_obsidiannote_root_len)
unset(_obsidiannote_head)
unset(_obsidiannote_tail)
unset(_obsidiannote_dollar)

message(STATUS "obsidian-note: install route     : ${_obsidiannote_route}")
message(STATUS "obsidian-note: applet .so goes to: ${OBSIDIANNOTE_PLUGIN_ROOT}/plasma/applets")
message(STATUS "obsidian-note: Qt searches       : ${_obsidiannote_qt_plugin_dir}")

if(INSTALL_ENVIRONMENT_DROPIN)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/environment.d.conf.in"
        "${CMAKE_CURRENT_BINARY_DIR}/${OBSIDIANNOTE_DROPIN_NAME}"
        @ONLY)

    # file(INSTALL) inside install(CODE) still records the file in
    # install_manifest.txt, so "cmake --build build --target uninstall" and
    # scripts/uninstall.sh both pick it up. Skipped under DESTDIR, where the
    # install is a packaging staging run and $HOME must not be touched.
    install(CODE "
if(NOT \"\$ENV{DESTDIR}\" STREQUAL \"\")
    message(STATUS \"DESTDIR set -- skipping environment.d drop-in (packaging build)\")
else()
    file(INSTALL
         DESTINATION \"${OBSIDIANNOTE_DROPIN_DIR}\"
         TYPE FILE
         FILES \"${CMAKE_CURRENT_BINARY_DIR}/${OBSIDIANNOTE_DROPIN_NAME}\")
endif()
")
    message(STATUS "obsidian-note: env drop-in       : ${OBSIDIANNOTE_DROPIN_FILE}")
else()
    message(STATUS "obsidian-note: env drop-in       : not needed")
endif()

# Because the drop-in is installed with file(INSTALL) it is recorded in
# install_manifest.txt, so the "uninstall" target that KDECMakeSettings already
# provides (ECMUninstallTarget) removes it along with the applet:
#     cmake --build build --target uninstall
# scripts/uninstall.sh does the same without needing a build directory.

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
