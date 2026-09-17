# SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
# SPDX-License-Identifier: GPL-2.0-or-later

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)

# The torture fixture the functional smoke test runs against.
set(OBSIDIAN_TORTURE_FIXTURE "${CMAKE_CURRENT_SOURCE_DIR}/obsidian-torture.md")

add_executable(tst_taskmarkdown
    tst_taskmarkdown.cpp
    ${CMAKE_SOURCE_DIR}/taskmarkdown.cpp
    ${CMAKE_SOURCE_DIR}/markdownnote.cpp
    ${CMAKE_SOURCE_DIR}/markdownblocks.cpp
)
target_include_directories(tst_taskmarkdown PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_definitions(tst_taskmarkdown PRIVATE
    TORTURE_FIXTURE="${OBSIDIAN_TORTURE_FIXTURE}"
    # Obsidian's FORMAT-REFERENCE note the geometry reference was measured on.
    FORMAT_REFERENCE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/format-reference.md"
)
target_link_libraries(tst_taskmarkdown PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Test
    KF6::CoreAddons
    KF6::I18n
)
add_test(NAME tst_taskmarkdown COMMAND tst_taskmarkdown)
set_tests_properties(tst_taskmarkdown PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --------------------------------------------------------------------------
# File-safety tests for MarkdownNote (policy S1-S7): byte fidelity, no blind
# overwrite, autosave/conflict rules, self-validating toggle, atomic writes.
# --------------------------------------------------------------------------
add_executable(tst_markdownnote
    tst_markdownnote.cpp
    ${CMAKE_SOURCE_DIR}/taskmarkdown.cpp
    ${CMAKE_SOURCE_DIR}/markdownnote.cpp
    ${CMAKE_SOURCE_DIR}/markdownblocks.cpp
)
target_include_directories(tst_markdownnote PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_definitions(tst_markdownnote PRIVATE
    # The S7 test asserts the source never re-enables QSaveFile's in-place fallback.
    MARKDOWNNOTE_CPP="${CMAKE_SOURCE_DIR}/markdownnote.cpp"
)
target_compile_definitions(tst_markdownnote PRIVATE
    TORTURE_FIXTURE="${OBSIDIAN_TORTURE_FIXTURE}"
)
target_link_libraries(tst_markdownnote PRIVATE
    Qt6::Core
    Qt6::Qml
    Qt6::Test
    KF6::CoreAddons
    KF6::I18n
)
add_test(NAME tst_markdownnote COMMAND tst_markdownnote)
set_tests_properties(tst_markdownnote PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --------------------------------------------------------------------------
# Headless QML smoke test: instantiates the REAL NoteView.qml / NoteEditor.qml
# against the torture fixture and fails on any QML warning or error.
# --------------------------------------------------------------------------
find_package(Qt6 ${QT_MIN_VERSION} QUIET COMPONENTS Quick QuickControls2)

if(TARGET Qt6::Quick)
    # The view QML lives in ../qml; copy it in so it can join this module.
    # configure_file() also puts the originals on CMAKE_CONFIGURE_DEPENDS, so
    # editing them re-runs cmake and refreshes the copies.
    set(_obsnote_view_qml
        NoteView.qml NoteEditor.qml
        ObsidianMetrics.qml BlockList.qml BlockDelegate.qml InlineText.qml
        HeadingBlock.qml ParagraphBlock.qml ListItemBlock.qml TaskCheckbox.qml
        BlockquoteBlock.qml CalloutBlock.qml CodeBlock.qml TableBlock.qml
        RuleBlock.qml PropertiesBlock.qml IndentGuides.qml)
    set(_obsnote_view_qml_copies)
    foreach(_f IN LISTS _obsnote_view_qml)
        configure_file("${CMAKE_SOURCE_DIR}/qml/${_f}" "${CMAKE_CURRENT_BINARY_DIR}/qmlharness/${_f}" COPYONLY)
        # An absolute path in a Qt resource needs an explicit alias.
        set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/qmlharness/${_f}"
            PROPERTIES QT_RESOURCE_ALIAS "${_f}")
        list(APPEND _obsnote_view_qml_copies "${CMAKE_CURRENT_BINARY_DIR}/qmlharness/${_f}")
    endforeach()

    qt_add_executable(obsnote_qmlharness
        qmlharness/main.cpp
        ${CMAKE_SOURCE_DIR}/taskmarkdown.cpp
        ${CMAKE_SOURCE_DIR}/markdownnote.cpp
        ${CMAKE_SOURCE_DIR}/markdownblocks.cpp
    )
    target_include_directories(obsnote_qmlharness PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_definitions(obsnote_qmlharness PRIVATE
        TORTURE_FIXTURE="${OBSIDIAN_TORTURE_FIXTURE}"
        GEOMETRY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/geometry"
    )
    set_source_files_properties(qmlharness/Harness.qml PROPERTIES QT_RESOURCE_ALIAS "Harness.qml")
    set_source_files_properties(qmlharness/GeometryDump.qml PROPERTIES QT_RESOURCE_ALIAS "GeometryDump.qml")
    qt_add_qml_module(obsnote_qmlharness
        URI obsnoteharness
        VERSION 1.0
        # Qt's finalizer runs qt_import_qml_plugins() on every executable: it
        # walks the QML modules NoteView.qml / NoteEditor.qml import (Plasma
        # components, Kirigami, KConfig QML, ...) and tries to link the *static*
        # plugin target behind each one. Those modules ship as shared plugins
        # and are not imported as CMake packages here, so each one emitted a
        # "the link target it defines ... does not exist in the current scope"
        # CMake warning -- 18 of them at every configure. The scan is only
        # meaningful for a statically linked Qt; this harness resolves those
        # modules at run time from the QML import path, which is exactly what
        # the test already proves. Turn the scan off.
        NO_IMPORT_SCAN
        QML_FILES
            qmlharness/Harness.qml
            qmlharness/GeometryDump.qml
            ${_obsnote_view_qml_copies}
    )
    target_link_libraries(obsnote_qmlharness PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        KF6::CoreAddons
        KF6::I18n
    )
    add_test(NAME obsnote_qmlharness COMMAND obsnote_qmlharness)
    set_tests_properties(obsnote_qmlharness PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
    )

    # ----------------------------------------------------------------------
    # Geometry parity with Obsidian's reading view: dump the laid-out block
    # rects for FORMAT-REFERENCE.md and diff them against the measured
    # Obsidian rects. Skipped (77) when the reference fonts are not installed.
    # ----------------------------------------------------------------------
    find_package(Python3 COMPONENTS Interpreter)
    # tests/geometry is the contract location; tests/parity is accepted too.
    set(_obsnote_parity_dir "")
    foreach(_d geometry parity)
        if(_obsnote_parity_dir STREQUAL "" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_d}/run_parity.cmake")
            set(_obsnote_parity_dir "${CMAKE_CURRENT_SOURCE_DIR}/${_d}")
        endif()
    endforeach()
    if(Python3_Interpreter_FOUND AND NOT _obsnote_parity_dir STREQUAL "")
        add_test(NAME geometry_parity
            COMMAND ${CMAKE_COMMAND}
                -DHARNESS=$<TARGET_FILE:obsnote_qmlharness>
                -DPY=${Python3_EXECUTABLE}
                -DDIR=${_obsnote_parity_dir}
                -P ${_obsnote_parity_dir}/run_parity.cmake)
        set_tests_properties(geometry_parity PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
            SKIP_RETURN_CODE 77
        )
    else()
        message(STATUS "obsidian-note: geometry_parity test not registered (needs Python3 and tests/geometry/run_parity.cmake)")
    endif()
endif()
