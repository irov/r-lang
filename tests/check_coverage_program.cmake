# B8.2: line coverage of the R source. The program is built with --coverage, which writes the
# notes, linked with the profile runtime of the toolchain and run once, which writes the counts;
# llvm-cov gcov reports the counts by the lines of the fixture.
if(NOT DEFINED OUTPUT_EXE OR NOT DEFINED C_COMPILER OR NOT DEFINED SOURCE_1)
    message(FATAL_ERROR "missing coverage test input")
endif()

get_filename_component(R_COVERAGE_TOOLS "${C_COMPILER}" DIRECTORY)
execute_process(
    COMMAND "${C_COMPILER}" -print-resource-dir
    RESULT_VARIABLE R_COVERAGE_RESULT
    OUTPUT_VARIABLE R_COVERAGE_RESOURCES
    OUTPUT_STRIP_TRAILING_WHITESPACE)
set(R_COVERAGE_RUNTIME "${R_COVERAGE_RESOURCES}/lib/darwin/libclang_rt.profile_osx.a")
if(NOT R_COVERAGE_RESULT EQUAL 0 OR NOT EXISTS "${R_COVERAGE_RUNTIME}")
    message(FATAL_ERROR "the toolchain has no profile runtime at ${R_COVERAGE_RUNTIME}")
endif()
file(REMOVE "${OUTPUT_EXE}.gcno" "${OUTPUT_EXE}.gcda")
set(FRONTEND_EXTRA_ARGUMENTS --coverage "${OUTPUT_EXE}")
set(LINK_EXTRA_FLAGS "${R_COVERAGE_RUNTIME}")
include("${CMAKE_CURRENT_LIST_DIR}/check_codegen_program.cmake")

if(NOT EXISTS "${OUTPUT_EXE}.gcno" OR NOT EXISTS "${OUTPUT_EXE}.gcda")
    message(FATAL_ERROR "the program wrote no coverage notes or counts next to ${OUTPUT_EXE}")
endif()
get_filename_component(R_COVERAGE_DIRECTORY "${OUTPUT_EXE}" DIRECTORY)
execute_process(
    COMMAND "${R_COVERAGE_TOOLS}/llvm-cov" gcov -o "${OUTPUT_EXE}.gcno" -t "${SOURCE_1}"
    WORKING_DIRECTORY "${R_COVERAGE_DIRECTORY}"
    RESULT_VARIABLE R_COVERAGE_RESULT
    OUTPUT_VARIABLE R_COVERAGE_REPORT
    ERROR_VARIABLE R_COVERAGE_ERROR)
if(NOT R_COVERAGE_RESULT EQUAL 0)
    message(FATAL_ERROR "llvm-cov gcov failed (${R_COVERAGE_RESULT}): ${R_COVERAGE_ERROR}")
endif()
# The insertion loop runs 300 times, a lookup never misses, so its `none` arm never runs.
foreach(R_COVERAGE_EXPECTED
        "300:[ ]+[0-9]+:        o<i32> previous = std[.]dict::insert[(]&table, key [*] 37, key[)];"
        "#####:[ ]+[0-9]+:            case variant o::none: return 3;")
    if(NOT R_COVERAGE_REPORT MATCHES "${R_COVERAGE_EXPECTED}")
        message(FATAL_ERROR
            "the coverage report has no line matching '${R_COVERAGE_EXPECTED}':\n${R_COVERAGE_REPORT}")
    endif()
endforeach()
