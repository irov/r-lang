if(NOT DEFINED R_FRONT_EXECUTABLE OR
   NOT DEFINED C_COMPILER OR
   NOT DEFINED PYTHON_EXECUTABLE OR
   NOT DEFINED TARGET_TOOLCHAIN_CHECK OR
   NOT DEFINED TARGET_MANIFEST OR
   NOT DEFINED OUTPUT_EXE OR
   NOT DEFINED RUNTIME_INCLUDE OR
   NOT DEFINED RUNTIME_DARWIN_INCLUDE OR
   NOT DEFINED R_STD_ALLOC_INCLUDE OR
   NOT DEFINED R_STD_ASYNC_INCLUDE OR
   NOT DEFINED R_STD_C_INCLUDE OR
   NOT DEFINED R_STD_CONVERT_INCLUDE OR
   NOT DEFINED R_STD_ERROR_INCLUDE OR
   NOT DEFINED R_STD_STRING_INCLUDE OR
   NOT DEFINED R_LIBRARY_ROOT OR
   NOT DEFINED R_STD_ARC_LIBRARY OR
   NOT DEFINED R_STD_ALLOC_LIBRARY OR
   NOT DEFINED R_STD_ARRAY_LIBRARY OR
   NOT DEFINED R_STD_LIST_LIBRARY OR
   NOT DEFINED R_STD_DICT_LIBRARY OR
   NOT DEFINED R_STD_BITS_LIBRARY OR
   NOT DEFINED R_STD_BYTES_LIBRARY OR
   NOT DEFINED R_STD_HASH_LIBRARY OR
   NOT DEFINED R_STD_UTF8_LIBRARY OR
   NOT DEFINED R_CORE_LIBRARY OR
   NOT DEFINED R_STD_ASYNC_LIBRARY OR
   NOT DEFINED R_STD_C_LIBRARY OR
   NOT DEFINED R_STD_SECRET_LIBRARY OR
   NOT DEFINED R_STD_RANDOM_LIBRARY OR
   NOT DEFINED R_STD_SIGNAL_LIBRARY OR
   NOT DEFINED R_STD_TEST_LIBRARY OR
   NOT DEFINED R_STD_CONVERT_LIBRARY OR
   NOT DEFINED R_STD_FS_LIBRARY OR
   NOT DEFINED R_STD_IO_LIBRARY OR
   NOT DEFINED R_STD_RC_LIBRARY OR
   NOT DEFINED R_STD_SYNC_LIBRARY OR
   NOT DEFINED R_STD_THREAD_LIBRARY OR
   NOT DEFINED R_STD_TIME_LIBRARY OR
   NOT DEFINED RUNTIME_HOSTED_SOURCE OR
   NOT DEFINED RUNTIME_ALLOCATOR_SOURCE OR
   NOT DEFINED RUNTIME_ARRAY_SOURCE OR
   NOT DEFINED RUNTIME_LIST_SOURCE OR
   NOT DEFINED RUNTIME_DICT_SOURCE OR
   NOT DEFINED RUNTIME_ARC_SOURCE OR
   NOT DEFINED RUNTIME_RC_SOURCE OR
   NOT DEFINED RUNTIME_DROP_SOURCE OR
   NOT DEFINED RUNTIME_OWN_SOURCE OR
   NOT DEFINED RUNTIME_THREAD_ATTACHMENT_SOURCE OR
   NOT DEFINED RUNTIME_TASK_SOURCE OR
   NOT DEFINED RUNTIME_STACK_SOURCE OR
   NOT DEFINED RUNTIME_DARWIN_IO_LIBRARY OR
   NOT DEFINED RUNTIME_DARWIN_PROCESS_LIBRARY OR
   NOT DEFINED RUNTIME_DARWIN_EVENT_LIBRARY OR
   NOT DEFINED RUNTIME_DARWIN_TIMER_LIBRARY OR
   NOT DEFINED RUNTIME_FS_LANE_SOURCE OR
   NOT DEFINED RUNTIME_FS_LANE_ASYNC_SOURCE OR
   NOT DEFINED RUNTIME_FS_LANE_SYSCALLS_SOURCE OR
   NOT DEFINED RUNTIME_FS_SERVICE_SOURCE OR
   NOT DEFINED RUNTIME_STRING_SOURCE OR
   NOT DEFINED RUNTIME_UTF8_SOURCE OR
   NOT DEFINED RUNTIME_PANIC_SOURCE OR
   NOT DEFINED R_RUNTIME_INLINE_SHIMS_LIBRARY)
    message(FATAL_ERROR "missing codegen program test input")
endif()

# The C parts of the program (runtime sources, shims, wrapper and foreign sources) are compiled
# at -O0 for tests; the benchmark pairs compile them as their C mirrors are compiled.
if(NOT DEFINED C_OPTIMIZATION)
    set(C_OPTIMIZATION -O0)
endif()

execute_process(
    COMMAND "${PYTHON_EXECUTABLE}" "${TARGET_TOOLCHAIN_CHECK}"
        --manifest "${TARGET_MANIFEST}"
        --cc "${C_COMPILER}"
    RESULT_VARIABLE R_TOOLCHAIN_RESULT
    OUTPUT_VARIABLE R_TOOLCHAIN_OUTPUT
    ERROR_VARIABLE R_TOOLCHAIN_ERROR
)
if(NOT R_TOOLCHAIN_RESULT EQUAL 0)
    message(FATAL_ERROR
        "target toolchain verification failed (${R_TOOLCHAIN_RESULT}):\n"
        "${R_TOOLCHAIN_OUTPUT}${R_TOOLCHAIN_ERROR}")
endif()

set(R_CODEGEN_PLATFORM_FLAGS)
if(APPLE)
    execute_process(
        COMMAND xcrun --sdk macosx --show-sdk-path
        RESULT_VARIABLE R_SDK_PATH_RESULT
        OUTPUT_VARIABLE R_SDK_PATH
        ERROR_VARIABLE R_SDK_PATH_ERROR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT R_SDK_PATH_RESULT EQUAL 0 OR R_SDK_PATH STREQUAL "")
        message(FATAL_ERROR
            "could not resolve the macOS SDK path (${R_SDK_PATH_RESULT}):\n"
            "${R_SDK_PATH_ERROR}")
    endif()
    list(APPEND R_CODEGEN_PLATFORM_FLAGS -isysroot "${R_SDK_PATH}")
endif()

# The symbols of the program object answer what the generated C text answered before B6: which
# native providers the program imports and which library entries it calls.
get_filename_component(R_CODEGEN_TOOL_DIRECTORY "${C_COMPILER}" DIRECTORY)
set(R_CODEGEN_NM "${R_CODEGEN_TOOL_DIRECTORY}/llvm-nm")
if(NOT EXISTS "${R_CODEGEN_NM}")
    message(FATAL_ERROR "llvm-nm of the pinned toolchain is not found next to ${C_COMPILER}")
endif()

if((NOT DEFINED SOURCE_1 OR SOURCE_1 STREQUAL "") AND
   (NOT DEFINED MODULE_MAP OR MODULE_MAP STREQUAL "" OR
    NOT DEFINED ENTRY OR ENTRY STREQUAL ""))
    message(FATAL_ERROR "codegen program test requires sources or a module-map entry")
endif()

set(R_FRONTEND_ARGUMENTS --emit=object)
if(DEFINED MODULE_MAP AND NOT MODULE_MAP STREQUAL "")
    list(APPEND R_FRONTEND_ARGUMENTS --module-map "${MODULE_MAP}")
endif()
if(DEFINED ENTRY AND NOT ENTRY STREQUAL "")
    list(APPEND R_FRONTEND_ARGUMENTS --entry "${ENTRY}")
endif()
if(DEFINED LIBRARY_MAP AND NOT LIBRARY_MAP STREQUAL "")
    list(APPEND R_FRONTEND_ARGUMENTS --library-map "${LIBRARY_MAP}")
endif()
if(DEFINED PROFILE AND NOT PROFILE STREQUAL "")
    list(APPEND R_FRONTEND_ARGUMENTS --profile "${PROFILE}")
endif()
if(DEFINED TARGET_MANIFEST AND NOT TARGET_MANIFEST STREQUAL "")
    list(APPEND R_FRONTEND_ARGUMENTS --target-manifest "${TARGET_MANIFEST}")
endif()
if(DEFINED LINK_MANIFEST AND NOT LINK_MANIFEST STREQUAL "")
    list(APPEND R_FRONTEND_ARGUMENTS --link-manifest "${LINK_MANIFEST}")
endif()
if(DEFINED SOURCE_1 AND NOT SOURCE_1 STREQUAL "")
    list(APPEND R_FRONTEND_ARGUMENTS "${SOURCE_1}")
endif()
if(DEFINED SOURCE_2 AND NOT SOURCE_2 STREQUAL "")
    list(APPEND R_FRONTEND_ARGUMENTS "${SOURCE_2}")
endif()
# R-FUNC-0025 (M24): translate the entry module in test mode; appended last so that the
# argument positions above stay as they are.
if(TEST_MODE)
    list(APPEND R_FRONTEND_ARGUMENTS --test)
endif()
# B7.2: the bitcode of the runtime and library C the program may be optimized with, and further
# options of a test (profile-guided optimization, tests/check_pgo_program.cmake).
if(DEFINED BITCODE_CATALOG AND NOT BITCODE_CATALOG STREQUAL "")
    list(APPEND R_FRONTEND_ARGUMENTS --bitcode-catalog "${BITCODE_CATALOG}")
endif()
if(DEFINED FRONTEND_EXTRA_ARGUMENTS AND NOT FRONTEND_EXTRA_ARGUMENTS STREQUAL "")
    list(APPEND R_FRONTEND_ARGUMENTS ${FRONTEND_EXTRA_ARGUMENTS})
endif()

set(R_CODEGEN_SANITIZER_FLAGS)
if(DEFINED ENABLE_SANITIZERS AND ENABLE_SANITIZERS)
    list(APPEND R_CODEGEN_SANITIZER_FLAGS
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
elseif(DEFINED ENABLE_THREAD_SANITIZER AND ENABLE_THREAD_SANITIZER)
    set(R_CODEGEN_SANITIZER_FLAGS
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

set(R_CODEGEN_EXTRA_C_SOURCES)
if(DEFINED EXTRA_C_SOURCES AND NOT EXTRA_C_SOURCES STREQUAL "")
    set(R_CODEGEN_EXTRA_C_SOURCES ${EXTRA_C_SOURCES})
endif()
set(R_CODEGEN_HEADER_INCLUDE_FLAGS)
if(DEFINED HEADER_INCLUDE_DIRS AND NOT HEADER_INCLUDE_DIRS STREQUAL "")
    foreach(R_CODEGEN_HEADER_DIR IN LISTS HEADER_INCLUDE_DIRS)
        list(APPEND R_CODEGEN_HEADER_INCLUDE_FLAGS "-I${R_CODEGEN_HEADER_DIR}")
    endforeach()
endif()

if(DEFINED ABI_INVENTORY AND ABI_INVENTORY)
    # R-FFI-0041: complete C aggregates need the record produced from the verified headers.
    include("${CMAKE_CURRENT_LIST_DIR}/abi_record_support.cmake")
    r_abi_generate_record(
        FRONT "${R_FRONT_EXECUTABLE}"
        PYTHON "${PYTHON_EXECUTABLE}"
        TOOL "${ABI_RECORD_TOOL}"
        CC "${C_COMPILER}"
        OUTPUT_PREFIX "${OUTPUT_EXE}"
        ARGUMENTS_VARIABLE R_FRONTEND_ARGUMENTS
        INCLUDE_FLAGS ${R_CODEGEN_HEADER_INCLUDE_FLAGS}
        SYSROOT "${R_SDK_PATH}")
endif()

# B5.4: a C wrapper hooks runtime and library entries with macros before it includes
# R_TEST_PROGRAM_PRELUDE, the runtime and library headers the program is compiled against. The
# object takes the same renames (--rename-symbol); the inline functions of the headers, which the
# object calls through shims, are compiled here with the same macros.
set(R_CODEGEN_OBJECT "${OUTPUT_EXE}.o")
set(R_CODEGEN_WRAPPER_FLAGS)
set(R_CODEGEN_SHIMS)
if(DEFINED C_WRAPPER AND NOT C_WRAPPER STREQUAL "")
    file(READ "${C_WRAPPER}" R_CODEGEN_WRAPPER_TEXT)
    # A wrapper may be another wrapper with a different entry around it.
    if(R_CODEGEN_WRAPPER_TEXT MATCHES "#include \"(codegen_[a-z0-9_]+_wrapper[.]c)\"")
        get_filename_component(R_CODEGEN_WRAPPER_DIRECTORY "${C_WRAPPER}" DIRECTORY)
        file(READ "${R_CODEGEN_WRAPPER_DIRECTORY}/${CMAKE_MATCH_1}" R_CODEGEN_WRAPPER_INCLUDED)
        string(APPEND R_CODEGEN_WRAPPER_TEXT "${R_CODEGEN_WRAPPER_INCLUDED}")
    endif()
    string(FIND "${R_CODEGEN_WRAPPER_TEXT}" "#include R_TEST_PROGRAM_PRELUDE"
           R_CODEGEN_WRAPPER_AT)
    if(R_CODEGEN_WRAPPER_AT LESS 0)
        message(FATAL_ERROR "the C wrapper does not include R_TEST_PROGRAM_PRELUDE")
    endif()
    string(SUBSTRING "${R_CODEGEN_WRAPPER_TEXT}" 0 ${R_CODEGEN_WRAPPER_AT} R_CODEGEN_WRAPPER_HEAD)
    string(REGEX MATCHALL "#define[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]+[A-Za-z_][A-Za-z0-9_]*"
           R_CODEGEN_WRAPPER_DEFINES "${R_CODEGEN_WRAPPER_HEAD}")
    set(R_CODEGEN_SHIMS_TEXT "/* The runtime shims of a program with the renames of its wrapper. */\n")
    foreach(R_CODEGEN_WRAPPER_DEFINE IN LISTS R_CODEGEN_WRAPPER_DEFINES)
        string(REGEX REPLACE "#define[ \t]+([A-Za-z0-9_]+)[ \t]+([A-Za-z0-9_]+)" "\\1=\\2"
               R_CODEGEN_RENAME "${R_CODEGEN_WRAPPER_DEFINE}")
        list(APPEND R_FRONTEND_ARGUMENTS --rename-symbol "${R_CODEGEN_RENAME}")
        if(NOT R_CODEGEN_RENAME MATCHES "^main=")
            string(APPEND R_CODEGEN_SHIMS_TEXT "${R_CODEGEN_WRAPPER_DEFINE}\n")
            set(R_CODEGEN_SHIMS "${OUTPUT_EXE}.shims.c")
        endif()
    endforeach()
    if(R_CODEGEN_SHIMS)
        string(APPEND R_CODEGEN_SHIMS_TEXT
               "#include \"${RUNTIME_INCLUDE}/../llvm/inline_shims.generated.c\"\n")
        file(WRITE "${R_CODEGEN_SHIMS}" "${R_CODEGEN_SHIMS_TEXT}")
    endif()
    list(APPEND R_CODEGEN_EXTRA_C_SOURCES "${C_WRAPPER}")
    set(R_CODEGEN_WRAPPER_FLAGS
        "-DR_TEST_PROGRAM_PRELUDE=\"${CMAKE_CURRENT_LIST_DIR}/codegen_program_prelude.h\""
        "-I${R_LIBRARY_ROOT}/core/include")
    file(GLOB R_CODEGEN_LIBRARY_INCLUDE_DIRS LIST_DIRECTORIES true
         "${R_LIBRARY_ROOT}/std/*/include")
    foreach(R_CODEGEN_LIBRARY_INCLUDE_DIR IN LISTS R_CODEGEN_LIBRARY_INCLUDE_DIRS)
        list(APPEND R_CODEGEN_WRAPPER_FLAGS "-I${R_CODEGEN_LIBRARY_INCLUDE_DIR}")
    endforeach()
endif()

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" ${R_FRONTEND_ARGUMENTS}
    RESULT_VARIABLE R_EMIT_RESULT
    OUTPUT_FILE "${R_CODEGEN_OBJECT}"
    ERROR_VARIABLE R_EMIT_ERROR
)
if(NOT R_EMIT_RESULT EQUAL 0)
    message(FATAL_ERROR "r-front --emit=object failed (${R_EMIT_RESULT}): ${R_EMIT_ERROR}")
endif()
execute_process(
    COMMAND "${R_CODEGEN_NM}" --undefined-only --format=just-symbols "${R_CODEGEN_OBJECT}"
    RESULT_VARIABLE R_CODEGEN_NM_RESULT
    OUTPUT_VARIABLE R_CODEGEN_IMPORTS
    ERROR_VARIABLE R_CODEGEN_NM_ERROR
)
if(NOT R_CODEGEN_NM_RESULT EQUAL 0)
    message(FATAL_ERROR "llvm-nm failed (${R_CODEGEN_NM_RESULT}): ${R_CODEGEN_NM_ERROR}")
endif()

if(HASH_ONLY)
    if(NOT R_CODEGEN_IMPORTS MATCHES "(^|\n)_r_std_hash_" OR
       R_CODEGEN_IMPORTS MATCHES "(^|\n)_(r_std_bytes_|r_runtime_array_)")
        message(FATAL_ERROR "standalone hash program has incorrect module dependencies")
    endif()
    file(READ "${SOURCE_1}" R_HASH_SOURCE)
    string(REGEX MATCH "module ([A-Za-z0-9_.]+);" R_HASH_MODULE "${R_HASH_SOURCE}")
    set(R_HASH_PLAN_ARGUMENTS --entry "${CMAKE_MATCH_1}::main" ${R_FRONTEND_ARGUMENTS})
    list(REMOVE_AT R_HASH_PLAN_ARGUMENTS 2)
    execute_process(
        COMMAND "${R_FRONT_EXECUTABLE}" --emit=link-plan ${R_HASH_PLAN_ARGUMENTS}
        RESULT_VARIABLE R_HASH_PLAN_RESULT
        OUTPUT_VARIABLE R_HASH_PLAN
        ERROR_VARIABLE R_HASH_PLAN_ERROR
    )
    if(NOT R_HASH_PLAN_RESULT EQUAL 0 OR
       NOT R_HASH_PLAN MATCHES "module=\"std[.]hash\" target=\"r_std_hash\"" OR
       R_HASH_PLAN MATCHES "std[.]bytes|r_std_bytes")
        message(FATAL_ERROR "standalone hash link plan: ${R_HASH_PLAN}${R_HASH_PLAN_ERROR}")
    endif()
    # The executable must link successfully with no bytes library on the command line.
    set(R_STD_BYTES_LIBRARY "")
endif()
if(R_CODEGEN_IMPORTS MATCHES "(^|\n)_+(sig)?(setjmp|longjmp)(\n|$)")
    message(FATAL_ERROR "the program uses a forbidden setjmp/longjmp checked-error primitive")
endif()

# R-FUNC-0004: the emitter measures the frame of every function it defines and keeps the
# measurement in the r.stack.frames metadata of the IR (function, frame, @recursion depth, bound
# below a call into it). A fixture may bound every frame (STACK_FRAME_LIMIT); STACK_REPORT keeps
# the IR next to the executable for a driver that reads the bounds.
set(R_CODEGEN_STACK_IR "${OUTPUT_EXE}.ll")
if((DEFINED STACK_FRAME_LIMIT AND NOT STACK_FRAME_LIMIT STREQUAL "") OR
   (DEFINED STACK_REPORT AND STACK_REPORT))
    set(R_CODEGEN_IR_ARGUMENTS ${R_FRONTEND_ARGUMENTS})
    list(REMOVE_AT R_CODEGEN_IR_ARGUMENTS 0)
    execute_process(
        COMMAND "${R_FRONT_EXECUTABLE}" --emit=llvm-ir ${R_CODEGEN_IR_ARGUMENTS}
        RESULT_VARIABLE R_CODEGEN_IR_RESULT
        OUTPUT_FILE "${R_CODEGEN_STACK_IR}"
        ERROR_VARIABLE R_CODEGEN_IR_ERROR
    )
    if(NOT R_CODEGEN_IR_RESULT EQUAL 0)
        message(FATAL_ERROR
            "r-front --emit=llvm-ir failed (${R_CODEGEN_IR_RESULT}): ${R_CODEGEN_IR_ERROR}")
    endif()
endif()
if(DEFINED STACK_FRAME_LIMIT AND NOT STACK_FRAME_LIMIT STREQUAL "")
    file(STRINGS "${R_CODEGEN_STACK_IR}" R_CODEGEN_FRAMES
         REGEX "^![0-9]+ = !{!\"[^\"]*\", i64 [0-9]+, i64 [0-9]+, i64 [0-9]+}$")
    if(NOT R_CODEGEN_FRAMES)
        message(FATAL_ERROR "the IR carries no r.stack.frames measurement")
    endif()
    foreach(R_CODEGEN_FRAME IN LISTS R_CODEGEN_FRAMES)
        string(REGEX REPLACE "^![0-9]+ = !{!\"([^\"]*)\", i64 ([0-9]+), .*$" "\\1"
               R_CODEGEN_FRAME_NAME "${R_CODEGEN_FRAME}")
        string(REGEX REPLACE "^![0-9]+ = !{!\"[^\"]*\", i64 ([0-9]+), .*$" "\\1"
               R_CODEGEN_FRAME_SIZE "${R_CODEGEN_FRAME}")
        if(R_CODEGEN_FRAME_SIZE GREATER STACK_FRAME_LIMIT)
            message(FATAL_ERROR
                "${R_CODEGEN_FRAME_NAME} has a stack frame of ${R_CODEGEN_FRAME_SIZE} bytes, "
                "above the limit of ${STACK_FRAME_LIMIT}")
        endif()
    endforeach()
endif()

# Library R-SLIB-RSRC-0003 (M25): a standard module written in R that imports the C functions of
# a native provider of the library, such as std.tls, is verified against the header of the
# provider and bridged like any extern "C" import (Core R-FFI-0042); the archives of the provider
# follow the generated code. With ABI_VERIFY the verifier and the bridge below cover these
# imports together with those of the program.
set(R_CODEGEN_STD_NATIVE_ARCHIVES)
set(R_CODEGEN_STD_NATIVE OFF)
if(R_CODEGEN_IMPORTS MATCHES "(^|\n)_r_bridge_r_std_[a-z]+_native_")
    if(NOT DEFINED STD_NATIVE_INCLUDE_DIRS OR STD_NATIVE_INCLUDE_DIRS STREQUAL "")
        message(FATAL_ERROR "the program imports a native provider of the library without "
                            "STD_NATIVE_INCLUDE_DIRS")
    endif()
    foreach(R_STD_NATIVE_DIR IN LISTS STD_NATIVE_INCLUDE_DIRS)
        list(APPEND R_CODEGEN_HEADER_INCLUDE_FLAGS "-I${R_STD_NATIVE_DIR}")
    endforeach()
    set(R_CODEGEN_STD_NATIVE_ARCHIVES ${STD_NATIVE_LIBRARIES})
    set(R_CODEGEN_STD_NATIVE ON)
endif()

if(R_CODEGEN_STD_NATIVE AND NOT (DEFINED ABI_VERIFY AND ABI_VERIFY))
    set(R_STD_NATIVE_ARGUMENTS ${R_FRONTEND_ARGUMENTS})
    list(REMOVE_AT R_STD_NATIVE_ARGUMENTS 0)
    set(R_STD_NATIVE_VERIFIER_C "${OUTPUT_EXE}.native-verifier.c")
    set(R_STD_NATIVE_BRIDGE_C "${OUTPUT_EXE}.native-bridge.c")
    execute_process(
        COMMAND "${R_FRONT_EXECUTABLE}" --emit=abi-verifier ${R_STD_NATIVE_ARGUMENTS}
        RESULT_VARIABLE R_STD_NATIVE_EMIT_RESULT
        OUTPUT_FILE "${R_STD_NATIVE_VERIFIER_C}"
        ERROR_VARIABLE R_STD_NATIVE_EMIT_ERROR
    )
    if(NOT R_STD_NATIVE_EMIT_RESULT EQUAL 0)
        message(FATAL_ERROR "r-front --emit=abi-verifier failed (${R_STD_NATIVE_EMIT_RESULT}): "
                            "${R_STD_NATIVE_EMIT_ERROR}")
    endif()
    execute_process(
        COMMAND "${C_COMPILER}"
            ${R_CODEGEN_PLATFORM_FLAGS}
            -std=c17
            -pedantic-errors
            -Wall
            -Wextra
            -Werror
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wstrict-prototypes
            -Wmissing-prototypes
            ${R_CODEGEN_HEADER_INCLUDE_FLAGS}
            -fsyntax-only
            "${R_STD_NATIVE_VERIFIER_C}"
        RESULT_VARIABLE R_STD_NATIVE_VERIFY_RESULT
        OUTPUT_VARIABLE R_STD_NATIVE_VERIFY_OUTPUT
        ERROR_VARIABLE R_STD_NATIVE_VERIFY_ERROR
    )
    if(NOT R_STD_NATIVE_VERIFY_RESULT EQUAL 0)
        message(FATAL_ERROR
            "R-DIAG-FFI-004 [R-FFI-0042]: header verification rejected the imports of a native "
            "provider of the library (${R_STD_NATIVE_VERIFY_RESULT}):\n"
            "${R_STD_NATIVE_VERIFY_OUTPUT}${R_STD_NATIVE_VERIFY_ERROR}")
    endif()
    execute_process(
        COMMAND "${R_FRONT_EXECUTABLE}" --emit=c17-bridge ${R_STD_NATIVE_ARGUMENTS}
        RESULT_VARIABLE R_STD_NATIVE_BRIDGE_RESULT
        OUTPUT_FILE "${R_STD_NATIVE_BRIDGE_C}"
        ERROR_VARIABLE R_STD_NATIVE_BRIDGE_ERROR
    )
    if(NOT R_STD_NATIVE_BRIDGE_RESULT EQUAL 0)
        message(FATAL_ERROR "r-front --emit=c17-bridge failed (${R_STD_NATIVE_BRIDGE_RESULT}): "
                            "${R_STD_NATIVE_BRIDGE_ERROR}")
    endif()
    list(APPEND R_CODEGEN_EXTRA_C_SOURCES "${R_STD_NATIVE_BRIDGE_C}")
endif()

if(DEFINED ABI_VERIFY AND ABI_VERIFY)
    # R-FFI-0042: the verifier translation unit is compiled for the target with the C options of
    # the bridge and never executed; a failed compile is the R-DIAG-FFI-004 outcome.
    set(R_ABI_ARGUMENTS ${R_FRONTEND_ARGUMENTS})
    list(REMOVE_AT R_ABI_ARGUMENTS 0)
    set(R_ABI_VERIFIER_C "${OUTPUT_EXE}.abi-verifier.c")
    set(R_ABI_BRIDGE_C "${OUTPUT_EXE}.bridge.c")
    execute_process(
        COMMAND "${R_FRONT_EXECUTABLE}" --emit=abi-verifier ${R_ABI_ARGUMENTS}
        RESULT_VARIABLE R_ABI_EMIT_RESULT
        OUTPUT_FILE "${R_ABI_VERIFIER_C}"
        ERROR_VARIABLE R_ABI_EMIT_ERROR
    )
    if(NOT R_ABI_EMIT_RESULT EQUAL 0)
        message(FATAL_ERROR
            "r-front --emit=abi-verifier failed (${R_ABI_EMIT_RESULT}): ${R_ABI_EMIT_ERROR}")
    endif()
    execute_process(
        COMMAND "${C_COMPILER}"
            ${R_CODEGEN_PLATFORM_FLAGS}
            -std=c17
            -pedantic-errors
            -Wall
            -Wextra
            -Werror
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wstrict-prototypes
            -Wmissing-prototypes
            ${R_CODEGEN_HEADER_INCLUDE_FLAGS}
            -fsyntax-only
            "${R_ABI_VERIFIER_C}"
        RESULT_VARIABLE R_ABI_VERIFY_RESULT
        OUTPUT_VARIABLE R_ABI_VERIFY_OUTPUT
        ERROR_VARIABLE R_ABI_VERIFY_ERROR
    )
    if(NOT R_ABI_VERIFY_RESULT EQUAL 0)
        message(FATAL_ERROR
            "R-DIAG-FFI-004 [R-FFI-0042]: header verification rejected the extern C "
            "imports (${R_ABI_VERIFY_RESULT}):\n${R_ABI_VERIFY_OUTPUT}${R_ABI_VERIFY_ERROR}")
    endif()
    execute_process(
        COMMAND "${R_FRONT_EXECUTABLE}" --emit=c17-bridge ${R_ABI_ARGUMENTS}
        RESULT_VARIABLE R_ABI_BRIDGE_RESULT
        OUTPUT_FILE "${R_ABI_BRIDGE_C}"
        ERROR_VARIABLE R_ABI_BRIDGE_ERROR
    )
    if(NOT R_ABI_BRIDGE_RESULT EQUAL 0)
        message(FATAL_ERROR
            "r-front --emit=c17-bridge failed (${R_ABI_BRIDGE_RESULT}): ${R_ABI_BRIDGE_ERROR}")
    endif()
    list(APPEND R_CODEGEN_EXTRA_C_SOURCES "${R_ABI_BRIDGE_C}")
endif()

# The inline functions of the headers that the object calls by their shims.
if(R_CODEGEN_SHIMS)
    list(APPEND R_CODEGEN_EXTRA_C_SOURCES "${R_CODEGEN_SHIMS}")
else()
    list(APPEND R_CODEGEN_EXTRA_C_SOURCES "${R_RUNTIME_INLINE_SHIMS_LIBRARY}")
endif()

if(DEFINED ENABLE_SANITIZERS AND ENABLE_SANITIZERS AND
   DEFINED EXTRA_C_SOURCES AND NOT EXTRA_C_SOURCES STREQUAL "")
    # The C libraries a fixture imports stand for foreign code that its own build compiles.
    # The function sanitizer compares the C++ names of the caller's and the callee's function
    # types, and across the FFI boundary those differ even where the C types are compatible (an
    # enum and its integer type) or the layouts are proven equal (R-FFI-0021, R-FFI-0041). That
    # boundary is defined by the C ABI, so the foreign objects carry no function-type signature
    # and check no indirect calls; address and the other undefined-behaviour checks stay on.
    set(R_CODEGEN_FOREIGN_INDEX 0)
    foreach(R_CODEGEN_FOREIGN_SOURCE IN LISTS EXTRA_C_SOURCES)
        set(R_CODEGEN_FOREIGN_OBJECT "${OUTPUT_EXE}.foreign${R_CODEGEN_FOREIGN_INDEX}.o")
        execute_process(
            COMMAND "${C_COMPILER}"
                ${R_CODEGEN_PLATFORM_FLAGS}
                -std=c17
                -pedantic-errors
                -Wall
                -Wextra
                -Werror
                ${C_OPTIMIZATION}
                ${R_CODEGEN_SANITIZER_FLAGS}
                -fno-sanitize=function
                ${R_CODEGEN_HEADER_INCLUDE_FLAGS}
                -c "${R_CODEGEN_FOREIGN_SOURCE}"
                -o "${R_CODEGEN_FOREIGN_OBJECT}"
            RESULT_VARIABLE R_CODEGEN_FOREIGN_RESULT
            ERROR_VARIABLE R_CODEGEN_FOREIGN_ERROR
        )
        if(NOT R_CODEGEN_FOREIGN_RESULT EQUAL 0)
            message(FATAL_ERROR
                "foreign C source failed to compile (${R_CODEGEN_FOREIGN_RESULT}): "
                "${R_CODEGEN_FOREIGN_SOURCE}\n${R_CODEGEN_FOREIGN_ERROR}")
        endif()
        list(FIND R_CODEGEN_EXTRA_C_SOURCES "${R_CODEGEN_FOREIGN_SOURCE}" R_CODEGEN_FOREIGN_AT)
        list(REMOVE_AT R_CODEGEN_EXTRA_C_SOURCES ${R_CODEGEN_FOREIGN_AT})
        list(INSERT R_CODEGEN_EXTRA_C_SOURCES ${R_CODEGEN_FOREIGN_AT} "${R_CODEGEN_FOREIGN_OBJECT}")
        math(EXPR R_CODEGEN_FOREIGN_INDEX "${R_CODEGEN_FOREIGN_INDEX} + 1")
    endforeach()
endif()

execute_process(
    COMMAND "${C_COMPILER}"
        ${R_CODEGEN_PLATFORM_FLAGS}
        -std=c17
        -pedantic-errors
        -Wall
        -Wextra
        -Werror
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wstrict-prototypes
        -Wmissing-prototypes
        ${C_OPTIMIZATION}
        ${R_CODEGEN_SANITIZER_FLAGS}
        ${R_CODEGEN_WRAPPER_FLAGS}
        ${R_CODEGEN_HEADER_INCLUDE_FLAGS}
        "-I${RUNTIME_INCLUDE}"
        "-I${RUNTIME_DARWIN_INCLUDE}"
        "-I${R_LIBRARY_ROOT}/core/include"
        "-I${R_STD_ALLOC_INCLUDE}"
        "-I${R_LIBRARY_ROOT}/std/arc/include"
        "-I${R_LIBRARY_ROOT}/std/array/include"
        "-I${R_STD_ASYNC_INCLUDE}"
        "-I${R_LIBRARY_ROOT}/std/bits/include"
        "-I${R_LIBRARY_ROOT}/std/bytes/include"
        "-I${R_LIBRARY_ROOT}/std/hash/include"
        "-I${R_LIBRARY_ROOT}/std/secret/include"
        "-I${R_LIBRARY_ROOT}/std/random/include"
        "-I${R_LIBRARY_ROOT}/std/signal/include"
        "-I${R_LIBRARY_ROOT}/std/test/include"
        "-I${R_STD_C_INCLUDE}"
        "-I${R_STD_CONVERT_INCLUDE}"
        "-I${R_LIBRARY_ROOT}/std/dict/include"
        "-I${R_LIBRARY_ROOT}/std/env/include"
        "-I${R_STD_ERROR_INCLUDE}"
        "-I${R_LIBRARY_ROOT}/std/format/include"
            "-I${R_LIBRARY_ROOT}/std/json/include"
        "-I${R_LIBRARY_ROOT}/std/fs/include"
        "-I${R_LIBRARY_ROOT}/std/io/include"
        "-I${R_LIBRARY_ROOT}/std/list/include"
        "-I${R_LIBRARY_ROOT}/std/math/include"
        "-I${R_LIBRARY_ROOT}/std/net/include"
        "-I${R_LIBRARY_ROOT}/std/process/include"
        "-I${R_LIBRARY_ROOT}/std/rc/include"
        "-I${R_STD_STRING_INCLUDE}"
        "-I${R_LIBRARY_ROOT}/std/sync/include"
        "-I${R_LIBRARY_ROOT}/std/thread/include"
        "-I${R_LIBRARY_ROOT}/std/time/include"
        "-I${R_LIBRARY_ROOT}/std/utf8/include"
        "${R_CODEGEN_OBJECT}"
        ${R_CODEGEN_EXTRA_C_SOURCES}
        ${R_CODEGEN_STD_NATIVE_ARCHIVES}
        "${R_STD_ALLOC_LIBRARY}"
        "${R_STD_ARC_LIBRARY}"
        "${R_STD_ARRAY_LIBRARY}"
        "${R_STD_LIST_LIBRARY}"
        "${R_STD_DICT_LIBRARY}"
        "${R_STD_BYTES_LIBRARY}"
        "${R_STD_HASH_LIBRARY}"
        "${R_STD_SECRET_LIBRARY}"
        "${R_STD_RANDOM_LIBRARY}"
        "${R_STD_MATH_LIBRARY}"
        "${R_STD_FORMAT_LIBRARY}"
        "${R_STD_JSON_LIBRARY}"
        "${R_STD_NET_LIBRARY}"
        "${R_STD_SIGNAL_LIBRARY}"
        "${R_STD_TEST_LIBRARY}"
        "${R_STD_PROCESS_LIBRARY}"
        "${R_STD_STRING_LIBRARY}"
        "${R_STD_UTF8_LIBRARY}"
        "${R_STD_BITS_LIBRARY}"
        "${R_CORE_LIBRARY}"
        "${R_STD_ASYNC_LIBRARY}"
        "${R_STD_C_LIBRARY}"
        "${R_STD_CONVERT_LIBRARY}"
        "${R_STD_ENV_LIBRARY}"
        "${R_STD_ERROR_LIBRARY}"
        "${R_STD_FS_LIBRARY}"
        "${R_STD_IO_LIBRARY}"
        "${R_STD_TIME_LIBRARY}"
        "${R_STD_RC_LIBRARY}"
        "${R_STD_SYNC_LIBRARY}"
        "${R_STD_THREAD_LIBRARY}"
        "${RUNTIME_ALLOCATOR_SOURCE}"
        "${RUNTIME_ARRAY_SOURCE}"
        "${RUNTIME_LIST_SOURCE}"
        "${RUNTIME_DICT_SOURCE}"
        "${RUNTIME_ARC_SOURCE}"
        "${RUNTIME_RC_SOURCE}"
        "${RUNTIME_DROP_SOURCE}"
        "${RUNTIME_OWN_SOURCE}"
        "${RUNTIME_HOSTED_SOURCE}"
        "${RUNTIME_THREAD_ATTACHMENT_SOURCE}"
        "${RUNTIME_TASK_SOURCE}"
        "${RUNTIME_STACK_SOURCE}"
        "${RUNTIME_DARWIN_SOCKET_LIBRARY}"
        "${RUNTIME_DARWIN_DNS_LIBRARY}"
        "${RUNTIME_DARWIN_IO_LIBRARY}"
        "${RUNTIME_DARWIN_PROCESS_LIBRARY}"
        "${RUNTIME_FS_LANE_SOURCE}"
        "${RUNTIME_FS_LANE_ASYNC_SOURCE}"
        "${RUNTIME_FS_LANE_SYSCALLS_SOURCE}"
        "${RUNTIME_FS_SERVICE_SOURCE}"
        "${RUNTIME_STRING_SOURCE}"
        "${RUNTIME_UTF8_SOURCE}"
        "${RUNTIME_PANIC_SOURCE}"
        "${RUNTIME_DARWIN_EVENT_LIBRARY}"
        "${RUNTIME_DARWIN_TIMER_LIBRARY}"
        ${LINK_EXTRA_FLAGS}
        -o "${OUTPUT_EXE}"
    RESULT_VARIABLE R_COMPILE_RESULT
    OUTPUT_VARIABLE R_COMPILE_OUTPUT
    ERROR_VARIABLE R_COMPILE_ERROR
)
if(NOT R_COMPILE_RESULT EQUAL 0)
    message(FATAL_ERROR
        "the program did not link (${R_COMPILE_RESULT}):\n"
        "${R_COMPILE_OUTPUT}${R_COMPILE_ERROR}")
endif()

if(DEFINED COMPILE_ONLY AND COMPILE_ONLY)
    if(NOT EXISTS "${OUTPUT_EXE}" OR IS_DIRECTORY "${OUTPUT_EXE}")
        message(FATAL_ERROR
            "the link reported success but produced no executable: "
            "${OUTPUT_EXE}")
    endif()
    file(SIZE "${OUTPUT_EXE}" R_CODEGEN_EXECUTABLE_SIZE)
    if(R_CODEGEN_EXECUTABLE_SIZE EQUAL 0)
        message(FATAL_ERROR
            "the link produced an empty executable: ${OUTPUT_EXE}")
    endif()
    return()
endif()

set(R_CODEGEN_CREATED_DIRECTORY_ROOT "")
set(R_CODEGEN_CREATED_DIRECTORY_PATH "")
set(R_CODEGEN_ATOMIC_WRITE_ROOT "")
set(R_CODEGEN_ATOMIC_WRITE_PATH "")
if(DEFINED CREATE_DIRECTORY_RELATIVE AND NOT CREATE_DIRECTORY_RELATIVE STREQUAL "" AND
   DEFINED ATOMIC_WRITE_RELATIVE AND NOT ATOMIC_WRITE_RELATIVE STREQUAL "")
    message(FATAL_ERROR
        "CREATE_DIRECTORY_RELATIVE and ATOMIC_WRITE_RELATIVE are mutually exclusive")
endif()
if(DEFINED CREATE_DIRECTORY_RELATIVE AND NOT CREATE_DIRECTORY_RELATIVE STREQUAL "")
    if(IS_ABSOLUTE "${CREATE_DIRECTORY_RELATIVE}" OR
       CREATE_DIRECTORY_RELATIVE MATCHES "(^|/)\\.\\.(/|$)")
        message(FATAL_ERROR
            "CREATE_DIRECTORY_RELATIVE must be a non-escaping relative path")
    endif()
    set(R_CODEGEN_CREATED_DIRECTORY_ROOT "${OUTPUT_EXE}.root")
    set(R_CODEGEN_CREATED_DIRECTORY_PATH
        "${R_CODEGEN_CREATED_DIRECTORY_ROOT}/${CREATE_DIRECTORY_RELATIVE}")
    file(REMOVE_RECURSE "${R_CODEGEN_CREATED_DIRECTORY_ROOT}")
    file(MAKE_DIRECTORY "${R_CODEGEN_CREATED_DIRECTORY_ROOT}")
endif()
if(DEFINED ATOMIC_WRITE_RELATIVE AND NOT ATOMIC_WRITE_RELATIVE STREQUAL "")
    if(NOT DEFINED ATOMIC_WRITE_SOURCE OR ATOMIC_WRITE_SOURCE STREQUAL "" OR
       NOT EXISTS "${ATOMIC_WRITE_SOURCE}" OR IS_DIRECTORY "${ATOMIC_WRITE_SOURCE}")
        message(FATAL_ERROR "ATOMIC_WRITE_SOURCE must name an existing regular file")
    endif()
    if(IS_ABSOLUTE "${ATOMIC_WRITE_RELATIVE}" OR
       ATOMIC_WRITE_RELATIVE MATCHES "(^|/)\\.\\.(/|$)")
        message(FATAL_ERROR "ATOMIC_WRITE_RELATIVE must be a non-escaping relative path")
    endif()
    if(NOT DEFINED ATOMIC_WRITE_REPLACEMENT_SOURCE OR
       ATOMIC_WRITE_REPLACEMENT_SOURCE STREQUAL "" OR
       NOT EXISTS "${ATOMIC_WRITE_REPLACEMENT_SOURCE}" OR
       IS_DIRECTORY "${ATOMIC_WRITE_REPLACEMENT_SOURCE}")
        message(FATAL_ERROR
            "ATOMIC_WRITE_REPLACEMENT_SOURCE must name an existing regular file")
    endif()
    file(SHA256 "${ATOMIC_WRITE_SOURCE}" R_CODEGEN_ATOMIC_WRITE_SOURCE_HASH)
    file(SHA256
        "${ATOMIC_WRITE_REPLACEMENT_SOURCE}"
        R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_HASH)
    if(R_CODEGEN_ATOMIC_WRITE_SOURCE_HASH STREQUAL
       R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_HASH)
        message(FATAL_ERROR "atomic-write test sources must have different contents")
    endif()
    set(R_CODEGEN_ATOMIC_WRITE_ROOT "${OUTPUT_EXE}.root")
    set(R_CODEGEN_ATOMIC_WRITE_PATH
        "${R_CODEGEN_ATOMIC_WRITE_ROOT}/${ATOMIC_WRITE_RELATIVE}")
    file(REMOVE_RECURSE "${R_CODEGEN_ATOMIC_WRITE_ROOT}")
    file(MAKE_DIRECTORY "${R_CODEGEN_ATOMIC_WRITE_ROOT}")
endif()

set(R_CODEGEN_RUN_COMMAND "${OUTPUT_EXE}")
if(DEFINED ENABLE_SANITIZERS AND ENABLE_SANITIZERS)
    set(ENV{ASAN_OPTIONS} "abort_on_error=1")
    set(ENV{UBSAN_OPTIONS} "halt_on_error=1:print_stacktrace=1")
elseif(DEFINED ENABLE_THREAD_SANITIZER AND ENABLE_THREAD_SANITIZER)
    set(ENV{TSAN_OPTIONS} "halt_on_error=1")
endif()
set(R_CODEGEN_RUN_COMMAND_BASE ${R_CODEGEN_RUN_COMMAND})
if(DEFINED PROGRAM_ARGUMENTS AND NOT PROGRAM_ARGUMENTS STREQUAL "")
    list(APPEND R_CODEGEN_RUN_COMMAND ${PROGRAM_ARGUMENTS})
endif()
if(NOT R_CODEGEN_CREATED_DIRECTORY_ROOT STREQUAL "")
    list(APPEND R_CODEGEN_RUN_COMMAND
        "${R_CODEGEN_CREATED_DIRECTORY_ROOT}"
        "${CREATE_DIRECTORY_RELATIVE}")
endif()
if(NOT R_CODEGEN_ATOMIC_WRITE_ROOT STREQUAL "")
    list(APPEND R_CODEGEN_RUN_COMMAND
        "${ATOMIC_WRITE_SOURCE}"
        "${R_CODEGEN_ATOMIC_WRITE_ROOT}"
        "${ATOMIC_WRITE_RELATIVE}")
endif()

if(DEFINED PROGRAM_INPUT_FILE AND NOT PROGRAM_INPUT_FILE STREQUAL "")
    if(NOT EXISTS "${PROGRAM_INPUT_FILE}" OR IS_DIRECTORY "${PROGRAM_INPUT_FILE}")
        message(FATAL_ERROR "PROGRAM_INPUT_FILE must name an existing regular file")
    endif()
    execute_process(
        COMMAND ${R_CODEGEN_RUN_COMMAND}
        INPUT_FILE "${PROGRAM_INPUT_FILE}"
        RESULT_VARIABLE R_RUN_RESULT
        OUTPUT_VARIABLE R_RUN_OUTPUT
        ERROR_VARIABLE R_RUN_ERROR
    )
else()
    # Without an input file the program reads an empty standard input, not the one ctest was
    # started with: a driver that reads lines until the end of input would otherwise wait on a
    # terminal or an open pipe of the caller.
    execute_process(
        COMMAND ${R_CODEGEN_RUN_COMMAND}
        INPUT_FILE /dev/null
        RESULT_VARIABLE R_RUN_RESULT
        OUTPUT_VARIABLE R_RUN_OUTPUT
        ERROR_VARIABLE R_RUN_ERROR
    )
endif()
set(R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_RESULT 0)
set(R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_OUTPUT "")
set(R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_ERROR "")
if(NOT R_CODEGEN_ATOMIC_WRITE_ROOT STREQUAL "" AND "${R_RUN_RESULT}" STREQUAL "0")
    set(R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_COMMAND ${R_CODEGEN_RUN_COMMAND_BASE})
    list(APPEND R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_COMMAND
        "${ATOMIC_WRITE_REPLACEMENT_SOURCE}"
        "${R_CODEGEN_ATOMIC_WRITE_ROOT}"
        "${ATOMIC_WRITE_RELATIVE}")
    execute_process(
        COMMAND ${R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_COMMAND}
        RESULT_VARIABLE R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_RESULT
        OUTPUT_VARIABLE R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_OUTPUT
        ERROR_VARIABLE R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_ERROR
    )
endif()
set(R_CODEGEN_CREATED_DIRECTORY_FOUND FALSE)
set(R_CODEGEN_ATOMIC_WRITE_MATCHED FALSE)
set(R_CODEGEN_ATOMIC_WRITE_STAGING_CLEAN FALSE)
if(NOT R_CODEGEN_CREATED_DIRECTORY_ROOT STREQUAL "")
    if(IS_DIRECTORY "${R_CODEGEN_CREATED_DIRECTORY_PATH}")
        set(R_CODEGEN_CREATED_DIRECTORY_FOUND TRUE)
    endif()
    file(REMOVE_RECURSE "${R_CODEGEN_CREATED_DIRECTORY_ROOT}")
endif()
if(NOT R_CODEGEN_ATOMIC_WRITE_ROOT STREQUAL "")
    if(EXISTS "${R_CODEGEN_ATOMIC_WRITE_PATH}" AND
       NOT IS_DIRECTORY "${R_CODEGEN_ATOMIC_WRITE_PATH}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E compare_files
                "${ATOMIC_WRITE_SOURCE}"
                "${R_CODEGEN_ATOMIC_WRITE_PATH}"
            RESULT_VARIABLE R_CODEGEN_ATOMIC_WRITE_COMPARE_RESULT
        )
        if(R_CODEGEN_ATOMIC_WRITE_COMPARE_RESULT EQUAL 0)
            set(R_CODEGEN_ATOMIC_WRITE_MATCHED TRUE)
        endif()
    endif()
    file(GLOB R_CODEGEN_ATOMIC_WRITE_STAGING
        "${R_CODEGEN_ATOMIC_WRITE_ROOT}/.r-dir-stage-*")
    if(NOT R_CODEGEN_ATOMIC_WRITE_STAGING)
        set(R_CODEGEN_ATOMIC_WRITE_STAGING_CLEAN TRUE)
    endif()
    file(REMOVE_RECURSE "${R_CODEGEN_ATOMIC_WRITE_ROOT}")
endif()
if(DEFINED EXPECTED_PANIC AND NOT EXPECTED_PANIC STREQUAL "")
    if("${R_RUN_RESULT}" STREQUAL "0")
        message(FATAL_ERROR
            "generated panic program completed successfully")
    endif()
    string(FIND "${R_RUN_ERROR}" "${EXPECTED_PANIC}" R_PANIC_OFFSET)
    if(R_PANIC_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "panic stderr does not contain '${EXPECTED_PANIC}': "
            "${R_RUN_ERROR}")
    endif()
    if(DEFINED EXPECTED_SECOND_PANIC AND NOT EXPECTED_SECOND_PANIC STREQUAL "")
        # L39 (R-ERR-0008): a panic that begins while one unwinds is a second panic; the program
        # aborts with the report of the first panic followed by the report of the second.
        if(NOT R_RUN_RESULT MATCHES "[Aa]borted")
            message(FATAL_ERROR
                "second-panic fixture did not terminate through SIGABRT: ${R_RUN_RESULT}")
        endif()
        string(LENGTH "${EXPECTED_PANIC}" R_FIRST_PANIC_LENGTH)
        math(EXPR R_AFTER_FIRST_PANIC "${R_PANIC_OFFSET} + ${R_FIRST_PANIC_LENGTH}")
        string(SUBSTRING "${R_RUN_ERROR}" ${R_AFTER_FIRST_PANIC} -1 R_AFTER_FIRST_ERROR)
        string(FIND "${R_AFTER_FIRST_ERROR}" "${EXPECTED_SECOND_PANIC}" R_SECOND_PANIC_OFFSET)
        if(R_SECOND_PANIC_OFFSET EQUAL -1)
            message(FATAL_ERROR
                "second panic '${EXPECTED_SECOND_PANIC}' was not reported after the first: "
                "${R_RUN_ERROR}")
        endif()
    endif()
elseif(NOT "${R_RUN_RESULT}" STREQUAL "0")
    if(NOT DEFINED EXPECTED_EXIT_STATUS OR EXPECTED_EXIT_STATUS STREQUAL "" OR
       NOT "${R_RUN_RESULT}" STREQUAL "${EXPECTED_EXIT_STATUS}")
        message(FATAL_ERROR
            "generated program failed (${R_RUN_RESULT}): "
            "${R_RUN_OUTPUT}${R_RUN_ERROR}")
    endif()
elseif(DEFINED EXPECTED_EXIT_STATUS AND NOT EXPECTED_EXIT_STATUS STREQUAL "0")
    message(FATAL_ERROR
        "generated program returned 0 instead of ${EXPECTED_EXIT_STATUS}: "
        "${R_RUN_OUTPUT}${R_RUN_ERROR}")
endif()
if(DEFINED CHECK_STDOUT AND CHECK_STDOUT AND
   NOT "${R_RUN_OUTPUT}" STREQUAL "${EXPECTED_STDOUT}")
    message(FATAL_ERROR
        "generated program stdout mismatch: expected '${EXPECTED_STDOUT}', "
        "observed '${R_RUN_OUTPUT}'")
endif()
if(NOT R_CODEGEN_CREATED_DIRECTORY_ROOT STREQUAL "" AND
   NOT R_CODEGEN_CREATED_DIRECTORY_FOUND)
    message(FATAL_ERROR
        "generated program did not create '${CREATE_DIRECTORY_RELATIVE}' beneath its private root")
endif()
if(NOT R_CODEGEN_ATOMIC_WRITE_ROOT STREQUAL "" AND NOT R_CODEGEN_ATOMIC_WRITE_MATCHED)
    message(FATAL_ERROR
        "generated program did not atomically publish byte-identical '${ATOMIC_WRITE_RELATIVE}'")
endif()
if(NOT R_CODEGEN_ATOMIC_WRITE_ROOT STREQUAL "" AND
   NOT "${R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_RESULT}" STREQUAL "0")
    message(FATAL_ERROR
        "generated no-replace retry failed (${R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_RESULT}): "
        "${R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_OUTPUT}"
        "${R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_ERROR}")
endif()
if(NOT R_CODEGEN_ATOMIC_WRITE_ROOT STREQUAL "" AND
   NOT R_CODEGEN_ATOMIC_WRITE_STAGING_CLEAN)
    message(FATAL_ERROR
        "generated program left private atomic-write staging entries beneath its root")
endif()

if(DEFINED EXPECTED_STDERR AND NOT EXPECTED_STDERR STREQUAL "")
    string(FIND "${R_RUN_ERROR}" "${EXPECTED_STDERR}" R_EXPECTED_DIAGNOSTIC_OFFSET)
    if(R_EXPECTED_DIAGNOSTIC_OFFSET EQUAL -1)
        message(FATAL_ERROR "missing main diagnostic '${EXPECTED_STDERR}': ${R_RUN_ERROR}")
    endif()
endif()
