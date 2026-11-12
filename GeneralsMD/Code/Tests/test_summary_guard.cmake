# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms under GNU GPL section 7 apply: see LICENSE.md.
set(work "${CMAKE_CURRENT_BINARY_DIR}/summary-guard-selfcheck")
file(MAKE_DIRECTORY "${work}")
set(fake "${work}/fake.cmake")
file(WRITE "${fake}" [=[
if(MODE STREQUAL "early")
  return()
elseif(MODE STREQUAL "zero")
  message("0 tests, 0 checks, 0 failed")
  return()
elseif(MODE STREQUAL "prefix")
  if(NOT "$ENV{ZHR_TEST_LAUNCHER}" STREQUAL "1" OR NOT "$ENV{ZHR_TEST_EMULATOR}" STREQUAL "1")
    message(FATAL_ERROR "test launcher or emulator was lost")
  endif()
endif()
message("1 tests, 1 checks, 0 failed")
if(MODE STREQUAL "late")
  message(FATAL_ERROR "failure after the summary")
endif()
]=])
foreach(mode normal early zero late prefix)
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DTEST_BINARY=${CMAKE_COMMAND}"
    "-DTEST_ARGS=-DMODE=${mode};-P;${fake}"
    "-DTEST_LAUNCHER=${CMAKE_COMMAND};-E;env;ZHR_TEST_LAUNCHER=1"
    "-DTEST_EMULATOR=${CMAKE_COMMAND};-E;env;ZHR_TEST_EMULATOR=1"
    -P "${CMAKE_CURRENT_LIST_DIR}/require_test_summary.cmake"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
  if(mode STREQUAL "normal" OR mode STREQUAL "prefix")
    if(NOT "${status}" STREQUAL "0")
      message(FATAL_ERROR "valid harness failed (${mode}): ${output}${errors}")
    endif()
  elseif("${status}" STREQUAL "0")
    message(FATAL_ERROR "invalid harness passed (${mode})")
  endif()
endforeach()
file(REMOVE_RECURSE "${work}")
message(STATUS "summary guard: completion, exit status, launcher and emulator checked")
