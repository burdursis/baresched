cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SCHEDULER_VERSION)
string(STRIP "${SCHEDULER_VERSION}" SCHEDULER_VERSION)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
project(scheduler VERSION "${SCHEDULER_VERSION}" LANGUAGES CXX)

add_library(scheduler INTERFACE)
add_library(scheduler::scheduler ALIAS scheduler)
target_include_directories(scheduler INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(scheduler INTERFACE cxx_std_11)

include(CTest)
option(SCHEDULER_ENABLE_COVERAGE "Instrument host tests with GCC coverage" OFF)

if(SCHEDULER_ENABLE_COVERAGE AND NOT BUILD_TESTING)
    message(FATAL_ERROR "Coverage requires BUILD_TESTING=ON")
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

option(SCHEDULER_BUILD_EXAMPLES "Build the deterministic host example" ON)
if(SCHEDULER_BUILD_EXAMPLES)
    add_subdirectory(examples/basic)
endif()
