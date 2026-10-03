cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(r_runtime_darwin VERSION 0.1.0 LANGUAGES C)
    include(CTest)
endif()

if(NOT APPLE)
    message(FATAL_ERROR "The Darwin runtime adapters require an Apple target")
endif()

find_package(Threads REQUIRED)

option(R_RUNTIME_DARWIN_BUILD_TESTS "Build Darwin runtime adapter tests" ON)
option(R_RUNTIME_DARWIN_ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)
option(R_RUNTIME_DARWIN_ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer" OFF)

if(R_RUNTIME_DARWIN_ENABLE_SANITIZERS AND R_RUNTIME_DARWIN_ENABLE_THREAD_SANITIZER)
    message(FATAL_ERROR "AddressSanitizer/UBSan and ThreadSanitizer are separate configurations")
endif()

function(r_runtime_darwin_apply_c17_contract target)
    set_target_properties(
        ${target}
        PROPERTIES
            C_STANDARD 17
            C_STANDARD_REQUIRED YES
            C_EXTENSIONS NO
    )
    target_compile_options(
        ${target}
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wstrict-prototypes
            -Wmissing-prototypes
            -Wswitch-enum
    )
    if(R_RUNTIME_DARWIN_ENABLE_SANITIZERS)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
    if(R_RUNTIME_DARWIN_ENABLE_THREAD_SANITIZER)
        target_compile_options(${target} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=thread)
    endif()
endfunction()

add_library(
    r_runtime_darwin_base
    STATIC
        source/stack.c
        ../source/panic_abort.c
        ../source/thread_attachment.c
)
target_include_directories(r_runtime_darwin_base PUBLIC ../include)
target_link_libraries(r_runtime_darwin_base PUBLIC Threads::Threads)
r_runtime_darwin_apply_c17_contract(r_runtime_darwin_base)

add_library(r_runtime_darwin_event STATIC source/event.c)
target_include_directories(r_runtime_darwin_event PUBLIC include)
r_runtime_darwin_apply_c17_contract(r_runtime_darwin_event)

add_library(r_runtime_darwin_dns STATIC source/dns.c)
target_include_directories(r_runtime_darwin_dns PUBLIC include ../include)
target_link_libraries(r_runtime_darwin_dns PUBLIC Threads::Threads r_runtime_darwin_event)
r_runtime_darwin_apply_c17_contract(r_runtime_darwin_dns)

add_library(
    r_runtime_darwin_fs_lane
    STATIC
        source/fs_lane.c
        source/fs_lane_async.c
        source/fs_service.c
        source/fs_lane_syscalls.c
)
target_include_directories(
    r_runtime_darwin_fs_lane
    PUBLIC include
    PRIVATE source ../include
)
target_link_libraries(r_runtime_darwin_fs_lane PUBLIC Threads::Threads r_runtime_darwin_event)
r_runtime_darwin_apply_c17_contract(r_runtime_darwin_fs_lane)

add_library(
    r_runtime_darwin_io
    STATIC
        source/io_buffer.c
        source/io_close.c
        source/io_console.c
        source/io_deadline.c
        source/io_direct.c
        source/io_dispatch.c
        source/io_flush.c
        source/io_handle.c
        source/io_prepare.c
        source/io_read.c
        source/io_request.c
        source/io_shutdown.c
        source/io_write.c
)
target_include_directories(
    r_runtime_darwin_io
    PUBLIC include
    PRIVATE source ../include
)
target_link_libraries(r_runtime_darwin_io PUBLIC Threads::Threads r_runtime_darwin_event)
set_source_files_properties(
    source/io_deadline.c
    source/io_dispatch.c
    source/io_flush.c
    source/io_handle.c
    source/io_prepare.c
    source/io_read.c
    source/io_write.c
    PROPERTIES COMPILE_OPTIONS -fblocks
)
r_runtime_darwin_apply_c17_contract(r_runtime_darwin_io)

add_library(r_runtime_darwin_task STATIC source/task.c)
target_include_directories(r_runtime_darwin_task PUBLIC include ../include)
target_link_libraries(
    r_runtime_darwin_task
    PUBLIC Threads::Threads r_runtime_darwin_base r_runtime_darwin_event
)
r_runtime_darwin_apply_c17_contract(r_runtime_darwin_task)

add_library(r_runtime_darwin_process STATIC source/process.c source/signal.c)
target_include_directories(r_runtime_darwin_process PUBLIC include ../include)
target_link_libraries(
    r_runtime_darwin_process
    PUBLIC Threads::Threads r_runtime_darwin_event r_runtime_darwin_io
)
r_runtime_darwin_apply_c17_contract(r_runtime_darwin_process)

add_library(
    r_runtime_darwin_socket
    STATIC
        source/socket_accept.c
        source/socket_connect.c
        source/socket_datagram.c
)
target_include_directories(r_runtime_darwin_socket PUBLIC include ../include)
target_link_libraries(r_runtime_darwin_socket PUBLIC Threads::Threads r_runtime_darwin_event)
r_runtime_darwin_apply_c17_contract(r_runtime_darwin_socket)

add_library(r_runtime_darwin_timer STATIC source/timer.c)
target_include_directories(r_runtime_darwin_timer PUBLIC include ../include)
target_link_libraries(r_runtime_darwin_timer PUBLIC r_runtime_darwin_task)
r_runtime_darwin_apply_c17_contract(r_runtime_darwin_timer)

if(R_RUNTIME_DARWIN_BUILD_TESTS)
    add_library(r_runtime_darwin_test_allocator STATIC ../source/allocator.c)
    target_include_directories(r_runtime_darwin_test_allocator PUBLIC ../include)
    r_runtime_darwin_apply_c17_contract(r_runtime_darwin_test_allocator)

    target_compile_definitions(r_runtime_darwin_timer PRIVATE R_RUNTIME_DARWIN_TIMER_TESTING=1)
    target_compile_definitions(
        r_runtime_darwin_socket
        PRIVATE R_RUNTIME_DARWIN_SOCKET_TESTING=1
    )
    target_compile_definitions(
        r_runtime_darwin_process
        PRIVATE R_RUNTIME_DARWIN_PROCESS_TESTING=1
    )

    target_compile_definitions(
        r_runtime_darwin_event
        PRIVATE R_RUNTIME_DARWIN_EVENT_TESTING=1
    )
    add_executable(r_runtime_darwin_event_tests tests/event_tests.c)
    target_compile_definitions(
        r_runtime_darwin_event_tests
        PRIVATE R_RUNTIME_DARWIN_EVENT_TESTING=1
    )
    target_link_libraries(
        r_runtime_darwin_event_tests
        PRIVATE r_runtime_darwin_event Threads::Threads
    )
    r_runtime_darwin_apply_c17_contract(r_runtime_darwin_event_tests)
    add_test(NAME r_runtime_darwin_event COMMAND r_runtime_darwin_event_tests)
    set_tests_properties(r_runtime_darwin_event PROPERTIES TIMEOUT 10)

    add_executable(r_runtime_darwin_dns_tests tests/dns_tests.c)
    target_link_libraries(
        r_runtime_darwin_dns_tests
        PRIVATE r_runtime_darwin_dns r_runtime_darwin_test_allocator
    )
    r_runtime_darwin_apply_c17_contract(r_runtime_darwin_dns_tests)
    add_test(NAME r_runtime_darwin_dns COMMAND r_runtime_darwin_dns_tests)
    set_tests_properties(r_runtime_darwin_dns PROPERTIES TIMEOUT 20)

    target_compile_definitions(
        r_runtime_darwin_fs_lane
        PRIVATE R_RUNTIME_DARWIN_FS_LANE_TESTING=1
    )
    add_executable(r_runtime_darwin_fs_lane_tests tests/fs_lane_tests.c)
    target_compile_definitions(
        r_runtime_darwin_fs_lane_tests
        PRIVATE R_RUNTIME_DARWIN_FS_LANE_TESTING=1
    )
    target_link_libraries(
        r_runtime_darwin_fs_lane_tests
        PRIVATE r_runtime_darwin_fs_lane r_runtime_darwin_test_allocator
    )
    r_runtime_darwin_apply_c17_contract(r_runtime_darwin_fs_lane_tests)
    add_test(NAME r_runtime_darwin_fs_lane COMMAND r_runtime_darwin_fs_lane_tests)
    set_tests_properties(r_runtime_darwin_fs_lane PROPERTIES TIMEOUT 10)

    target_compile_definitions(r_runtime_darwin_io PRIVATE R_RUNTIME_DARWIN_IO_TESTING=1)
    add_executable(r_runtime_darwin_io_tests tests/io_tests.c)
    target_compile_definitions(
        r_runtime_darwin_io_tests
        PRIVATE R_RUNTIME_DARWIN_IO_TESTING=1
    )
    target_link_libraries(
        r_runtime_darwin_io_tests
        PRIVATE r_runtime_darwin_io r_runtime_darwin_test_allocator
    )
    r_runtime_darwin_apply_c17_contract(r_runtime_darwin_io_tests)
    add_test(NAME r_runtime_darwin_io COMMAND r_runtime_darwin_io_tests)
    set_tests_properties(r_runtime_darwin_io PROPERTIES TIMEOUT 20)

    add_executable(r_runtime_darwin_io_direct_tests tests/io_direct_tests.c)
    target_compile_definitions(
        r_runtime_darwin_io_direct_tests
        PRIVATE R_RUNTIME_DARWIN_IO_TESTING=1
    )
    target_link_libraries(
        r_runtime_darwin_io_direct_tests
        PRIVATE r_runtime_darwin_io r_runtime_darwin_test_allocator Threads::Threads
    )
    r_runtime_darwin_apply_c17_contract(r_runtime_darwin_io_direct_tests)
    add_test(NAME r_runtime_darwin_io_direct COMMAND r_runtime_darwin_io_direct_tests)
    set_tests_properties(r_runtime_darwin_io_direct PROPERTIES TIMEOUT 60)

    add_executable(r_runtime_darwin_task_tests tests/task_tests.c)
    target_link_libraries(
        r_runtime_darwin_task_tests
        PRIVATE r_runtime_darwin_task r_runtime_darwin_test_allocator
    )
    r_runtime_darwin_apply_c17_contract(r_runtime_darwin_task_tests)
    add_test(NAME r_runtime_darwin_task COMMAND r_runtime_darwin_task_tests)
    set_tests_properties(r_runtime_darwin_task PROPERTIES TIMEOUT 20)

    if(TARGET r_runtime_0_1)
        add_executable(r_runtime_darwin_stack_tests tests/stack_tests.c)
        target_link_libraries(r_runtime_darwin_stack_tests PRIVATE r_runtime_0_1 Threads::Threads)
        r_runtime_darwin_apply_c17_contract(r_runtime_darwin_stack_tests)
        add_test(NAME r_runtime_darwin_stack COMMAND r_runtime_darwin_stack_tests)
        set_tests_properties(r_runtime_darwin_stack PROPERTIES TIMEOUT 10)
    endif()

    add_executable(r_runtime_darwin_signal_tests tests/signal_tests.c)
    target_link_libraries(
        r_runtime_darwin_signal_tests
        PRIVATE r_runtime_darwin_process r_runtime_darwin_test_allocator
    )
    r_runtime_darwin_apply_c17_contract(r_runtime_darwin_signal_tests)
    add_test(NAME r_runtime_darwin_signal COMMAND r_runtime_darwin_signal_tests)
    set_tests_properties(r_runtime_darwin_signal PROPERTIES TIMEOUT 20)

    add_executable(r_runtime_darwin_socket_tests tests/socket_tests.c)
    target_link_libraries(
        r_runtime_darwin_socket_tests
        PRIVATE r_runtime_darwin_socket r_runtime_darwin_test_allocator
    )
    r_runtime_darwin_apply_c17_contract(r_runtime_darwin_socket_tests)
    add_test(NAME r_runtime_darwin_socket COMMAND r_runtime_darwin_socket_tests)
    set_tests_properties(r_runtime_darwin_socket PROPERTIES TIMEOUT 20)

    add_executable(r_runtime_darwin_socket_accept_tests tests/socket_accept_tests.c)
    target_link_libraries(
        r_runtime_darwin_socket_accept_tests
        PRIVATE r_runtime_darwin_socket r_runtime_darwin_test_allocator
    )
    r_runtime_darwin_apply_c17_contract(r_runtime_darwin_socket_accept_tests)
    add_test(NAME r_runtime_darwin_socket_accept COMMAND r_runtime_darwin_socket_accept_tests)
    set_tests_properties(r_runtime_darwin_socket_accept PROPERTIES TIMEOUT 30)
endif()
