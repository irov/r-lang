if(NOT DEFINED R_FRONT_EXECUTABLE OR NOT DEFINED SOURCE_FILE OR
   NOT DEFINED TARGET_MANIFEST OR NOT DEFINED UNSUPPORTED_TARGET_MANIFEST OR
   NOT DEFINED LINK_MANIFEST)
    message(FATAL_ERROR
            "R_FRONT_EXECUTABLE, SOURCE_FILE, TARGET_MANIFEST, UNSUPPORTED_TARGET_MANIFEST and "
            "LINK_MANIFEST are required")
endif()

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=mir
        --profile hosted
        --profile hosted-thread
        "${SOURCE_FILE}"
    RESULT_VARIABLE duplicate_result
    OUTPUT_VARIABLE duplicate_stdout
    ERROR_VARIABLE duplicate_stderr
)
if(NOT duplicate_result EQUAL 2)
    message(FATAL_ERROR "duplicate option returned ${duplicate_result}")
endif()
if(NOT duplicate_stderr MATCHES
   "r-front: duplicate option: --profile")
    message(FATAL_ERROR "unexpected duplicate diagnostic: ${duplicate_stderr}")
endif()

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=mir
        --profile imaginary
        "${SOURCE_FILE}"
    RESULT_VARIABLE profile_result
    OUTPUT_VARIABLE profile_stdout
    ERROR_VARIABLE profile_stderr
)
if(NOT profile_result EQUAL 2)
    message(FATAL_ERROR "unknown profile returned ${profile_result}")
endif()
if(NOT profile_stderr MATCHES "r-front: unknown profile: imaginary")
    message(FATAL_ERROR "unexpected profile diagnostic: ${profile_stderr}")
endif()

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=link-plan
        "${SOURCE_FILE}"
    RESULT_VARIABLE entry_result
    OUTPUT_VARIABLE entry_stdout
    ERROR_VARIABLE entry_stderr
)
if(NOT entry_result EQUAL 2)
    message(FATAL_ERROR "missing entry returned ${entry_result}")
endif()
if(NOT entry_stderr MATCHES
   "r-front: --entry is required for selected emit")
    message(FATAL_ERROR "unexpected entry diagnostic: ${entry_stderr}")
endif()

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=interface
        --profile hosted
        "${SOURCE_FILE}"
    RESULT_VARIABLE interface_result
    OUTPUT_VARIABLE interface_stdout
    ERROR_VARIABLE interface_stderr
)
if(NOT interface_result EQUAL 0)
    message(FATAL_ERROR
            "interface emit failed: ${interface_result}: ${interface_stderr}")
endif()
if(NOT interface_stdout MATCHES "codegen.main::main")
    message(FATAL_ERROR "interface artifact is missing main")
endif()

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=hir
        --profile hosted
        --target-manifest "${UNSUPPORTED_TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE unsupported_result
    OUTPUT_VARIABLE unsupported_stdout
    ERROR_VARIABLE unsupported_stderr
)
if(NOT unsupported_result EQUAL 2)
    message(FATAL_ERROR "unsupported target manifest returned ${unsupported_result}")
endif()
if(NOT unsupported_stderr MATCHES
   "r-front: --target-manifest is not the target manifest of this implementation for profile hosted")
    message(FATAL_ERROR "unexpected target manifest diagnostic: ${unsupported_stderr}")
endif()

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=link-plan
        --entry codegen.main::main
        --profile hosted
        --target-manifest "${TARGET_MANIFEST}"
        --link-manifest "${LINK_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE plan_result
    OUTPUT_VARIABLE plan_stdout
    ERROR_VARIABLE plan_stderr
)
if(NOT plan_result EQUAL 0)
    message(FATAL_ERROR
            "link-plan emit failed: ${plan_result}: ${plan_stderr}")
endif()
if(NOT plan_stdout MATCHES
   "target-manifest present=true length=[0-9]+ sha256=\"[0-9a-f]+\"")
    message(FATAL_ERROR "target manifest fingerprint is missing: ${plan_stdout}")
endif()
if(NOT plan_stdout MATCHES
   "link-manifest present=true length=[0-9]+ sha256=\"[0-9a-f]+\"")
    message(FATAL_ERROR "link manifest fingerprint is missing: ${plan_stdout}")
endif()

get_filename_component(fixture_directory "${SOURCE_FILE}" DIRECTORY)

# R-MOD-0007: an import that the module map does not declare is a diagnostic at the import.
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=hir
        --module-map "${fixture_directory}/compiler_missing_import.modules.map"
        --entry compiler.missing_import::main
    RESULT_VARIABLE missing_import_result
    OUTPUT_VARIABLE missing_import_stdout
    ERROR_VARIABLE missing_import_stderr
)
if(NOT missing_import_result EQUAL 1 OR NOT missing_import_stderr MATCHES
   "compiler_missing_import.r:4:8: error R-DIAG-MOD-001 \\[R-MOD-0007\\]")
    message(FATAL_ERROR
            "absent imported module was not diagnosed at its import "
            "(${missing_import_result}): ${missing_import_stderr}")
endif()

# R-BORROW-0024: a rejection by the MIR await-liveness check is not an implementation slice.
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=llvm-ir
        --profile hosted-native-async
        "${fixture_directory}/compiler_await_borrow_argument.r"
    RESULT_VARIABLE await_borrow_result
    OUTPUT_VARIABLE await_borrow_stdout
    ERROR_VARIABLE await_borrow_stderr
)
if(NOT await_borrow_result EQUAL 1 OR
   NOT await_borrow_stderr MATCHES "R-DIAG-ASYNC-001 \\[R-BORROW-0024\\]" OR
   await_borrow_stderr MATCHES "R-DIAG-SLICE-001")
    message(FATAL_ERROR
            "await-liveness rejection was reported as a slice "
            "(${await_borrow_result}): ${await_borrow_stderr}")
endif()

# Programs are emitted as LLVM IR or objects only; the former C program artifact is no longer
# an emit kind, so asking for it is a usage error rather than a silently different artifact.
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        "${SOURCE_FILE}"
    RESULT_VARIABLE removed_emit_result
    OUTPUT_VARIABLE removed_emit_stdout
    ERROR_VARIABLE removed_emit_stderr
)
if(NOT removed_emit_result EQUAL 2 OR
   NOT removed_emit_stderr MATCHES "r-front: unknown option: --emit=c17" OR
   NOT removed_emit_stdout STREQUAL "")
    message(FATAL_ERROR
            "--emit=c17 was not rejected as an unknown option "
            "(${removed_emit_result}): ${removed_emit_stderr}")
endif()

# --opt-level selects the LLVM pipeline of the program (B7): the emitter's own module at 0 and
# default<O2> when the option is absent; a level is given once and is 0, 1, 2 or 3.
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=llvm-ir --opt-level=1 --opt-level=2 "${SOURCE_FILE}"
    RESULT_VARIABLE level_duplicate_result
    OUTPUT_VARIABLE level_duplicate_stdout
    ERROR_VARIABLE level_duplicate_stderr
)
if(NOT level_duplicate_result EQUAL 2 OR
   NOT level_duplicate_stderr MATCHES "r-front: duplicate option: --opt-level")
    message(FATAL_ERROR
            "a repeated --opt-level was not rejected "
            "(${level_duplicate_result}): ${level_duplicate_stderr}")
endif()
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=llvm-ir --opt-level=4 "${SOURCE_FILE}"
    RESULT_VARIABLE level_invalid_result
    OUTPUT_VARIABLE level_invalid_stdout
    ERROR_VARIABLE level_invalid_stderr
)
if(NOT level_invalid_result EQUAL 2 OR
   NOT level_invalid_stderr MATCHES "r-front: expected 0, 1, 2 or 3: --opt-level=4")
    message(FATAL_ERROR
            "--opt-level=4 was not rejected (${level_invalid_result}): ${level_invalid_stderr}")
endif()
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=llvm-ir --opt-level=0 "${SOURCE_FILE}"
    RESULT_VARIABLE level_zero_result
    OUTPUT_VARIABLE level_zero_ir
    ERROR_VARIABLE level_zero_stderr
)
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=llvm-ir "${SOURCE_FILE}"
    RESULT_VARIABLE level_default_result
    OUTPUT_VARIABLE level_default_ir
    ERROR_VARIABLE level_default_stderr
)
if(NOT level_zero_result EQUAL 0 OR NOT level_default_result EQUAL 0)
    message(FATAL_ERROR
            "--opt-level=0 or the default level failed: ${level_zero_stderr}${level_default_stderr}")
endif()
# The optimizer runs only above level 0, after the stack bookkeeping is kept from it.
if(level_zero_ir MATCHES "llvm\\.compiler\\.used" OR
   NOT level_default_ir MATCHES "@llvm\\.compiler\\.used = appending global" OR
   NOT level_default_ir MATCHES "@r_stack_entry\\.[0-9]+ = private externally_initialized constant")
    message(FATAL_ERROR "the default level did not optimize the program:\n${level_default_ir}")
endif()

# B7.2: profile-guided optimization needs an optimizing level, and generating and using a profile
# exclude each other.
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=llvm-ir --opt-level=0 --profile-generate "${SOURCE_FILE}"
    RESULT_VARIABLE profile_level_result
    OUTPUT_VARIABLE profile_level_stdout
    ERROR_VARIABLE profile_level_stderr
)
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=llvm-ir --profile-generate --profile-use missing.profdata
        "${SOURCE_FILE}"
    RESULT_VARIABLE profile_both_result
    OUTPUT_VARIABLE profile_both_stdout
    ERROR_VARIABLE profile_both_stderr
)
if(NOT profile_level_result EQUAL 2 OR NOT profile_both_result EQUAL 2 OR
   NOT profile_level_stderr MATCHES "--profile-generate and --profile-use exclude each other" OR
   NOT profile_both_stderr MATCHES "--profile-generate and --profile-use exclude each other")
    message(FATAL_ERROR
            "profile options were not rejected (${profile_level_result}, ${profile_both_result}): "
            "${profile_level_stderr}${profile_both_stderr}")
endif()
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=llvm-ir --profile-generate "${SOURCE_FILE}"
    RESULT_VARIABLE profile_generate_result
    OUTPUT_VARIABLE profile_generate_ir
    ERROR_VARIABLE profile_generate_stderr
)
if(NOT profile_generate_result EQUAL 0 OR
   NOT profile_generate_ir MATCHES "__profc_|__llvm_profile")
    message(FATAL_ERROR
            "--profile-generate did not instrument the program "
            "(${profile_generate_result}): ${profile_generate_stderr}")
endif()
