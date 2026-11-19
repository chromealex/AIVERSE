cmake_minimum_required(VERSION 3.20)
project(aiverse_physis_journal_standalone LANGUAGES CXX)

# Build the REAL PHYSIS journal headers and tests, without configuring the
# engine, ECS, editor, networking, Box3D or unrelated game dependencies.
# This project is independent from the root AIVERSE CMake graph.
if(NOT UNIX OR EMSCRIPTEN)
    message(FATAL_ERROR "PHYSIS journal standalone tests require native POSIX")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
enable_testing()

get_filename_component(AIVERSE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

function(add_physis_journal_standalone_test target name source)
    add_executable(${target} "${AIVERSE_ROOT}/tests/${source}")
    target_include_directories(${target} PRIVATE "${AIVERSE_ROOT}")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -pedantic)
    endif()
    add_test(NAME ${name} COMMAND ${target})
    set_tests_properties(${name} PROPERTIES LABELS "PHYSIS;PERSISTENCE;SECURITY;STANDALONE")
endfunction()

add_physis_journal_standalone_test(
    aiverse_physis_journal_posix_io_test
    physis_journal_posix_io physis_journal_posix_io_test.cpp)
add_physis_journal_standalone_test(
    aiverse_physis_journal_locked_io_test
    physis_journal_locked_io physis_journal_locked_io_test.cpp)

add_physis_journal_standalone_test(
    aiverse_physis_journal_crash_recovery_test
    physis_journal_crash_recovery physis_journal_crash_recovery_test.cpp)
