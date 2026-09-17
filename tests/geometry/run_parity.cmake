# SPDX-FileCopyrightText: 2026 obsidian-sticky-note contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# ctest geometry_parity driver:
#   cmake -DHARNESS=<obsnote_qmlharness> -DPY=<python3> -DDIR=<tests/geometry> -P run_parity.cmake
# Dumps the laid-out block rects of FORMAT-REFERENCE.md at each measured
# Obsidian width and diffs them with compare_geometry.py. Exits 77 (skip) when
# the fonts the reference was measured with are not installed.

foreach(_v HARNESS PY DIR)
    if(NOT DEFINED ${_v} OR "${${_v}}" STREQUAL "")
        message(FATAL_ERROR "run_parity.cmake: -D${_v}=... is required")
    endif()
endforeach()

find_program(_fc_list fc-list)
if(NOT _fc_list)
    message("SKIP: fc-list not found; cannot confirm the reference fonts")
    cmake_language(EXIT 77)
endif()
execute_process(COMMAND ${_fc_list} : family OUTPUT_VARIABLE _families)
foreach(_font "Noto Sans" "DejaVu Sans Mono")
    string(REGEX MATCH "(^|\n|,)${_font}(\n|,|$)" _hit "${_families}")
    if(_hit STREQUAL "")
        message("SKIP: font '${_font}' is not installed")
        cmake_language(EXIT 77)
    endif()
endforeach()

set(_work "${CMAKE_CURRENT_BINARY_DIR}/geometry_parity")
file(MAKE_DIRECTORY "${_work}")

# width -> reference file
set(_cases "700=obsidian-1.13.7-rects.json" "380=obsidian-1.13.7-rects-380.json")
set(_failed "")
foreach(_case IN LISTS _cases)
    string(REPLACE "=" ";" _parts "${_case}")
    list(GET _parts 0 _width)
    list(GET _parts 1 _ref)
    if(NOT EXISTS "${DIR}/${_ref}")
        message(FATAL_ERROR "missing reference ${DIR}/${_ref}")
    endif()
    set(_out "${_work}/ours-${_width}.json")
    file(REMOVE "${_out}")
    execute_process(
        COMMAND "${HARNESS}" --dump-geometry "${DIR}/FORMAT-REFERENCE.md"
                --width ${_width} --base-px 16 --text-family "Noto Sans"
                --out "${_out}" --png "${_work}/ours-${_width}.png"
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0 OR NOT EXISTS "${_out}")
        message("FAIL: geometry dump at width ${_width} exited ${_rc}")
        list(APPEND _failed "dump-${_width}")
        continue()
    endif()
    message("=== width ${_width} vs ${_ref} ===")
    execute_process(COMMAND "${PY}" "${DIR}/compare_geometry.py" "${DIR}/${_ref}" "${_out}"
                    RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        list(APPEND _failed "compare-${_width}")
    endif()
endforeach()

if(_failed)
    message(FATAL_ERROR "geometry_parity failed: ${_failed}")
endif()
