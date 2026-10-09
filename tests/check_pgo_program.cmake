# B7.2: profile-guided optimization of a program. The program is built with --profile-generate,
# linked with the profile runtime of the toolchain and run, which writes its profile; llvm-profdata
# merges it; the IR of the program built with --profile-use carries the profile summary and branch
# weights, and that build runs with the same result as every other (check_codegen_program.cmake).
if(NOT DEFINED OUTPUT_EXE OR NOT DEFINED C_COMPILER OR NOT DEFINED R_FRONT_EXECUTABLE OR
   NOT DEFINED SOURCE_1 OR NOT DEFINED LIBRARY_MAP)
    message(FATAL_ERROR "missing profile-guided optimization test input")
endif()

set(R_PGO_SCRIPT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}")
get_filename_component(R_PGO_TOOLS "${C_COMPILER}" DIRECTORY)
execute_process(
    COMMAND "${C_COMPILER}" -print-resource-dir
    RESULT_VARIABLE R_PGO_RESULT
    OUTPUT_VARIABLE R_PGO_RESOURCES
    OUTPUT_STRIP_TRAILING_WHITESPACE)
set(R_PGO_RUNTIME "${R_PGO_RESOURCES}/lib/darwin/libclang_rt.profile_osx.a")
if(NOT R_PGO_RESULT EQUAL 0 OR NOT EXISTS "${R_PGO_RUNTIME}")
    message(FATAL_ERROR "the toolchain has no profile runtime at ${R_PGO_RUNTIME}")
endif()
set(R_PGO_DIRECTORY "${OUTPUT_EXE}.profile")
set(R_PGO_PROFILE "${R_PGO_DIRECTORY}/merged.profdata")
set(R_PGO_OUTPUT "${OUTPUT_EXE}")
file(REMOVE_RECURSE "${R_PGO_DIRECTORY}")
file(MAKE_DIRECTORY "${R_PGO_DIRECTORY}")

# One build and run of the program; the variables set here are local to the call.
function(r_pgo_program stage)
    set(OUTPUT_EXE "${R_PGO_OUTPUT}.${stage}")
    if(stage STREQUAL "generate")
        set(FRONTEND_EXTRA_ARGUMENTS --profile-generate)
        set(LINK_EXTRA_FLAGS "${R_PGO_RUNTIME}")
        set(ENV{LLVM_PROFILE_FILE} "${R_PGO_DIRECTORY}/run-%p.profraw")
    else()
        set(FRONTEND_EXTRA_ARGUMENTS --profile-use "${R_PGO_PROFILE}")
        set(LINK_EXTRA_FLAGS "")
        unset(ENV{LLVM_PROFILE_FILE})
    endif()
    include("${R_PGO_SCRIPT_DIRECTORY}/check_codegen_program.cmake")
endfunction()

r_pgo_program(generate)
file(GLOB R_PGO_RAW "${R_PGO_DIRECTORY}/*.profraw")
if(NOT R_PGO_RAW)
    message(FATAL_ERROR "the instrumented program wrote no profile into ${R_PGO_DIRECTORY}")
endif()
execute_process(
    COMMAND "${R_PGO_TOOLS}/llvm-profdata" merge -o "${R_PGO_PROFILE}" ${R_PGO_RAW}
    RESULT_VARIABLE R_PGO_RESULT
    ERROR_VARIABLE R_PGO_ERROR)
if(NOT R_PGO_RESULT EQUAL 0)
    message(FATAL_ERROR "llvm-profdata merge failed (${R_PGO_RESULT}): ${R_PGO_ERROR}")
endif()

set(R_PGO_ARGUMENTS --emit=llvm-ir --profile-use "${R_PGO_PROFILE}" --library-map "${LIBRARY_MAP}")
if(DEFINED BITCODE_CATALOG AND NOT BITCODE_CATALOG STREQUAL "")
    list(APPEND R_PGO_ARGUMENTS --bitcode-catalog "${BITCODE_CATALOG}")
endif()
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" ${R_PGO_ARGUMENTS} "${SOURCE_1}"
    RESULT_VARIABLE R_PGO_RESULT
    OUTPUT_VARIABLE R_PGO_IR
    ERROR_VARIABLE R_PGO_ERROR)
if(NOT R_PGO_RESULT EQUAL 0)
    message(FATAL_ERROR "r-front --profile-use failed (${R_PGO_RESULT}): ${R_PGO_ERROR}")
endif()
if(NOT R_PGO_IR MATCHES "ProfileSummary" OR NOT R_PGO_IR MATCHES "branch_weights")
    message(FATAL_ERROR "the program built with its profile carries no profile summary or weights")
endif()

r_pgo_program(use)
