foreach(required_variable IN ITEMS
        R_FRONT_EXECUTABLE
        ALPHA_FIRST_SOURCE
        BRAVO_FIRST_SOURCE
        TARGET_MANIFEST
        OUTPUT_DIRECTORY)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")

function(run_frontend source_file emit_kind output_file)
    execute_process(
        COMMAND
            "${R_FRONT_EXECUTABLE}"
            "--emit=${emit_kind}"
            --profile hosted-native-async
            --target-manifest "${TARGET_MANIFEST}"
            "${source_file}"
        RESULT_VARIABLE frontend_status
        OUTPUT_FILE "${output_file}"
        ERROR_VARIABLE frontend_error
    )
    if(NOT frontend_status EQUAL 0)
        message(FATAL_ERROR
            "${emit_kind} failed for ${source_file} with status ${frontend_status}:\n"
            "${frontend_error}")
    endif()
endfunction()

set(alpha_interface "${OUTPUT_DIRECTORY}/alpha_first.interface.sexp")
set(bravo_interface "${OUTPUT_DIRECTORY}/bravo_first.interface.sexp")
set(alpha_mir "${OUTPUT_DIRECTORY}/alpha_first.mir.sexp")
set(bravo_mir "${OUTPUT_DIRECTORY}/bravo_first.mir.sexp")

run_frontend("${ALPHA_FIRST_SOURCE}" interface "${alpha_interface}")
run_frontend("${BRAVO_FIRST_SOURCE}" interface "${bravo_interface}")
run_frontend("${ALPHA_FIRST_SOURCE}" mir "${alpha_mir}")
run_frontend("${BRAVO_FIRST_SOURCE}" mir "${bravo_mir}")

file(READ "${alpha_interface}" alpha_interface_text)
file(READ "${bravo_interface}" bravo_interface_text)
if(NOT alpha_interface_text STREQUAL bravo_interface_text)
    message(FATAL_ERROR
        "permuting a checked error declaration changed the canonical interface:\n"
        "alpha-first:\n${alpha_interface_text}\n"
        "bravo-first:\n${bravo_interface_text}")
endif()

file(SHA256 "${alpha_interface}" alpha_interface_hash)
file(SHA256 "${bravo_interface}" bravo_interface_hash)
if(NOT alpha_interface_hash STREQUAL bravo_interface_hash)
    message(FATAL_ERROR
        "permuting a checked error declaration changed the interface fingerprint: "
        "${alpha_interface_hash} != ${bravo_interface_hash}")
endif()

file(READ "${alpha_mir}" alpha_mir_text)
file(READ "${bravo_mir}" bravo_mir_text)
if(NOT alpha_mir_text STREQUAL bravo_mir_text)
    message(FATAL_ERROR
        "permuting a checked error declaration changed the canonical MIR tag table")
endif()

string(REPLACE "\n" " " flattened_interface "${alpha_interface_text}")
string(FIND "${flattened_interface}"
    "function name=\"test.interface.checked_effect_order::choose\""
    function_position)
if(function_position EQUAL -1)
    message(FATAL_ERROR "checked-effect function is absent from the interface")
endif()
string(SUBSTRING "${flattened_interface}" ${function_position} -1 function_text)

foreach(required_fragment IN ITEMS
        "checked=(declaration=synchronous"
        "value=i32"
        "tag=0 role=success type=i32"
        "tag=1 role=checked_error"
        "tag=2 role=checked_error"
        "carrier=explicit_output_parameter"
        "target_bound=true")
    string(FIND "${function_text}" "${required_fragment}" fragment_position)
    if(fragment_position EQUAL -1)
        message(FATAL_ERROR
            "canonical checked interface is missing '${required_fragment}':\n"
            "${function_text}")
    endif()
endforeach()

string(REGEX MATCH
    "layout=[(]target_bound=true sha256=\"[0-9a-f]+\"[)]"
    layout_fingerprint "${function_text}")
if(layout_fingerprint STREQUAL "")
    message(FATAL_ERROR
        "checked carrier lacks a target-bound SHA-256 layout fingerprint:\n"
        "${function_text}")
endif()
string(REGEX REPLACE
    ".*sha256=\"([0-9a-f]+)\".*"
    "\\1"
    carrier_layout_hash
    "${layout_fingerprint}")
string(LENGTH "${carrier_layout_hash}" carrier_layout_hash_length)
if(NOT carrier_layout_hash_length EQUAL 64)
    message(FATAL_ERROR
        "checked carrier layout fingerprint is not a SHA-256 digest: "
        "${carrier_layout_hash}")
endif()

# The carrier layout identity is bound to the exact target-manifest bytes. A manifest that is not
# byte for byte the committed manifest of the selected profile, such as one with an extra trailing
# LF, is refused before any artifact is written (R-CONF-G005), so no artifact carries an identity
# that differs from its target.
set(target_manifest_variant "${OUTPUT_DIRECTORY}/target-manifest-variant.json")
file(READ "${TARGET_MANIFEST}" target_manifest_text)
file(WRITE "${target_manifest_variant}" "${target_manifest_text}\n")
execute_process(
    COMMAND
        "${R_FRONT_EXECUTABLE}"
        --emit=interface
        --profile hosted-native-async
        --target-manifest "${target_manifest_variant}"
        "${ALPHA_FIRST_SOURCE}"
    RESULT_VARIABLE target_bound_status
    OUTPUT_VARIABLE target_bound_output
    ERROR_VARIABLE target_bound_error
)
if(NOT target_bound_status EQUAL 2 OR
        NOT target_bound_error MATCHES "--target-manifest is not the target manifest")
    message(FATAL_ERROR
        "a byte-distinct target manifest was not refused (${target_bound_status}):\n"
        "${target_bound_error}")
endif()

string(FIND "${function_text}" "errors=(" errors_position)
string(FIND "${function_text}" "alpha_error" alpha_error_position)
string(FIND "${function_text}" "bravo_error" bravo_error_position)
if(errors_position EQUAL -1 OR
        alpha_error_position EQUAL -1 OR
        bravo_error_position EQUAL -1 OR
        NOT errors_position LESS alpha_error_position OR
        NOT alpha_error_position LESS bravo_error_position)
    message(FATAL_ERROR
        "checked errors are not in canonical alpha_error, bravo_error order:\n"
        "${function_text}")
endif()

foreach(mir_fragment IN ITEMS
        "error=(struct \"test.interface.checked_effect_order\"::\"alpha_error\") propagate_tag=1"
        "error=(struct \"test.interface.checked_effect_order\"::\"bravo_error\") propagate_tag=2")
    string(FIND "${alpha_mir_text}" "${mir_fragment}" mir_fragment_position)
    if(mir_fragment_position EQUAL -1)
        message(FATAL_ERROR
            "canonical MIR tag table is missing '${mir_fragment}':\n"
            "${alpha_mir_text}")
    endif()
endforeach()
