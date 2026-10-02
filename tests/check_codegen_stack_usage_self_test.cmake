if(NOT DEFINED STACK_USAGE_CHECK_SCRIPT OR
   STACK_USAGE_CHECK_SCRIPT STREQUAL "" OR
   NOT DEFINED STACK_USAGE_SELF_TEST_ROOT OR
   STACK_USAGE_SELF_TEST_ROOT STREQUAL "" OR
   NOT DEFINED STACK_USAGE_SELF_TEST_CASE OR
   STACK_USAGE_SELF_TEST_CASE STREQUAL "")
    message(FATAL_ERROR
        "STACK_USAGE_CHECK_SCRIPT, STACK_USAGE_SELF_TEST_ROOT and "
        "STACK_USAGE_SELF_TEST_CASE are required")
endif()

set(self_test_directory
    "${STACK_USAGE_SELF_TEST_ROOT}/${STACK_USAGE_SELF_TEST_CASE}")
set(source_file "${self_test_directory}/generated_fixture.c")
set(object_file "${self_test_directory}/generated_fixture.o")
set(report_file "${self_test_directory}/generated_fixture.su")
set(header_file "${self_test_directory}/generated_fixture.stack-usage.h")
set(wrapper_file "${self_test_directory}/fixture_wrapper.c")
file(MAKE_DIRECTORY "${self_test_directory}")
file(REMOVE
    "${source_file}"
    "${object_file}"
    "${report_file}"
    "${header_file}"
    "${wrapper_file}"
)
file(WRITE "${source_file}" "int r_f00000002(void) { return 0; }\n")

function(run_stack_usage_check expected_result expected_text stack_usage_mode)
    set(stack_usage_command
        "${CMAKE_COMMAND}"
        "-DSTACK_USAGE_OBJECT=${object_file}"
        "-DSTACK_USAGE_REPORT=${report_file}"
        "-DSTACK_USAGE_SOURCE=${source_file}"
        "-DSTACK_USAGE_BASE_DIRECTORY=${self_test_directory}"
        "-DSTACK_USAGE_HEADER=${header_file}"
        "-DSTACK_USAGE_MODE=${stack_usage_mode}"
    )
    if(DEFINED self_test_max_frame_size AND
       NOT self_test_max_frame_size STREQUAL "")
        list(APPEND stack_usage_command
            "-DSTACK_USAGE_MAX_FRAME_SIZE=${self_test_max_frame_size}")
    endif()
    list(APPEND stack_usage_command -P "${STACK_USAGE_CHECK_SCRIPT}")
    execute_process(
        COMMAND ${stack_usage_command}
        RESULT_VARIABLE stack_usage_result
        OUTPUT_VARIABLE stack_usage_output
        ERROR_VARIABLE stack_usage_error
    )
    set(stack_usage_diagnostics "${stack_usage_output}${stack_usage_error}")
    if(expected_result STREQUAL "success")
        if(NOT stack_usage_result EQUAL 0)
            message(FATAL_ERROR
                "stack-usage positive self-test failed (${stack_usage_result}):\n"
                "${stack_usage_diagnostics}")
        endif()
    elseif(expected_result STREQUAL "failure")
        if(stack_usage_result EQUAL 0)
            message(FATAL_ERROR
                "stack-usage negative self-test unexpectedly succeeded")
        endif()
        string(FIND "${stack_usage_diagnostics}" "${expected_text}"
            expected_text_offset)
        if(expected_text_offset EQUAL -1)
            message(FATAL_ERROR
                "stack-usage negative self-test did not report '${expected_text}':\n"
                "${stack_usage_diagnostics}")
        endif()
    else()
        message(FATAL_ERROR "invalid expected self-test result: ${expected_result}")
    endif()
endfunction()

set(static_record_f
    "${source_file}:10:1:r_f00000002\t24\tstatic\n")
set(static_record_async
    "${source_file}:4:1:r_async_step_00000001\t48\tstatic\n")

if(STACK_USAGE_SELF_TEST_CASE STREQUAL "positive_deterministic_header")
    file(WRITE "${object_file}" "object\n")
    file(WRITE "${report_file}" "${static_record_f}${static_record_async}")
    run_stack_usage_check(success "" generate)
    file(READ "${header_file}" first_header)
    string(CONCAT expected_header
        "#ifndef R_GENERATED_STACK_USAGE_H\n"
        "#define R_GENERATED_STACK_USAGE_H\n"
        "\n"
        "#include <stddef.h>\n"
        "\n"
        "#define R_STACK_FRAME_r_async_step_00000001 ((size_t)48)\n"
        "#define R_STACK_FRAME_r_f00000002 ((size_t)24)\n"
        "\n"
        "#endif\n")
    if(NOT first_header STREQUAL expected_header)
        message(FATAL_ERROR
            "stack-usage header does not match the canonical expected output:\n"
            "${first_header}")
    endif()

    file(WRITE "${report_file}" "${static_record_async}${static_record_f}")
    file(REMOVE "${header_file}")
    run_stack_usage_check(success "" generate)
    file(READ "${header_file}" second_header)
    if(NOT second_header STREQUAL first_header)
        message(FATAL_ERROR
            "stack-usage header depends on input record order")
    endif()
elseif(STACK_USAGE_SELF_TEST_CASE STREQUAL "missing_artifact")
    file(WRITE "${report_file}" "${static_record_f}")
    run_stack_usage_check(failure "missing stack-usage artifact" generate)
elseif(STACK_USAGE_SELF_TEST_CASE STREQUAL "empty_artifact")
    file(WRITE "${object_file}" "object\n")
    file(WRITE "${report_file}" "")
    run_stack_usage_check(failure "empty stack-usage artifact" generate)
elseif(STACK_USAGE_SELF_TEST_CASE STREQUAL "malformed_record")
    file(WRITE "${object_file}" "object\n")
    file(WRITE "${report_file}" "${source_file}:1:1:r_f00000002\t24\n")
    run_stack_usage_check(
        failure "malformed or incomplete stack-usage record" generate)
elseif(STACK_USAGE_SELF_TEST_CASE STREQUAL "dynamic_frame")
    file(WRITE "${object_file}" "object\n")
    file(WRITE "${report_file}"
        "${source_file}:1:1:r_f00000002\t24\tdynamic\n")
    run_stack_usage_check(
        failure "dynamic or unknown generated stack frame is not accepted" generate)
elseif(STACK_USAGE_SELF_TEST_CASE STREQUAL "duplicate_function")
    file(WRITE "${object_file}" "object\n")
    file(WRITE "${report_file}"
        "${source_file}:1:1:r_f00000002\t24\tstatic\n"
        "${source_file}:2:1:r_f00000002\t32\tstatic\n")
    run_stack_usage_check(
        failure
        "duplicate stack-usage record for function 'r_f00000002'"
        generate)
elseif(STACK_USAGE_SELF_TEST_CASE STREQUAL "ceiling_violation")
    file(WRITE "${object_file}" "object\n")
    file(WRITE "${report_file}"
        "${source_file}:1:1:r_f00000002\t65\tstatic\n")
    set(self_test_max_frame_size 64)
    run_stack_usage_check(
        failure
        "generated frame exceeds the fixture regression ceiling of 64 bytes"
        generate)
    if(EXISTS "${header_file}")
        message(FATAL_ERROR
            "ceiling failure created a stack-usage header")
    endif()
elseif(STACK_USAGE_SELF_TEST_CASE STREQUAL "merge_ceiling_preserves_header")
    file(WRITE "${object_file}" "object\n")
    file(WRITE "${report_file}" "${static_record_f}${static_record_async}")
    set(self_test_max_frame_size 64)
    run_stack_usage_check(success "" generate)
    file(READ "${header_file}" initial_header)
    file(WRITE "${report_file}"
        "${source_file}:10:1:r_f00000002\t65\tstatic\n"
        "${source_file}:4:1:r_async_step_00000001\t48\tstatic\n")
    run_stack_usage_check(
        failure
        "generated frame exceeds the fixture regression ceiling of 64 bytes"
        merge)
    file(READ "${header_file}" rejected_header)
    if(NOT rejected_header STREQUAL initial_header)
        message(FATAL_ERROR
            "ceiling failure changed the previously accepted stack-usage header")
    endif()
elseif(STACK_USAGE_SELF_TEST_CASE STREQUAL "fixed_point_merge")
    file(WRITE "${object_file}" "object\n")
    file(WRITE "${report_file}" "${static_record_f}${static_record_async}")
    run_stack_usage_check(success "" generate)
    file(WRITE "${report_file}"
        "${source_file}:10:1:r_f00000002\t32\tstatic\n"
        "${source_file}:4:1:r_async_step_00000001\t40\tstatic\n")
    run_stack_usage_check(success "" merge)
    file(READ "${header_file}" expanded_header)
    string(CONCAT expected_expanded_header
        "#ifndef R_GENERATED_STACK_USAGE_H\n"
        "#define R_GENERATED_STACK_USAGE_H\n"
        "\n"
        "#include <stddef.h>\n"
        "\n"
        "#define R_STACK_FRAME_r_async_step_00000001 ((size_t)48)\n"
        "#define R_STACK_FRAME_r_f00000002 ((size_t)32)\n"
        "\n"
        "#endif\n")
    if(NOT expanded_header STREQUAL expected_expanded_header)
        message(FATAL_ERROR
            "fixed-point merge did not preserve the per-function monotone maximum:\n"
            "${expanded_header}")
    endif()
    run_stack_usage_check(success "" merge)
    file(READ "${header_file}" stable_header)
    if(NOT stable_header STREQUAL expanded_header)
        message(FATAL_ERROR "stable fixed-point merge changed the bounds header")
    endif()
    run_stack_usage_check(success "" validate)
elseif(STACK_USAGE_SELF_TEST_CASE STREQUAL "final_under_bound")
    file(WRITE "${object_file}" "object\n")
    file(WRITE "${report_file}" "${static_record_f}${static_record_async}")
    run_stack_usage_check(success "" generate)
    file(WRITE "${report_file}"
        "${source_file}:10:1:r_f00000002\t16\tstatic\n"
        "${source_file}:4:1:r_async_step_00000001\t48\tstatic\n")
    run_stack_usage_check(success "" validate)
elseif(STACK_USAGE_SELF_TEST_CASE STREQUAL "final_over_bound")
    file(WRITE "${object_file}" "object\n")
    file(WRITE "${report_file}" "${static_record_f}${static_record_async}")
    run_stack_usage_check(success "" generate)
    file(WRITE "${report_file}"
        "${source_file}:10:1:r_f00000002\t25\tstatic\n"
        "${source_file}:4:1:r_async_step_00000001\t48\tstatic\n")
    run_stack_usage_check(
        failure
        "final generated frame 'r_f00000002' exceeds its measured bound"
        validate)
elseif(STACK_USAGE_SELF_TEST_CASE STREQUAL "final_name_mismatch")
    file(WRITE "${object_file}" "object\n")
    file(WRITE "${report_file}" "${static_record_f}${static_record_async}")
    run_stack_usage_check(success "" generate)
    file(WRITE "${report_file}"
        "${source_file}:10:1:r_f00000002\t24\tstatic\n"
        "${source_file}:4:1:r_async_step_00000003\t48\tstatic\n")
    run_stack_usage_check(
        failure
        "final generated frame name set does not match measured header"
        validate)
elseif(STACK_USAGE_SELF_TEST_CASE STREQUAL "wrapper_huge_frame")
    file(WRITE "${object_file}" "object\n")
    file(WRITE "${wrapper_file}" "int wrapper_main(void) { return 0; }\n")
    file(WRITE "${report_file}"
        "${wrapper_file}:1:1:wrapper_main\t1048576\tstatic\n"
        "${source_file}:1:1:r_f00000002\t32\tstatic\n")
    set(self_test_max_frame_size 64)
    run_stack_usage_check(success "" generate)
    file(READ "${header_file}" wrapper_header)
    string(FIND "${wrapper_header}" "R_STACK_FRAME_wrapper_main" wrapper_macro_offset)
    if(NOT wrapper_macro_offset EQUAL -1)
        message(FATAL_ERROR
            "non-generated wrapper frame leaked into generated stack bounds")
    endif()
    run_stack_usage_check(success "" validate)
else()
    message(FATAL_ERROR
        "unknown stack-usage self-test case: ${STACK_USAGE_SELF_TEST_CASE}")
endif()
