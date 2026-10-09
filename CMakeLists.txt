cmake_minimum_required(VERSION 3.25)

project(r_frontend VERSION 0.1.0 LANGUAGES C)

option(R_FRONTEND_BUILD_TESTS "Build the R frontend tests" ON)
option(R_FRONTEND_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(R_FRONTEND_ENABLE_FUZZERS "Build libFuzzer entrypoints" OFF)
option(R_FRONTEND_ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer" OFF)

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND
   (R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS))
    message(FATAL_ERROR "ThreadSanitizer cannot be combined with ASan/UBSan or fuzzing")
endif()

if(R_FRONTEND_BUILD_TESTS)
    enable_testing()
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Threads REQUIRED)

# LLVM transition B0: r-front links the LLVM release of the pinned toolchain through its C
# interface (llvm-c); compiler/llvm verifies the target machine against the target manifest.
if(NOT DEFINED R_LLVM_ROOT)
    set(R_LLVM_ROOT "/opt/homebrew/opt/llvm@22" CACHE PATH "Root of the pinned LLVM 22.1.8 installation")
endif()
find_program(R_LLVM_CONFIG llvm-config HINTS "${R_LLVM_ROOT}/bin" NO_DEFAULT_PATH)
if(NOT R_LLVM_CONFIG)
    message(FATAL_ERROR "llvm-config of the pinned LLVM 22.1.8 is not found under ${R_LLVM_ROOT}")
endif()
execute_process(COMMAND "${R_LLVM_CONFIG}" --version
    OUTPUT_VARIABLE R_LLVM_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT R_LLVM_VERSION STREQUAL "22.1.8")
    message(FATAL_ERROR "LLVM ${R_LLVM_VERSION} at ${R_LLVM_ROOT} is not the pinned LLVM 22.1.8")
endif()
execute_process(COMMAND "${R_LLVM_CONFIG}" --includedir
    OUTPUT_VARIABLE R_LLVM_INCLUDE_DIR OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${R_LLVM_CONFIG}" --libdir
    OUTPUT_VARIABLE R_LLVM_LIBRARY_DIR OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
find_library(R_LLVM_LIBRARY NAMES LLVM PATHS "${R_LLVM_LIBRARY_DIR}" NO_DEFAULT_PATH REQUIRED)

add_library(r_frontend STATIC
    compiler/source/frontend.c
    compiler/source/link_manifest.c
    compiler/source/link_resolve.c
    compiler/source/abi_record.c
    compiler/source/named_standard_copy_abi.c
    compiler/source/named_standard_move_abi.c
    compiler/source/standard_error_erasures.c
    compiler/source/standard_math_operations.c
    compiler/source/unicode.c
    compiler/source/unicode_data.c
    compiler/source/target_manifest.c
    compiler/lexer/lexer.c
    compiler/lexer/lexer_generated.c
    compiler/parser/parser.c
    compiler/ast/ast.c
    compiler/ast/ast_view.c
    compiler/semantic/semantic.c
    compiler/hir/hir.c
    compiler/mir/mir.c
    compiler/codegen/c_abi_bridge.c
    compiler/llvm/abi.c
    compiler/llvm/async.c
    compiler/llvm/atomics.c
    compiler/llvm/clone.c
    compiler/llvm/containers.c
    compiler/llvm/dispatch.c
    compiler/llvm/emit.c
    compiler/llvm/ffi.c
    compiler/llvm/format.c
    compiler/llvm/glue.c
    compiler/llvm/hosted_main.c
    compiler/llvm/json.c
    compiler/llvm/json_decode.c
    compiler/llvm/json_encode.c
    compiler/llvm/json_stream.c
    compiler/llvm/keys.c
    compiler/llvm/library.c
    compiler/llvm/outcome.c
    compiler/llvm/reflection.c
    compiler/llvm/runtime.c
    compiler/llvm/stack.c
    compiler/llvm/standard.c
    compiler/llvm/statics.c
    compiler/llvm/surface.c
    compiler/llvm/sync.c
    compiler/llvm/tagged.c
    compiler/llvm/target.c
    compiler/llvm/text.c
    compiler/llvm/threads.c
    compiler/llvm/types.c
    compiler/llvm/values.c
    compiler/source/dump.c
)

target_include_directories(r_frontend
    PUBLIC compiler/include
    PRIVATE compiler/source runtime/include
)
# Only the llvm-c headers are used; the include directory also holds C++ and system-like headers,
# so it is a private system directory of the compiler library.
target_include_directories(r_frontend SYSTEM PRIVATE "${R_LLVM_INCLUDE_DIR}")
target_link_libraries(r_frontend PUBLIC "${R_LLVM_LIBRARY}")

# Native layout facts are compiled from the same pinned ABI headers the program is linked with.
file(GLOB R_STANDARD_LAYOUT_INCLUDE_DIRS LIST_DIRECTORIES true
    "${CMAKE_CURRENT_SOURCE_DIR}/library/std/*/include")
target_include_directories(r_frontend PRIVATE
    library/core/include runtime/darwin/include ${R_STANDARD_LAYOUT_INCLUDE_DIRS})

# LLVM transition (B4): the static inline functions of the runtime and library headers as
# external functions for programs the LLVM emitter builds (tools/generate_runtime_surface.py).
add_library(r_runtime_inline_shims STATIC runtime/llvm/inline_shims.generated.c)
target_include_directories(r_runtime_inline_shims PRIVATE
    runtime/include runtime/darwin/include library/core/include ${R_STANDARD_LAYOUT_INCLUDE_DIRS})

add_library(r_runtime_0_1 STATIC
    runtime/source/allocator.c
    runtime/source/arc.c
    runtime/source/array.c
    runtime/source/dict.c
    runtime/source/drop.c
    runtime/source/hosted_entry.c
    runtime/source/list.c
    runtime/source/own.c
    runtime/source/rc.c
    runtime/source/string.c
    runtime/source/thread_attachment.c
    runtime/source/utf8.c
)
target_include_directories(r_runtime_0_1 PUBLIC runtime/include)
target_link_libraries(r_runtime_0_1 PUBLIC Threads::Threads)

# Freestanding core runtime (Core R-CONF-0005, Annex G.3): panic through the environment handler,
# environment-adopted stack bounds and the thread-local destruction hook; no allocator, I/O or
# threads. Its include directory precedes runtime/include so that generated freestanding programs
# resolve the freestanding r_runtime_target_abi.h.
add_library(r_runtime_freestanding STATIC
    runtime/freestanding/source/panic.c
    runtime/freestanding/source/stack.c
    runtime/freestanding/source/thread_local.c
)
target_include_directories(r_runtime_freestanding PUBLIC
    runtime/freestanding/include
    runtime/include
)

add_subdirectory(library)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    set(R_ASCIIDOCTOR_EXECUTABLE "asciidoctor" CACHE FILEPATH
        "Path to the pinned Asciidoctor executable")
    add_custom_target(specification-check
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_standard_methods.py" --check
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_spec_parity.py"
            --spec-dir "${CMAKE_CURRENT_SOURCE_DIR}/specification"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_grammar.py"
            --specification
            "${CMAKE_CURRENT_SOURCE_DIR}/specification/R_LANGUAGE_SPECIFICATION_0_1.en.adoc"
            --manifest
            "${CMAKE_CURRENT_SOURCE_DIR}/compiler/grammar/annex_a_manifest.json"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_rule_inventory.py"
            --specification
            "${CMAKE_CURRENT_SOURCE_DIR}/specification/R_LANGUAGE_SPECIFICATION_0_1.en.adoc"
            --inventory
            "${CMAKE_CURRENT_SOURCE_DIR}/specification/generated/R_LANGUAGE_SPECIFICATION_0_1.rules.json"
            --verify
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_rule_inventory.py"
            --specification
            "${CMAKE_CURRENT_SOURCE_DIR}/specification/R_STANDARD_LIBRARY_SPECIFICATION_0_1.en.adoc"
            --inventory
            "${CMAKE_CURRENT_SOURCE_DIR}/specification/generated/R_STANDARD_LIBRARY_SPECIFICATION_0_1.rules.json"
            --verify
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_target_manifest.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_target_manifest.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            --manifest
            "${CMAKE_CURRENT_SOURCE_DIR}/targets/arm64-apple-darwin.freestanding.json"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_target_abi.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            --manifest
            "${CMAKE_CURRENT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            --verify
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_runtime_surface.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            --manifest
            "${CMAKE_CURRENT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            --cc "${CMAKE_C_COMPILER}"
            --verify
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_target_abi.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            --manifest
            "${CMAKE_CURRENT_SOURCE_DIR}/targets/arm64-apple-darwin.freestanding.json"
            --verify
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_named_standard_copy_abi.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            --verify
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_named_standard_move_abi.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            --verify
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_standard_type_registry.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            --verify
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_standard_operation_registry.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            --verify
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
    add_custom_target(regenerate-grammar-manifest
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_grammar.py"
            --specification
            "${CMAKE_CURRENT_SOURCE_DIR}/specification/R_LANGUAGE_SPECIFICATION_0_1.en.adoc"
            --manifest
            "${CMAKE_CURRENT_SOURCE_DIR}/compiler/grammar/annex_a_manifest.json"
            --write
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
    # B2 of the LLVM transition: the runtime surface tables of the LLVM backend.
    add_custom_target(regenerate-runtime-surface
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_runtime_surface.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            --manifest
            "${CMAKE_CURRENT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            --cc "${CMAKE_C_COMPILER}"
            --write
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
    add_custom_target(regenerate-target-abi
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_target_abi.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            --manifest
            "${CMAKE_CURRENT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            --write
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_target_abi.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            --manifest
            "${CMAKE_CURRENT_SOURCE_DIR}/targets/arm64-apple-darwin.freestanding.json"
            --write
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
    add_custom_target(runtime-entry-stack-inventory-check
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_runtime_entry_stack.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            --inventory
            "${CMAKE_CURRENT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.runtime-entry-stack.json"
            --validate-inventory
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
    add_custom_target(library-source-surface-audit
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/audit_library_source_surface.py"
            --compiler "$<TARGET_FILE:r-front>"
            --inventory
            "${CMAKE_CURRENT_SOURCE_DIR}/library/generated/api_inventory/implementation_inventory.json"
            --output
            "${CMAKE_CURRENT_BINARY_DIR}/library-source-surface-audit.json"
        DEPENDS r-front verify_library_inventory
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
    add_custom_target(semantic-slice-audit
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/audit_semantic_slices.py"
            --source "${CMAKE_CURRENT_SOURCE_DIR}/compiler/semantic/semantic.c"
            --output "${CMAKE_CURRENT_BINARY_DIR}/semantic-slice-audit.json"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
    add_custom_target(lowering-rejection-audit
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/audit_lowering_rejections.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            --output "${CMAKE_CURRENT_BINARY_DIR}/lowering-rejection-audit.json"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
    add_custom_target(regenerate-runtime-entry-stack-inventory
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_runtime_entry_stack.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            --inventory
            "${CMAKE_CURRENT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.runtime-entry-stack.json"
            --write-inventory
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
    add_custom_target(standard-math-operations-check
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_standard_math_operations.py"
            --inventory
            "${CMAKE_CURRENT_SOURCE_DIR}/library/generated/api_inventory/implementation_inventory.json"
            --output
            "${CMAKE_CURRENT_SOURCE_DIR}/compiler/source/standard_math_operations.generated.inc"
            --fixture-output
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/audit_std_math_all_operations.r"
            --async-fixture-output
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/audit_async_std_math_all_operations.r"
            --check
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
    if(APPLE)
        add_custom_target(target-toolchain-check
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_target_toolchain.py"
                --manifest
                "${CMAKE_CURRENT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
                --cc "${CMAKE_C_COMPILER}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            VERBATIM
        )
    endif()
    add_custom_target(regenerate-named-standard-copy-abi
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_named_standard_copy_abi.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            --write
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
    add_custom_target(regenerate-named-standard-move-abi
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_named_standard_move_abi.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            --write
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
    add_custom_target(regenerate-standard-type-registry
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_standard_type_registry.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            --write
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
    add_custom_target(regenerate-standard-operation-registry
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_standard_operation_registry.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            --write
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
    add_custom_target(specification-render-check
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_spec_render.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            --asciidoctor "${R_ASCIIDOCTOR_EXECUTABLE}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
    add_custom_target(c-style-check
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_c_style.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
    add_custom_target(production-assertion-check
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_no_production_asserts.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
endif()

if(MSVC)
    target_compile_options(r_frontend PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_frontend PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
    target_compile_options(r_runtime_0_1 PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
    target_compile_options(r_runtime_freestanding PRIVATE
        -ffreestanding
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if(MSVC)
    target_compile_options(r_runtime_0_1 PRIVATE /W4 /WX /permissive-)
    target_compile_options(r_runtime_freestanding PRIVATE /W4 /WX /permissive-)
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_frontend PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(r_frontend PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_compile_options(r_runtime_0_1 PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(r_runtime_0_1 PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    foreach(R_SANITIZED_LIBRARY IN ITEMS r_frontend r_runtime_0_1)
        target_compile_options(${R_SANITIZED_LIBRARY} PRIVATE
            -fsanitize=thread
            -fno-omit-frame-pointer
        )
        target_link_options(${R_SANITIZED_LIBRARY} PUBLIC
            -fsanitize=thread
            -fno-omit-frame-pointer
        )
    endforeach()
endif()

if(APPLE)
    set(R_RUNTIME_DARWIN_BUILD_TESTS ${R_FRONTEND_BUILD_TESTS})
    set(R_RUNTIME_DARWIN_ENABLE_SANITIZERS ${R_FRONTEND_ENABLE_SANITIZERS})
    set(R_RUNTIME_DARWIN_ENABLE_THREAD_SANITIZER ${R_FRONTEND_ENABLE_THREAD_SANITIZER})
    add_subdirectory(runtime/darwin)
    target_link_libraries(r_runtime_0_1 PUBLIC
        r_runtime_darwin_base
        r_runtime_darwin_dns
        r_runtime_darwin_fs_lane
        r_runtime_darwin_io
        r_runtime_darwin_process
        r_runtime_darwin_socket
        r_runtime_darwin_task
        r_runtime_darwin_timer
    )
    target_link_libraries(r_std_io PUBLIC r_runtime_darwin_io r_runtime_darwin_task)
    target_link_libraries(r_std_fs PUBLIC
        r_runtime_darwin_fs_lane
        r_runtime_darwin_io
        r_runtime_darwin_task
    )
    target_link_libraries(r_std_time PUBLIC r_runtime_darwin_timer)
    target_link_libraries(r_std_net PUBLIC
        r_runtime_darwin_dns
        r_runtime_darwin_socket
        r_runtime_darwin_task
    )
    target_link_libraries(r_std_process PUBLIC r_runtime_darwin_process r_runtime_darwin_task)
    target_link_libraries(r_std_signal PUBLIC r_runtime_darwin_process r_runtime_darwin_task)
else()
    target_sources(r_runtime_0_1 PRIVATE runtime/source/panic_abort.c)
endif()

add_executable(r-front compiler/cli/allocation.c compiler/cli/main.c)
target_link_libraries(r-front PRIVATE r_frontend)

if(MSVC)
    target_compile_options(r-front PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r-front PRIVATE -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow)
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r-front PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(r-front PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
endif()

find_program(RE2C_EXECUTABLE re2c)
if(RE2C_EXECUTABLE)
    add_custom_target(regenerate_lexer
        COMMAND "${CMAKE_COMMAND}"
            -DRE2C_EXECUTABLE=${RE2C_EXECUTABLE}
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/compiler/lexer/lexer.re
            -DOUTPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/compiler/lexer/lexer_generated.c
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/regenerate_lexer.cmake"
        VERBATIM
    )
    add_custom_target(verify_generated_lexer
        COMMAND "${CMAKE_COMMAND}"
            -DRE2C_EXECUTABLE=${RE2C_EXECUTABLE}
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/compiler/lexer/lexer.re
            -DOUTPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/compiler/lexer/lexer_generated.c
            -DVERIFY_ONLY=ON
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/regenerate_lexer.cmake"
        VERBATIM
    )
endif()

if(R_FRONTEND_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(R_FRONTEND_ENABLE_FUZZERS)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "R frontend fuzzers require Clang sanitizer instrumentation")
    endif()
    include(CheckCSourceCompiles)
    set(R_FRONTEND_SAVED_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS}")
    set(R_FRONTEND_SAVED_REQUIRED_LINK_OPTIONS "${CMAKE_REQUIRED_LINK_OPTIONS}")
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -fsanitize=fuzzer")
    set(CMAKE_REQUIRED_LINK_OPTIONS "${CMAKE_REQUIRED_LINK_OPTIONS};-fsanitize=fuzzer")
    check_c_source_compiles(
        "#include <stddef.h>\n#include <stdint.h>\nint LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {(void)data; (void)size; return 0;}"
        R_FRONTEND_HAS_LIBFUZZER
    )
    set(CMAKE_REQUIRED_FLAGS "${R_FRONTEND_SAVED_REQUIRED_FLAGS}")
    set(CMAKE_REQUIRED_LINK_OPTIONS "${R_FRONTEND_SAVED_REQUIRED_LINK_OPTIONS}")
    if(R_FRONTEND_HAS_LIBFUZZER)
        target_compile_options(r_frontend PRIVATE -fsanitize=fuzzer-no-link)
        target_compile_options(r_std_json PRIVATE -fsanitize=fuzzer-no-link)
    endif()
    foreach(FUZZER_NAME IN ITEMS lexer parser)
        if(R_FRONTEND_HAS_LIBFUZZER)
            add_executable(r_frontend_fuzz_${FUZZER_NAME} tests/fuzz_${FUZZER_NAME}.c)
        else()
            add_executable(r_frontend_fuzz_${FUZZER_NAME}
                tests/fuzz_${FUZZER_NAME}.c
                tests/fuzz_standalone_main.c
            )
        endif()
        target_link_libraries(r_frontend_fuzz_${FUZZER_NAME} PRIVATE r_frontend)
        target_compile_options(r_frontend_fuzz_${FUZZER_NAME} PRIVATE
            -Wall -Wextra -Wpedantic -Werror
            -Wconversion -Wsign-conversion -Wshadow
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(r_frontend_fuzz_${FUZZER_NAME} PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        if(R_FRONTEND_HAS_LIBFUZZER)
            target_compile_options(r_frontend_fuzz_${FUZZER_NAME} PRIVATE -fsanitize=fuzzer)
            target_link_options(r_frontend_fuzz_${FUZZER_NAME} PRIVATE -fsanitize=fuzzer)
        endif()
        if(R_FRONTEND_BUILD_TESTS)
            add_test(
                NAME r_frontend_fuzz_${FUZZER_NAME}_smoke
                COMMAND r_frontend_fuzz_${FUZZER_NAME} -runs=1000 -max_len=4096
            )
        endif()
    endforeach()
    if(R_FRONTEND_HAS_LIBFUZZER)
        add_executable(r_library_json_fuzz tests/json_fuzz.c)
    else()
        add_executable(r_library_json_fuzz tests/json_fuzz.c tests/fuzz_standalone_main.c)
    endif()
    target_link_libraries(r_library_json_fuzz PRIVATE r_std_json)
    target_compile_options(r_library_json_fuzz PRIVATE
        -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(r_library_json_fuzz PRIVATE -fsanitize=address,undefined)
    if(R_FRONTEND_HAS_LIBFUZZER)
        target_compile_options(r_library_json_fuzz PRIVATE -fsanitize=fuzzer)
        target_link_options(r_library_json_fuzz PRIVATE -fsanitize=fuzzer)
    endif()
    if(R_FRONTEND_BUILD_TESTS)
        add_test(NAME r_library_json_fuzz_smoke COMMAND r_library_json_fuzz -runs=10000 -max_len=4096)
        if(R_FRONTEND_HAS_LIBFUZZER)
            add_test(
                NAME r_fuzzer_dependency_instrumentation
                COMMAND "${CMAKE_COMMAND}"
                    "-DFRONTEND_COMPILE_OPTIONS=$<JOIN:$<TARGET_PROPERTY:r_frontend,COMPILE_OPTIONS>,|>"
                    "-DJSON_COMPILE_OPTIONS=$<JOIN:$<TARGET_PROPERTY:r_std_json,COMPILE_OPTIONS>,|>"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_fuzzer_instrumentation.cmake"
            )
            set_tests_properties(
                r_fuzzer_dependency_instrumentation
                PROPERTIES LABELS "audit;fuzz;regression"
            )
        endif()
    endif()
    if(NOT R_FRONTEND_HAS_LIBFUZZER)
        message(STATUS "libFuzzer runtime unavailable; using standalone byte-fuzz driver")
    endif()
endif()
