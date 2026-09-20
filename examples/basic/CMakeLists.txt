add_executable(scheduler_example main.cpp)
target_link_libraries(scheduler_example PRIVATE scheduler::scheduler)
set_target_properties(scheduler_example PROPERTIES
    CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(scheduler_example PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(scheduler_example PRIVATE /W4)
endif()

if(BUILD_TESTING)
    add_test(NAME SchedulerExample.Runs COMMAND scheduler_example)
endif()

if(TARGET coverage)
    # CTest also runs the example when both options are enabled.
    add_dependencies(coverage scheduler_example)
endif()
