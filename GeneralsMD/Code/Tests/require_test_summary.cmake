# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms under GNU GPL section 7 apply: see LICENSE.md.
# A handled engine crash may exit zero without finishing the harness. Check both
# contracts; PASS_REGULAR_EXPRESSION alone would ignore nonzero process exits.
if(NOT DEFINED TEST_BINARY)
  message(FATAL_ERROR "TEST_BINARY is required")
endif()
execute_process(COMMAND ${TEST_LAUNCHER} ${TEST_EMULATOR} "${TEST_BINARY}" ${TEST_ARGS}
  RESULT_VARIABLE test_status OUTPUT_VARIABLE test_output ERROR_VARIABLE test_error)
message("${test_output}${test_error}")
if(NOT "${test_status}" STREQUAL "0")
  message(FATAL_ERROR "Test process failed: ${test_status}")
endif()
if(NOT "${test_output}${test_error}" MATCHES "[1-9][0-9]* tests, [1-9][0-9]* checks, 0 failed")
  message(FATAL_ERROR "Test exited without a successful harness completion summary")
endif()
