if(NOT DEFINED FRONTEND_COMPILE_OPTIONS OR
   NOT DEFINED JSON_COMPILE_OPTIONS)
    message(FATAL_ERROR "missing fuzzer dependency compile options")
endif()

function(require_fuzzer_instrumentation target_name compile_options)
    if(NOT compile_options MATCHES "(^|\\|)-fsanitize=fuzzer(-no-link)?($|\\|)")
        message(FATAL_ERROR
            "${target_name} is linked into a libFuzzer target without coverage instrumentation: "
            "${compile_options}")
    endif()
endfunction()

require_fuzzer_instrumentation("r_frontend" "${FRONTEND_COMPILE_OPTIONS}")
require_fuzzer_instrumentation("r_std_json" "${JSON_COMPILE_OPTIONS}")
