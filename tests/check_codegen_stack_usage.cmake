if(NOT DEFINED STACK_USAGE_OBJECT OR STACK_USAGE_OBJECT STREQUAL "" OR
   NOT DEFINED STACK_USAGE_REPORT OR STACK_USAGE_REPORT STREQUAL "" OR
   NOT DEFINED STACK_USAGE_SOURCE OR STACK_USAGE_SOURCE STREQUAL "" OR
   NOT DEFINED STACK_USAGE_BASE_DIRECTORY OR STACK_USAGE_BASE_DIRECTORY STREQUAL "" OR
   NOT DEFINED STACK_USAGE_HEADER OR STACK_USAGE_HEADER STREQUAL "")
    message(FATAL_ERROR
        "STACK_USAGE_OBJECT, STACK_USAGE_REPORT, STACK_USAGE_SOURCE, "
        "STACK_USAGE_BASE_DIRECTORY and STACK_USAGE_HEADER are required")
endif()

if(NOT DEFINED STACK_USAGE_MODE OR STACK_USAGE_MODE STREQUAL "")
    set(STACK_USAGE_MODE "generate")
endif()
if(NOT STACK_USAGE_MODE STREQUAL "generate" AND
   NOT STACK_USAGE_MODE STREQUAL "merge" AND
   NOT STACK_USAGE_MODE STREQUAL "validate")
    message(FATAL_ERROR
        "STACK_USAGE_MODE must be 'generate', 'merge' or 'validate'")
endif()

foreach(required_file IN ITEMS "${STACK_USAGE_OBJECT}" "${STACK_USAGE_REPORT}")
    if(NOT EXISTS "${required_file}" OR IS_DIRECTORY "${required_file}")
        message(FATAL_ERROR "missing stack-usage artifact: ${required_file}")
    endif()
    file(SIZE "${required_file}" required_file_size)
    if(required_file_size EQUAL 0)
        message(FATAL_ERROR "empty stack-usage artifact: ${required_file}")
    endif()
endforeach()
if(NOT EXISTS "${STACK_USAGE_SOURCE}" OR IS_DIRECTORY "${STACK_USAGE_SOURCE}")
    message(FATAL_ERROR "missing generated source for stack-usage check: ${STACK_USAGE_SOURCE}")
endif()
if(NOT STACK_USAGE_MODE STREQUAL "generate")
    if(NOT EXISTS "${STACK_USAGE_HEADER}" OR IS_DIRECTORY "${STACK_USAGE_HEADER}")
        message(FATAL_ERROR
            "missing measured stack-usage header: ${STACK_USAGE_HEADER}")
    endif()
    file(SIZE "${STACK_USAGE_HEADER}" stack_usage_header_size)
    if(stack_usage_header_size EQUAL 0)
        message(FATAL_ERROR
            "empty measured stack-usage header: ${STACK_USAGE_HEADER}")
    endif()
endif()

if(DEFINED STACK_USAGE_MAX_FRAME_SIZE AND
   NOT STACK_USAGE_MAX_FRAME_SIZE STREQUAL "" AND
   NOT STACK_USAGE_MAX_FRAME_SIZE MATCHES "^[0-9]+$")
    message(FATAL_ERROR
        "STACK_USAGE_MAX_FRAME_SIZE must be an unsigned decimal integer")
endif()
if(DEFINED STACK_USAGE_EXPECT_FRAME_GREATER_THAN AND
   NOT STACK_USAGE_EXPECT_FRAME_GREATER_THAN STREQUAL "" AND
   NOT STACK_USAGE_EXPECT_FRAME_GREATER_THAN MATCHES "^[0-9]+$")
    message(FATAL_ERROR
        "STACK_USAGE_EXPECT_FRAME_GREATER_THAN must be an unsigned decimal integer")
endif()

# R-FUNC-0004: every R_STACK_ENTRY(<name>) receives frame(name) plus the longest acyclic callee
# path. The fragment is derived from the same .su report and appended to the frame bounds.
function(r_stack_usage_entry_fragment report output_variable)
    set(${output_variable} "" PARENT_SCOPE)
    if(NOT DEFINED STACK_USAGE_PYTHON OR STACK_USAGE_PYTHON STREQUAL "" OR
       NOT DEFINED STACK_USAGE_ENTRY_TOOL OR STACK_USAGE_ENTRY_TOOL STREQUAL "")
        return()
    endif()
    if(NOT DEFINED STACK_USAGE_ENTRY_BUDGET OR NOT STACK_USAGE_ENTRY_BUDGET MATCHES "^[0-9]+$")
        message(FATAL_ERROR "STACK_USAGE_ENTRY_BUDGET must be an unsigned decimal integer")
    endif()
    set(fragment_file "${STACK_USAGE_HEADER}.entries")
    execute_process(
        COMMAND "${STACK_USAGE_PYTHON}" "${STACK_USAGE_ENTRY_TOOL}"
            --source "${STACK_USAGE_SOURCE}"
            --report "${report}"
            --output "${fragment_file}"
            --budget "${STACK_USAGE_ENTRY_BUDGET}"
        RESULT_VARIABLE entry_result
        OUTPUT_VARIABLE entry_output
        ERROR_VARIABLE entry_error
    )
    if(NOT entry_result EQUAL 0)
        message(FATAL_ERROR
            "stack entry bounds failed (${entry_result}):\n${entry_output}${entry_error}")
    endif()
    file(READ "${fragment_file}" fragment)
    message(STATUS "${entry_output}")
    set(${output_variable} "${fragment}" PARENT_SCOPE)
endfunction()

get_filename_component(expected_source_name "${STACK_USAGE_SOURCE}" NAME)
file(REAL_PATH "${STACK_USAGE_SOURCE}" expected_source_path)
file(STRINGS "${STACK_USAGE_REPORT}" stack_usage_records ENCODING UTF-8)
if(NOT stack_usage_records)
    message(FATAL_ERROR "stack-usage report contains no records: ${STACK_USAGE_REPORT}")
endif()

string(ASCII 9 stack_usage_tab)
# The frame tables of a pass are variables keyed by function name, so each check is a lookup and
# not a scan of a list; the pass number keeps the tables of an earlier inclusion out of this one.
if(NOT DEFINED R_STACK_USAGE_PASS)
    set(R_STACK_USAGE_PASS 0)
endif()
math(EXPR R_STACK_USAGE_PASS "${R_STACK_USAGE_PASS} + 1")
set(r_stack_seen "r_stack_seen_${R_STACK_USAGE_PASS}_")
set(r_stack_measured "r_stack_measured_${R_STACK_USAGE_PASS}_")
# The records of one source come together, so the source of the previous record answers most
# questions of whether a record is generated without touching the file system.
set(r_stack_previous_source "")
set(r_stack_previous_generated FALSE)
set(record_count 0)
set(generated_record_count 0)
set(generated_frame_definitions)
set(generated_function_names)
set(generated_max_frame_size 0)
set(generated_max_frame_record "")

foreach(stack_usage_record IN LISTS stack_usage_records)
    string(REPLACE "${stack_usage_tab}" ";" stack_usage_fields "${stack_usage_record}")
    list(LENGTH stack_usage_fields stack_usage_field_count)
    if(NOT stack_usage_field_count EQUAL 3)
        message(FATAL_ERROR
            "malformed or incomplete stack-usage record: ${stack_usage_record}")
    endif()

    list(GET stack_usage_fields 0 frame_identity)
    list(GET stack_usage_fields 1 frame_size)
    list(GET stack_usage_fields 2 frame_kind)
    if(NOT frame_identity MATCHES
       "^.+:[0-9]+(:[0-9]+)?:[A-Za-z_][A-Za-z0-9_]*$")
        message(FATAL_ERROR
            "stack-usage record has an invalid identity: ${stack_usage_record}")
    endif()
    if(NOT frame_size MATCHES "^[0-9]+$")
        message(FATAL_ERROR
            "stack-usage record is missing a static byte count: ${stack_usage_record}")
    endif()
    string(REGEX REPLACE "^.*:" "" function_name "${frame_identity}")
    math(EXPR record_count "${record_count} + 1")

    string(REGEX REPLACE
        ":[0-9]+(:[0-9]+)?:[A-Za-z_][A-Za-z0-9_]*$" ""
        frame_source "${frame_identity}")
    if(frame_source STREQUAL r_stack_previous_source)
        set(frame_is_generated "${r_stack_previous_generated}")
    else()
        if(IS_ABSOLUTE "${frame_source}")
            set(frame_source_candidate "${frame_source}")
        else()
            set(frame_source_candidate "${STACK_USAGE_BASE_DIRECTORY}/${frame_source}")
        endif()
        set(frame_is_generated FALSE)
        if(EXISTS "${frame_source_candidate}" AND
           NOT IS_DIRECTORY "${frame_source_candidate}")
            file(REAL_PATH "${frame_source_candidate}" frame_source_path)
            if(frame_source_path STREQUAL expected_source_path)
                set(frame_is_generated TRUE)
            endif()
        endif()
        set(r_stack_previous_source "${frame_source}")
        set(r_stack_previous_generated "${frame_is_generated}")
    endif()
    if(frame_is_generated)
        if(NOT frame_kind STREQUAL "static")
            message(FATAL_ERROR
                "dynamic or unknown generated stack frame is not accepted: "
                "${stack_usage_record}")
        endif()
        if(DEFINED ${r_stack_seen}${function_name})
            message(FATAL_ERROR
                "duplicate stack-usage record for function '${function_name}'")
        endif()
        set(${r_stack_seen}${function_name} TRUE)
        math(EXPR generated_record_count "${generated_record_count} + 1")
        list(APPEND generated_frame_definitions "${function_name}|${frame_size}")
        list(APPEND generated_function_names "${function_name}")
        if(frame_size GREATER generated_max_frame_size)
            set(generated_max_frame_size "${frame_size}")
            set(generated_max_frame_record "${frame_identity}")
        endif()
    endif()
endforeach()

if(record_count EQUAL 0)
    message(FATAL_ERROR "stack-usage report contains no frame records")
endif()
if(generated_record_count EQUAL 0)
    message(FATAL_ERROR
        "stack-usage report is missing records for generated source '${expected_source_name}'")
endif()

list(SORT generated_frame_definitions)
list(SORT generated_function_names)

if(DEFINED STACK_USAGE_MAX_FRAME_SIZE AND
   NOT STACK_USAGE_MAX_FRAME_SIZE STREQUAL "" AND
   generated_max_frame_size GREATER STACK_USAGE_MAX_FRAME_SIZE)
    message(FATAL_ERROR
        "generated frame exceeds the fixture regression ceiling of "
        "${STACK_USAGE_MAX_FRAME_SIZE} bytes: ${generated_max_frame_record} uses "
        "${generated_max_frame_size} bytes")
endif()

if(DEFINED STACK_USAGE_EXPECT_FRAME_GREATER_THAN AND
   NOT STACK_USAGE_EXPECT_FRAME_GREATER_THAN STREQUAL "" AND
   NOT generated_max_frame_size GREATER STACK_USAGE_EXPECT_FRAME_GREATER_THAN)
    message(FATAL_ERROR
        "stack-usage measurement did not find a generated frame larger than "
        "${STACK_USAGE_EXPECT_FRAME_GREATER_THAN} bytes; maximum was "
        "${generated_max_frame_size} bytes at ${generated_max_frame_record}")
endif()

if(STACK_USAGE_MODE STREQUAL "generate")
    string(CONCAT stack_usage_header_content
        "#ifndef R_GENERATED_STACK_USAGE_H\n"
        "#define R_GENERATED_STACK_USAGE_H\n"
        "\n"
        "#include <stddef.h>\n"
        "\n")
    foreach(generated_frame_definition IN LISTS generated_frame_definitions)
        string(REPLACE "|" ";" generated_frame_fields "${generated_frame_definition}")
        list(GET generated_frame_fields 0 generated_function_name)
        list(GET generated_frame_fields 1 generated_frame_size)
        string(APPEND stack_usage_header_content
            "#define R_STACK_FRAME_${generated_function_name} "
            "((size_t)${generated_frame_size})\n")
    endforeach()
    r_stack_usage_entry_fragment("${STACK_USAGE_REPORT}" stack_usage_entry_fragment)
    if(NOT stack_usage_entry_fragment STREQUAL "")
        string(APPEND stack_usage_header_content "\n${stack_usage_entry_fragment}")
    endif()
    string(APPEND stack_usage_header_content "\n#endif\n")
    file(WRITE "${STACK_USAGE_HEADER}" "${stack_usage_header_content}")
    file(SIZE "${STACK_USAGE_HEADER}" stack_usage_header_size)
    if(stack_usage_header_size EQUAL 0)
        message(FATAL_ERROR
            "generated an empty stack-usage header: ${STACK_USAGE_HEADER}")
    endif()
else()
    file(STRINGS "${STACK_USAGE_HEADER}" stack_usage_header_lines ENCODING UTF-8)
    set(measured_frame_definitions)
    set(measured_function_names)
    foreach(stack_usage_header_line IN LISTS stack_usage_header_lines)
        if(stack_usage_header_line MATCHES "^#define R_STACK_FRAME_")
            if(stack_usage_header_line MATCHES
               "^#define R_STACK_FRAME_([A-Za-z_][A-Za-z0-9_]*) \\(\\(size_t\\)([0-9]+)\\)$")
                set(measured_function_name "${CMAKE_MATCH_1}")
                set(measured_frame_size "${CMAKE_MATCH_2}")
            else()
                message(FATAL_ERROR
                    "malformed measured stack-frame definition: "
                    "${stack_usage_header_line}")
            endif()
            if(DEFINED ${r_stack_measured}${measured_function_name})
                message(FATAL_ERROR
                    "duplicate measured stack-frame definition for function "
                    "'${measured_function_name}'")
            endif()
            set(${r_stack_measured}${measured_function_name} "${measured_frame_size}")
            list(APPEND measured_function_names "${measured_function_name}")
            list(APPEND measured_frame_definitions
                "${measured_function_name}|${measured_frame_size}")
        endif()
    endforeach()
    if(NOT measured_function_names)
        message(FATAL_ERROR
            "measured stack-usage header contains no frame definitions: "
            "${STACK_USAGE_HEADER}")
    endif()
    list(SORT measured_function_names)
    list(SORT measured_frame_definitions)
    string(JOIN "|" generated_function_set ${generated_function_names})
    string(JOIN "|" measured_function_set ${measured_function_names})
    if(NOT "${generated_function_set}" STREQUAL "${measured_function_set}")
        string(JOIN ", " generated_function_list ${generated_function_names})
        string(JOIN ", " measured_function_list ${measured_function_names})
        message(FATAL_ERROR
            "final generated frame name set does not match measured header; "
            "final=[${generated_function_list}], "
            "measured=[${measured_function_list}]")
    endif()

    set(merged_frame_definitions)
    set(stack_usage_bounds_changed FALSE)
    foreach(generated_frame_definition IN LISTS generated_frame_definitions)
        string(REPLACE "|" ";" generated_frame_fields "${generated_frame_definition}")
        list(GET generated_frame_fields 0 generated_function_name)
        list(GET generated_frame_fields 1 generated_frame_size)
        set(measured_frame_size "")
        if(DEFINED ${r_stack_measured}${generated_function_name})
            set(measured_frame_size "${${r_stack_measured}${generated_function_name}}")
        endif()
        if("${measured_frame_size}" STREQUAL "")
            message(FATAL_ERROR
                "final generated frame '${generated_function_name}' has no measured bound")
        endif()
        if(generated_frame_size GREATER measured_frame_size)
            if(STACK_USAGE_MODE STREQUAL "validate")
                message(FATAL_ERROR
                    "final generated frame '${generated_function_name}' exceeds its measured "
                    "bound: ${generated_frame_size} bytes > ${measured_frame_size} bytes")
            endif()
            set(measured_frame_size "${generated_frame_size}")
            set(stack_usage_bounds_changed TRUE)
        endif()
        list(APPEND merged_frame_definitions
            "${generated_function_name}|${measured_frame_size}")
    endforeach()
    if(STACK_USAGE_MODE STREQUAL "merge" AND stack_usage_bounds_changed)
        list(SORT merged_frame_definitions)
        string(CONCAT stack_usage_header_content
            "#ifndef R_GENERATED_STACK_USAGE_H\n"
            "#define R_GENERATED_STACK_USAGE_H\n"
            "\n"
            "#include <stddef.h>\n"
            "\n")
        foreach(merged_frame_definition IN LISTS merged_frame_definitions)
            string(REPLACE "|" ";" merged_frame_fields "${merged_frame_definition}")
            list(GET merged_frame_fields 0 merged_function_name)
            list(GET merged_frame_fields 1 merged_frame_size)
            string(APPEND stack_usage_header_content
                "#define R_STACK_FRAME_${merged_function_name} "
                "((size_t)${merged_frame_size})\n")
        endforeach()
        r_stack_usage_entry_fragment("${STACK_USAGE_REPORT}" stack_usage_entry_fragment)
        if(NOT stack_usage_entry_fragment STREQUAL "")
            string(APPEND stack_usage_header_content "\n${stack_usage_entry_fragment}")
        endif()
        string(APPEND stack_usage_header_content "\n#endif\n")
        file(WRITE "${STACK_USAGE_HEADER}" "${stack_usage_header_content}")
    elseif(STACK_USAGE_MODE STREQUAL "merge")
        # Frames are stable; the entry bounds derived from them must already be in the header.
        r_stack_usage_entry_fragment("${STACK_USAGE_REPORT}" stack_usage_entry_fragment)
        if(NOT stack_usage_entry_fragment STREQUAL "")
            file(READ "${STACK_USAGE_HEADER}" stack_usage_header_current)
            string(FIND "${stack_usage_header_current}" "${stack_usage_entry_fragment}"
                stack_usage_entry_offset)
            if(stack_usage_entry_offset EQUAL -1)
                set(stack_usage_bounds_changed TRUE)
                string(REGEX REPLACE "\n#define R_STACK_ENTRY_[^\n]*" ""
                    stack_usage_header_current "${stack_usage_header_current}")
                string(REPLACE "\n#endif\n" "\n${stack_usage_entry_fragment}\n#endif\n"
                    stack_usage_header_current "${stack_usage_header_current}")
                file(WRITE "${STACK_USAGE_HEADER}" "${stack_usage_header_current}")
            endif()
        endif()
    else()
        r_stack_usage_entry_fragment("${STACK_USAGE_REPORT}" stack_usage_entry_fragment)
        if(NOT stack_usage_entry_fragment STREQUAL "")
            file(READ "${STACK_USAGE_HEADER}" stack_usage_header_current)
            string(FIND "${stack_usage_header_current}" "${stack_usage_entry_fragment}"
                stack_usage_entry_offset)
            if(stack_usage_entry_offset EQUAL -1)
                message(FATAL_ERROR
                    "final entry stack bounds differ from the measured header: "
                    "${STACK_USAGE_HEADER}")
            endif()
        endif()
    endif()
endif()

if(STACK_USAGE_MODE STREQUAL "generate")
    message(STATUS
        "measured ${record_count} stack frames; generated maximum is "
        "${generated_max_frame_size} bytes at ${generated_max_frame_record}; wrote "
        "initial bounds to ${STACK_USAGE_HEADER}")
elseif(STACK_USAGE_MODE STREQUAL "merge")
    if(stack_usage_bounds_changed)
        message(STATUS
            "expanded generated stack bounds from candidate ${STACK_USAGE_OBJECT}; "
            "another fixed-point compile is required")
    else()
        message(STATUS
            "generated stack bounds reached a fixed point for ${STACK_USAGE_OBJECT}; "
            "validated ${generated_record_count} frames")
    endif()
else()
    message(STATUS
        "validated ${generated_record_count} final generated stack frames against "
        "${STACK_USAGE_HEADER}; generated maximum is ${generated_max_frame_size} bytes at "
        "${generated_max_frame_record}")
endif()
