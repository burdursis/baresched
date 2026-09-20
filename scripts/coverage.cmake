# Executed by the coverage target, after the instrumented tests are built.
foreach(required SOURCE_DIR BINARY_DIR CTEST_EXECUTABLE GCOVR_EXECUTABLE GCOV_EXECUTABLE)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()

# Only reset counters in this build tree. Other builds are never scanned.
file(GLOB_RECURSE counters "${BINARY_DIR}/*.gcda")
if(counters)
    file(REMOVE ${counters})
endif()

execute_process(
    COMMAND "${CTEST_EXECUTABLE}" --test-dir "${BINARY_DIR}"
        -C "${TEST_CONFIG}" --output-on-failure
    RESULT_VARIABLE test_result)
if(NOT test_result STREQUAL "0")
    message(FATAL_ERROR "Tests failed; coverage was not generated")
endif()

set(report_dir "${BINARY_DIR}/reports")
file(MAKE_DIRECTORY "${report_dir}")
execute_process(
    COMMAND "${GCOVR_EXECUTABLE}"
        --root "${SOURCE_DIR}"
        --filter "include/.*"
        --gcov-executable "${GCOV_EXECUTABLE}"
        --exclude-unreachable-branches
        --exclude-throw-branches
        --html-details "${report_dir}/index.html"
        --xml-pretty --xml "${report_dir}/coverage.xml"
        --print-summary
        "${BINARY_DIR}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE coverage_result)
if(NOT coverage_result STREQUAL "0")
    message(FATAL_ERROR "gcovr failed: ${coverage_result}")
endif()
message(STATUS "Coverage report: ${report_dir}/index.html")
