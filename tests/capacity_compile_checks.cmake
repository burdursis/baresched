# Validate the template contract independently of GoogleTest's language standard.
foreach(capacity 1 8 32 0 33)
    try_compile(capacity_compiles
        "${CMAKE_CURRENT_BINARY_DIR}/compile-checks/${capacity}"
        "${CMAKE_CURRENT_SOURCE_DIR}/capacity_compile_test.cpp"
        CMAKE_FLAGS
            "-DCMAKE_CXX_STANDARD=11"
            "-DCMAKE_CXX_STANDARD_REQUIRED=ON"
            "-DCMAKE_CXX_EXTENSIONS=OFF"
        COMPILE_DEFINITIONS "-DSCHEDULER_TEST_CAPACITY=${capacity}"
        OUTPUT_VARIABLE compile_output)
    if(capacity GREATER_EQUAL 1 AND capacity LESS_EQUAL 32)
        if(NOT capacity_compiles)
            message(FATAL_ERROR "Scheduler<${capacity}> must compile in C++11:\n${compile_output}")
        endif()
    else()
        if(capacity_compiles OR NOT compile_output MATCHES "MaxTasks must be between 1 and 32")
            message(FATAL_ERROR "Scheduler<${capacity}> must fail its capacity static_assert:\n${compile_output}")
        endif()
    endif()
endforeach()
message(STATUS "Scheduler capacity checks passed: 1, 8, 32 accepted; 0, 33 rejected")
