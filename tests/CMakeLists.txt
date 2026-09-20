find_package(GTest REQUIRED)
include(capacity_compile_checks.cmake)
add_executable(scheduler_tests
    main.cpp
    test_support.cpp
    time_test.cpp
    validation_test.cpp
    lifecycle_test.cpp
    runtime_control_test.cpp
    selection_test.cpp
    periodic_test.cpp
    background_test.cpp
    diagnostics_test.cpp
    boundary_test.cpp
    capacity_test.cpp
)
target_link_libraries(scheduler_tests PRIVATE scheduler::scheduler GTest::gtest)
# An installed GoogleTest may propagate a newer standard for host tests.
set_target_properties(scheduler_tests PROPERTIES
    CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(scheduler_tests PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(scheduler_tests PRIVATE /W4)
endif()

include(GoogleTest)
gtest_discover_tests(scheduler_tests)

if(SCHEDULER_ENABLE_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "Coverage currently requires GCC and matching gcov")
    endif()
    find_program(GCOVR_EXECUTABLE gcovr REQUIRED)
    set(SCHEDULER_GCOV_EXECUTABLE gcov CACHE STRING "gcov matching the GCC compiler")
    target_compile_options(scheduler_tests PRIVATE -O0 -g --coverage)
    target_link_options(scheduler_tests PRIVATE --coverage)
    add_custom_target(coverage
        COMMAND "${CMAKE_COMMAND}"
            "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DBINARY_DIR=${PROJECT_BINARY_DIR}"
            "-DCTEST_EXECUTABLE=${CMAKE_CTEST_COMMAND}"
            "-DGCOVR_EXECUTABLE=${GCOVR_EXECUTABLE}"
            "-DGCOV_EXECUTABLE=${SCHEDULER_GCOV_EXECUTABLE}"
            "-DTEST_CONFIG=$<CONFIG>"
            -P "${PROJECT_SOURCE_DIR}/scripts/coverage.cmake"
        DEPENDS scheduler_tests
        USES_TERMINAL
        COMMENT "Run tests with fresh counters and generate coverage reports"
        VERBATIM)
endif()
