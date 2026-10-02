if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.25)
    project(r_standard_library VERSION 0.1.0 LANGUAGES C)
    option(R_FRONTEND_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
    option(R_FRONTEND_ENABLE_FUZZERS "Build with frontend fuzz sanitizer settings" OFF)
    option(R_FRONTEND_ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer" OFF)
endif()

set(R_LIBRARY_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(R_CLANG_FORMAT_EXECUTABLE "clang-format" CACHE FILEPATH
    "Path to the pinned clang-format executable")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/RLibraryModule.cmake")

add_subdirectory(internal)
add_subdirectory(core)
add_subdirectory(std/alloc)
add_subdirectory(std/arc)
add_subdirectory(std/rc)
add_subdirectory(std/array)
add_subdirectory(std/list)
add_subdirectory(std/dict)
add_subdirectory(std/error)
add_subdirectory(std/bytes)
add_subdirectory(std/hash)
add_subdirectory(std/utf8)
add_subdirectory(std/bits)
add_subdirectory(std/secret)
add_subdirectory(std/random)
add_subdirectory(std/test)
add_subdirectory(std/string)
add_subdirectory(std/convert)
target_link_libraries(r_std_error PUBLIC r_std_convert)
add_subdirectory(std/format)
add_subdirectory(std/json)
target_link_libraries(r_std_error PUBLIC r_std_format)
add_subdirectory(std/math)
add_subdirectory(std/time)
add_subdirectory(std/env)
add_subdirectory(std/thread)
add_subdirectory(std/sync)
add_subdirectory(std/async)
target_link_libraries(r_std_time PUBLIC r_std_async)
add_subdirectory(std/io)
add_subdirectory(std/fs)
add_subdirectory(std/net)
add_subdirectory(std/process)
add_subdirectory(std/signal)
add_subdirectory(std/c)
# Library R-SLIB-RSRC-0003 (M25): native providers of standard modules written in R.
add_subdirectory(native/tls)
add_subdirectory(native/crypto)
add_subdirectory(native/sqlite)
add_subdirectory(native/alloc)
add_subdirectory(native/fs)

add_library(r_stdlib_0_1 INTERFACE)
target_link_libraries(r_stdlib_0_1 INTERFACE
    r_core
    r_std_alloc
    r_std_arc
    r_std_rc
    r_std_array
    r_std_list
    r_std_dict
    r_std_error
    r_std_bytes
    r_std_hash
    r_std_utf8
    r_std_bits
    r_std_string
    r_std_convert
    r_std_format
    r_std_json
    r_std_math
    r_std_time
    r_std_env
    r_std_thread
    r_std_sync
    r_std_async
    r_std_io
    r_std_fs
    r_std_net
    r_std_process
    r_std_signal
    r_std_c
)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_target(library-layout-check
        COMMAND "${Python3_EXECUTABLE}"
            "${R_LIBRARY_REPOSITORY_ROOT}/tools/check_library_layout.py"
            --root "${R_LIBRARY_REPOSITORY_ROOT}"
        WORKING_DIRECTORY "${R_LIBRARY_REPOSITORY_ROOT}"
        VERBATIM
    )
    add_custom_target(library-coverage-check
        COMMAND "${Python3_EXECUTABLE}"
            "${R_LIBRARY_REPOSITORY_ROOT}/tools/check_library_layout.py"
            --root "${R_LIBRARY_REPOSITORY_ROOT}"
            --require-complete
        WORKING_DIRECTORY "${R_LIBRARY_REPOSITORY_ROOT}"
        VERBATIM
    )
    add_custom_target(verify_library_inventory
        COMMAND "${Python3_EXECUTABLE}"
            "${R_LIBRARY_REPOSITORY_ROOT}/tools/generate_library_inventory.py"
            --specification
            "${R_LIBRARY_REPOSITORY_ROOT}/specification/R_STANDARD_LIBRARY_SPECIFICATION_0_1.en.adoc"
            --inventory
            "${R_LIBRARY_REPOSITORY_ROOT}/library/generated/api_inventory/implementation_inventory.json"
            --verify
        WORKING_DIRECTORY "${R_LIBRARY_REPOSITORY_ROOT}"
        VERBATIM
    )
    add_custom_target(regenerate_library_inventory
        COMMAND "${Python3_EXECUTABLE}"
            "${R_LIBRARY_REPOSITORY_ROOT}/tools/generate_library_inventory.py"
            --specification
            "${R_LIBRARY_REPOSITORY_ROOT}/specification/R_STANDARD_LIBRARY_SPECIFICATION_0_1.en.adoc"
            --inventory
            "${R_LIBRARY_REPOSITORY_ROOT}/library/generated/api_inventory/implementation_inventory.json"
            --write
        WORKING_DIRECTORY "${R_LIBRARY_REPOSITORY_ROOT}"
        VERBATIM
    )
    add_custom_target(format-check
        COMMAND "${Python3_EXECUTABLE}"
            "${R_LIBRARY_REPOSITORY_ROOT}/tools/check_format.py"
            --root "${R_LIBRARY_REPOSITORY_ROOT}"
            --clang-format "${R_CLANG_FORMAT_EXECUTABLE}"
        WORKING_DIRECTORY "${R_LIBRARY_REPOSITORY_ROOT}"
        VERBATIM
    )
    add_custom_target(format
        COMMAND "${Python3_EXECUTABLE}"
            "${R_LIBRARY_REPOSITORY_ROOT}/tools/check_format.py"
            --root "${R_LIBRARY_REPOSITORY_ROOT}"
            --clang-format "${R_CLANG_FORMAT_EXECUTABLE}"
            --fix
        WORKING_DIRECTORY "${R_LIBRARY_REPOSITORY_ROOT}"
        VERBATIM
    )
else()
    message(STATUS "Python3 unavailable: library inventory and format targets are disabled")
endif()
