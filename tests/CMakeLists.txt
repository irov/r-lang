add_executable(r_frontend_tests frontend_tests.c)
target_link_libraries(r_frontend_tests PRIVATE r_frontend)

if(MSVC)
    target_compile_options(r_frontend_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_frontend_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

add_test(NAME r_frontend_unit COMMAND r_frontend_tests)

add_executable(r_frontend_internal_tests frontend_internal_tests.c)
target_link_libraries(r_frontend_internal_tests PRIVATE r_frontend)
target_include_directories(r_frontend_internal_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/compiler/source"
)

if(MSVC)
    target_compile_options(r_frontend_internal_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_frontend_internal_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

add_test(NAME r_frontend_internal_unit COMMAND r_frontend_internal_tests)

add_executable(r_library_bytes_tests library_bytes_tests.c)
target_link_libraries(r_library_bytes_tests PRIVATE r_std_bytes)

if(MSVC)
    target_compile_options(r_library_bytes_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_bytes_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_bytes_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_bytes_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_bytes_unit COMMAND r_library_bytes_tests)

add_executable(r_library_secret_tests library_secret_tests.c)
target_link_libraries(r_library_secret_tests PRIVATE r_std_secret)

if(MSVC)
    target_compile_options(r_library_secret_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_secret_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_secret_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_secret_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_secret_unit COMMAND r_library_secret_tests)

# R-SLIB-RANDOM-0001 (M20): the operating-system generator behind std.random::fill.
add_executable(r_library_random_tests library_random_tests.c)
target_link_libraries(r_library_random_tests PRIVATE r_std_random)

if(MSVC)
    target_compile_options(r_library_random_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_random_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_random_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_random_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_random_unit COMMAND r_library_random_tests)

add_executable(r_library_hash_tests library_hash_tests.c)
target_link_libraries(r_library_hash_tests PRIVATE r_std_hash)

if(MSVC)
    target_compile_options(r_library_hash_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_hash_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_hash_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_hash_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_hash_unit COMMAND r_library_hash_tests)

add_executable(r_library_bits_tests library_bits_tests.c)
target_link_libraries(r_library_bits_tests PRIVATE r_std_bits)

if(MSVC)
    target_compile_options(r_library_bits_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_bits_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_bits_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_bits_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_bits_unit COMMAND r_library_bits_tests)

add_executable(r_library_utf8_tests library_utf8_tests.c)
target_link_libraries(r_library_utf8_tests PRIVATE r_std_utf8)

if(MSVC)
    target_compile_options(r_library_utf8_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_utf8_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_utf8_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_utf8_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_utf8_unit COMMAND r_library_utf8_tests)

add_executable(r_library_core_tests library_core_tests.c)
target_link_libraries(r_library_core_tests PRIVATE r_core)

if(MSVC)
    target_compile_options(r_library_core_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_core_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_core_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_core_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_core_unit COMMAND r_library_core_tests)

add_executable(r_library_error_tests library_error_tests.c)
target_link_libraries(r_library_error_tests PRIVATE r_std_error)

if(MSVC)
    target_compile_options(r_library_error_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_error_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_error_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_error_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_error_unit COMMAND r_library_error_tests)

add_executable(r_library_convert_tests library_convert_tests.c)
target_link_libraries(r_library_convert_tests PRIVATE r_std_convert)

if(MSVC)
    target_compile_options(r_library_convert_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_convert_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_convert_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_convert_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_convert_unit COMMAND r_library_convert_tests)

add_executable(r_library_checked_convert_tests library_checked_convert_tests.c)
target_link_libraries(r_library_checked_convert_tests PRIVATE r_std_c)

if(MSVC)
    target_compile_options(r_library_checked_convert_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_checked_convert_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_checked_convert_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_checked_convert_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_checked_convert_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_checked_convert_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_checked_convert_unit COMMAND r_library_checked_convert_tests)

add_executable(r_library_format_tests library_format_tests.c)
target_link_libraries(r_library_format_tests PRIVATE r_std_format)

if(MSVC)
    target_compile_options(r_library_format_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_format_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_format_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_format_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_format_unit COMMAND r_library_format_tests)

# Library R-SLIB-RSRC-0003: the headers and archives of every native provider of the library
# (std.tls, std.crypto, std.sqlite, std.alloc). A program links those its generated code bridges
# to.
set(R_STD_NATIVE_INCLUDE_DIRS
    "${R_STD_TLS_NATIVE_INCLUDE_DIR};${R_STD_CRYPTO_NATIVE_INCLUDE_DIR};${R_STD_SQLITE_NATIVE_INCLUDE_DIR};${R_STD_ALLOC_NATIVE_INCLUDE_DIR};${R_STD_FS_NATIVE_INCLUDE_DIR}")
set(R_STD_NATIVE_LINK_ARCHIVES
    "${R_STD_TLS_NATIVE_LINK_ARCHIVES};${R_STD_CRYPTO_NATIVE_LINK_ARCHIVES};${R_STD_SQLITE_NATIVE_LINK_ARCHIVES};${R_STD_ALLOC_NATIVE_LINK_ARCHIVES};${R_STD_FS_NATIVE_LINK_ARCHIVES}")

# Library R-SLIB-CRYPTO-0001 (M28): the native provider of std.crypto over libsodium, each
# primitive against a known answer of its RFC.
add_executable(r_library_crypto_native_tests library_crypto_native_tests.c)
target_link_libraries(r_library_crypto_native_tests PRIVATE r_std_crypto_native)
if(MSVC)
    target_compile_options(r_library_crypto_native_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_crypto_native_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()
if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_crypto_native_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(r_library_crypto_native_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()
if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_crypto_native_tests PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(r_library_crypto_native_tests PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
endif()
add_test(NAME r_library_crypto_native_unit COMMAND r_library_crypto_native_tests)

# Library R-SLIB-SQLITE-0001 (M29): the native provider of std.sqlite over the SQLite library of
# the system, with an in-memory database.
add_executable(r_library_sqlite_native_tests library_sqlite_native_tests.c)
target_link_libraries(r_library_sqlite_native_tests PRIVATE r_std_sqlite_native r_runtime_0_1)
if(MSVC)
    target_compile_options(r_library_sqlite_native_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_sqlite_native_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()
if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_sqlite_native_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(r_library_sqlite_native_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()
if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_sqlite_native_tests PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(r_library_sqlite_native_tests PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
endif()
add_test(NAME r_library_sqlite_native_unit COMMAND r_library_sqlite_native_tests)

# Library R-SLIB-ALLOC-0004 (M31): the native provider of std.alloc::budget_usage over the budgets
# of the runtime.
add_executable(r_library_alloc_native_tests library_alloc_native_tests.c)
target_link_libraries(r_library_alloc_native_tests PRIVATE r_std_alloc_native r_runtime_0_1)
target_include_directories(r_library_alloc_native_tests PRIVATE "${PROJECT_SOURCE_DIR}/runtime/include")
if(MSVC)
    target_compile_options(r_library_alloc_native_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_alloc_native_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()
if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_alloc_native_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(r_library_alloc_native_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()
if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_alloc_native_tests PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(r_library_alloc_native_tests PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
endif()
add_test(NAME r_library_alloc_native_unit COMMAND r_library_alloc_native_tests)

# Library R-SLIB-TLS-0001 (M25): the native provider of std.tls over Mbed TLS 3.6, exchanging
# records between a client and a server in memory with the certificates of fixtures/tls.
add_executable(r_library_tls_native_tests library_tls_native_tests.c)
target_link_libraries(r_library_tls_native_tests PRIVATE r_std_tls_native r_runtime_0_1)
if(MSVC)
    target_compile_options(r_library_tls_native_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_tls_native_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()
if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_tls_native_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()
if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_tls_native_tests PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
endif()
add_test(NAME r_library_tls_native_unit
    COMMAND r_library_tls_native_tests "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tls")
set_tests_properties(r_library_tls_native_unit PROPERTIES LABELS "library;tls")
# Library R-SLIB-RSRC-0003 (M25): a program manifest may not name a provider of the library.
add_test(NAME r_frontend_library_links_collision
    COMMAND r-front --emit=link-plan
        --library-map "${PROJECT_SOURCE_DIR}/library/r/library.map"
        --module-map "${PROJECT_SOURCE_DIR}/examples/tls/modules.map"
        --entry example.tls.main
        --link-manifest "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/library_links_collision.json")
set_tests_properties(r_frontend_library_links_collision PROPERTIES
    PASS_REGULAR_EXPRESSION "--link-manifest names a logical name of the library link manifest"
    LABELS "library;tls;link")

add_executable(r_library_json_tests library_json_tests.c)
target_link_libraries(r_library_json_tests PRIVATE r_std_json)

if(MSVC)
    target_compile_options(r_library_json_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_json_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_json_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_json_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_json_unit COMMAND r_library_json_tests)

add_executable(r_library_math_tests library_math_tests.c)
target_link_libraries(r_library_math_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_unit COMMAND r_library_math_tests)

add_executable(r_library_math_sqrt_tests library_math_sqrt_tests.c)
target_link_libraries(r_library_math_sqrt_tests PRIVATE r_std_math)

if(MSVC)
    target_compile_options(r_library_math_sqrt_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_sqrt_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_sqrt_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_sqrt_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_sqrt_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_sqrt_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_sqrt_unit COMMAND r_library_math_sqrt_tests)

add_executable(r_library_math_cbrt_tests library_math_cbrt_tests.c)
target_link_libraries(r_library_math_cbrt_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_cbrt_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_cbrt_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_cbrt_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_cbrt_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_cbrt_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_cbrt_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_cbrt_unit COMMAND r_library_math_cbrt_tests)

add_executable(r_library_math_acos_tests library_math_acos_tests.c)
target_link_libraries(r_library_math_acos_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_acos_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_acos_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_acos_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_acos_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_acos_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_acos_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_acos_unit COMMAND r_library_math_acos_tests)

add_executable(r_library_math_acosh_tests library_math_acosh_tests.c)
target_link_libraries(r_library_math_acosh_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_acosh_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_acosh_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_acosh_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_acosh_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_acosh_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_acosh_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_acosh_unit COMMAND r_library_math_acosh_tests)

add_executable(r_library_math_asin_tests library_math_asin_tests.c)
target_link_libraries(r_library_math_asin_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_asin_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_asin_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_asin_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_asin_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_asin_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_asin_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_asin_unit COMMAND r_library_math_asin_tests)

add_executable(r_library_math_asinh_tests library_math_asinh_tests.c)
target_link_libraries(r_library_math_asinh_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_asinh_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_asinh_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_asinh_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_asinh_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_asinh_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_asinh_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_asinh_unit COMMAND r_library_math_asinh_tests)

add_executable(r_library_math_atan_tests library_math_atan_tests.c)
target_link_libraries(r_library_math_atan_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_atan_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_atan_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_atan_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_atan_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_atan_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_atan_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_atan_unit COMMAND r_library_math_atan_tests)

add_executable(r_library_math_atan2_tests library_math_atan2_tests.c)
target_link_libraries(r_library_math_atan2_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_atan2_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_atan2_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_atan2_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_atan2_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_atan2_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_atan2_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_atan2_unit COMMAND r_library_math_atan2_tests)

add_executable(r_library_math_pow_tests library_math_pow_tests.c)
target_link_libraries(r_library_math_pow_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_pow_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_pow_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_pow_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_pow_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_pow_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_pow_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_pow_unit COMMAND r_library_math_pow_tests)

add_executable(r_library_math_hypot_tests library_math_hypot_tests.c)
target_link_libraries(r_library_math_hypot_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_hypot_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_hypot_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_hypot_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_hypot_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_hypot_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_hypot_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_hypot_unit COMMAND r_library_math_hypot_tests)

add_executable(r_library_math_remainder_tests library_math_remainder_tests.c)
target_link_libraries(r_library_math_remainder_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_remainder_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_remainder_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_remainder_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_remainder_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_remainder_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_remainder_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_remainder_unit COMMAND r_library_math_remainder_tests)

add_executable(r_library_math_atanh_tests library_math_atanh_tests.c)
target_link_libraries(r_library_math_atanh_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_atanh_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_atanh_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_atanh_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_atanh_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_atanh_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_atanh_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_atanh_unit COMMAND r_library_math_atanh_tests)

add_executable(r_library_math_tanh_tests library_math_tanh_tests.c)
target_link_libraries(r_library_math_tanh_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_tanh_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_tanh_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_tanh_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_tanh_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_tanh_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_tanh_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_tanh_unit COMMAND r_library_math_tanh_tests)

add_executable(r_library_math_erf_tests library_math_erf_tests.c)
target_link_libraries(r_library_math_erf_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_erf_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_erf_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_erf_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_erf_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_erf_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_erf_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_erf_unit COMMAND r_library_math_erf_tests)

add_executable(r_library_math_erfc_tests library_math_erfc_tests.c)
target_link_libraries(r_library_math_erfc_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_erfc_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_erfc_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_erfc_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_erfc_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_erfc_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_erfc_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_erfc_unit COMMAND r_library_math_erfc_tests)

add_executable(r_library_math_exp2_tests library_math_exp2_tests.c)
target_link_libraries(r_library_math_exp2_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_exp2_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_exp2_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_exp2_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_exp2_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_exp2_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_exp2_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_exp2_unit COMMAND r_library_math_exp2_tests)

add_executable(r_library_math_exp_tests library_math_exp_tests.c)
target_link_libraries(r_library_math_exp_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_exp_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_exp_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_exp_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_exp_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_exp_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_exp_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_exp_unit COMMAND r_library_math_exp_tests)

add_executable(r_library_math_expm1_tests library_math_expm1_tests.c)
target_link_libraries(r_library_math_expm1_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_expm1_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_expm1_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_expm1_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_expm1_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_expm1_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_expm1_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_expm1_unit COMMAND r_library_math_expm1_tests)

add_executable(r_library_math_gamma_tests library_math_gamma_tests.c)
target_link_libraries(r_library_math_gamma_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_gamma_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_gamma_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_gamma_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_gamma_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_gamma_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_gamma_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_gamma_unit COMMAND r_library_math_gamma_tests)

add_executable(r_library_math_log_gamma_tests library_math_log_gamma_tests.c)
target_link_libraries(r_library_math_log_gamma_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_log_gamma_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_log_gamma_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_log_gamma_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_log_gamma_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_log_gamma_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_log_gamma_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_log_gamma_unit COMMAND r_library_math_log_gamma_tests)

add_executable(r_library_math_log_tests library_math_log_tests.c)
target_link_libraries(r_library_math_log_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_log_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_log_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_log_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_log_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_log_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_log_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_log_unit COMMAND r_library_math_log_tests)

add_executable(r_library_math_log2_tests library_math_log2_tests.c)
target_link_libraries(r_library_math_log2_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_log2_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_log2_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_log2_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_log2_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_log2_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_log2_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_log2_unit COMMAND r_library_math_log2_tests)

add_executable(r_library_math_log10_tests library_math_log10_tests.c)
target_link_libraries(r_library_math_log10_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_log10_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_log10_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_log10_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_log10_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_log10_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_log10_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_log10_unit COMMAND r_library_math_log10_tests)

add_executable(r_library_math_log1p_tests library_math_log1p_tests.c)
target_link_libraries(r_library_math_log1p_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_log1p_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_log1p_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_log1p_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_log1p_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_log1p_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_log1p_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_log1p_unit COMMAND r_library_math_log1p_tests)

add_executable(r_library_math_sin_tests library_math_sin_tests.c)
target_link_libraries(r_library_math_sin_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_sin_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_sin_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_sin_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_sin_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_sin_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_sin_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_sin_unit COMMAND r_library_math_sin_tests)

add_executable(r_library_math_sinh_tests library_math_sinh_tests.c)
target_link_libraries(r_library_math_sinh_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_sinh_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_sinh_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_sinh_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_sinh_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_sinh_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_sinh_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_sinh_unit COMMAND r_library_math_sinh_tests)

add_executable(r_library_math_tan_tests library_math_tan_tests.c)
target_link_libraries(r_library_math_tan_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_tan_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_tan_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_tan_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_tan_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_tan_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_tan_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_tan_unit COMMAND r_library_math_tan_tests)

add_executable(r_library_math_cosh_tests library_math_cosh_tests.c)
target_link_libraries(r_library_math_cosh_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_cosh_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_cosh_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_cosh_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_cosh_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_cosh_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_cosh_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_cosh_unit COMMAND r_library_math_cosh_tests)

add_executable(r_library_math_cos_tests library_math_cos_tests.c)
target_link_libraries(r_library_math_cos_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_cos_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_cos_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_cos_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_cos_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_cos_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_cos_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_cos_unit COMMAND r_library_math_cos_tests)

add_executable(r_library_math_complex_tests library_math_complex_tests.c)
target_link_libraries(r_library_math_complex_tests PRIVATE r_std_math Threads::Threads)

if(MSVC)
    target_compile_options(r_library_math_complex_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_math_complex_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_math_complex_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_complex_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_math_complex_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_math_complex_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_math_complex_unit COMMAND r_library_math_complex_tests)

add_executable(r_library_c_tests library_c_tests.c)
target_link_libraries(r_library_c_tests PRIVATE r_std_c)

if(MSVC)
    target_compile_options(r_library_c_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_c_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_c_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_c_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_c_unit COMMAND r_library_c_tests)

add_executable(r_link_manifest_tests link_manifest_tests.c)
target_link_libraries(r_link_manifest_tests PRIVATE r_frontend r_std_c)
target_include_directories(r_link_manifest_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/compiler/source"
)
if(MSVC)
    target_compile_options(r_link_manifest_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_link_manifest_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()
if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_link_manifest_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_link_manifest_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()
if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_link_manifest_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_link_manifest_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()
add_test(NAME r_link_manifest_unit COMMAND r_link_manifest_tests)

add_executable(r_library_env_tests library_env_tests.c)
target_link_libraries(r_library_env_tests PRIVATE r_std_env)

if(MSVC)
    target_compile_options(r_library_env_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_env_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_env_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_env_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_env_unit COMMAND r_library_env_tests)

add_executable(r_library_alloc_tests library_alloc_tests.c)
target_link_libraries(r_library_alloc_tests PRIVATE r_std_alloc)

if(MSVC)
    target_compile_options(r_library_alloc_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_alloc_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_alloc_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_alloc_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_alloc_unit COMMAND r_library_alloc_tests)

add_executable(r_library_string_tests library_string_tests.c)
target_link_libraries(r_library_string_tests PRIVATE r_std_string)

if(MSVC)
    target_compile_options(r_library_string_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_string_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_string_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_string_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_string_unit COMMAND r_library_string_tests)

add_executable(r_library_shared_owner_tests library_shared_owner_tests.c)
target_link_libraries(r_library_shared_owner_tests PRIVATE r_std_arc r_std_rc)

if(MSVC)
    target_compile_options(r_library_shared_owner_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_shared_owner_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_shared_owner_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_shared_owner_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_shared_owner_unit COMMAND r_library_shared_owner_tests)

add_executable(r_library_time_duration_tests library_time_duration_tests.c)
target_link_libraries(r_library_time_duration_tests PRIVATE r_std_time)

if(MSVC)
    target_compile_options(r_library_time_duration_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_time_duration_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_time_duration_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_time_duration_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_time_duration_unit COMMAND r_library_time_duration_tests)

find_package(Threads REQUIRED)
add_executable(r_library_time_clock_tests library_time_clock_tests.c)
target_link_libraries(r_library_time_clock_tests PRIVATE r_std_time Threads::Threads)
target_include_directories(r_library_time_clock_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/library/internal/time/include"
)

if(MSVC)
    target_compile_options(r_library_time_clock_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_time_clock_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_time_clock_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_time_clock_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_time_clock_unit COMMAND r_library_time_clock_tests)

add_executable(r_library_time_async_tests library_time_async_tests.c)
target_link_libraries(r_library_time_async_tests PRIVATE r_std_time Threads::Threads)
if(APPLE)
    target_compile_definitions(r_library_time_async_tests PRIVATE R_RUNTIME_DARWIN_TIMER_TESTING=1)
endif()

if(MSVC)
    target_compile_options(r_library_time_async_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_time_async_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_time_async_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_time_async_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_time_async_unit COMMAND r_library_time_async_tests)
set_tests_properties(r_library_time_async_unit PROPERTIES TIMEOUT 10)

if(APPLE)
    add_executable(r_library_json_reader_tests library_json_reader_tests.c)
    target_link_libraries(r_library_json_reader_tests PRIVATE r_std_json r_std_fs r_std_net r_std_io Threads::Threads)
    target_compile_options(r_library_json_reader_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes)
    if(R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS)
        target_compile_options(r_library_json_reader_tests PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(r_library_json_reader_tests PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
    endif()
    add_test(NAME r_library_json_reader_unit COMMAND r_library_json_reader_tests)

    add_executable(r_library_io_tests library_io_tests.c)
    target_compile_definitions(r_library_io_tests PRIVATE R_RUNTIME_DARWIN_IO_TESTING=1)
    target_link_libraries(r_library_io_tests PRIVATE r_std_io Threads::Threads)

    if(MSVC)
        target_compile_options(r_library_io_tests PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(r_library_io_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror
            -Wconversion -Wsign-conversion -Wshadow
            -Wstrict-prototypes -Wmissing-prototypes
        )
    endif()

    if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
        target_compile_options(r_library_io_tests PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(r_library_io_tests PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
    endif()

    if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
        target_compile_options(r_library_io_tests PRIVATE
            -fsanitize=thread
            -fno-omit-frame-pointer
        )
        target_link_options(r_library_io_tests PRIVATE
            -fsanitize=thread
            -fno-omit-frame-pointer
        )
    endif()

    add_test(NAME r_library_io_unit COMMAND r_library_io_tests)
    set_tests_properties(r_library_io_unit PROPERTIES TIMEOUT 20)

    add_executable(r_library_io_scoped_tests library_io_scoped_tests.c)
    target_link_libraries(r_library_io_scoped_tests PRIVATE r_std_io Threads::Threads)

    if(MSVC)
        target_compile_options(r_library_io_scoped_tests PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(r_library_io_scoped_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror
            -Wconversion -Wsign-conversion -Wshadow
            -Wstrict-prototypes -Wmissing-prototypes
        )
    endif()

    if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
        target_compile_options(r_library_io_scoped_tests PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(r_library_io_scoped_tests PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
    endif()

    if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
        target_compile_options(r_library_io_scoped_tests PRIVATE
            -fsanitize=thread
            -fno-omit-frame-pointer
        )
        target_link_options(r_library_io_scoped_tests PRIVATE
            -fsanitize=thread
            -fno-omit-frame-pointer
        )
    endif()

    add_test(NAME r_library_io_scoped_unit COMMAND r_library_io_scoped_tests)
    set_tests_properties(r_library_io_scoped_unit PROPERTIES TIMEOUT 20)
endif()

add_executable(r_library_array_tests library_array_tests.c)
target_link_libraries(r_library_array_tests PRIVATE r_std_array)

if(MSVC)
    target_compile_options(r_library_array_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_array_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_array_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_array_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_array_unit COMMAND r_library_array_tests)

add_executable(r_library_list_tests library_list_tests.c)
target_link_libraries(r_library_list_tests PRIVATE r_std_list)

if(MSVC)
    target_compile_options(r_library_list_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_list_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_list_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_list_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_list_unit COMMAND r_library_list_tests)

add_executable(r_library_dict_tests library_dict_tests.c)
target_link_libraries(r_library_dict_tests PRIVATE r_std_dict)

if(MSVC)
    target_compile_options(r_library_dict_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_dict_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_dict_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_dict_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_dict_unit COMMAND r_library_dict_tests)

add_executable(r_frontend_semantic_tests semantic_tests.c)
target_link_libraries(r_frontend_semantic_tests PRIVATE r_frontend)
target_include_directories(r_frontend_semantic_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/compiler/source"
)
target_compile_definitions(r_frontend_semantic_tests PRIVATE
    R_SEMANTIC_ALPHA_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_alpha.r"
    R_SEMANTIC_BETA_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_beta.r"
    R_SEMANTIC_GOLDEN_PATH="${CMAKE_CURRENT_SOURCE_DIR}/golden/semantic.hir.sexp"
    R_SEMANTIC_BODY_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_body.r"
    R_SEMANTIC_BODY_GOLDEN_PATH="${CMAKE_CURRENT_SOURCE_DIR}/golden/semantic_body.hir.sexp"
    R_SEMANTIC_MOVE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/move_values.r"
    R_SEMANTIC_MOVE_NEGATIVE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/move_values_negative.r"
    R_SEMANTIC_ASYNC_COPY_ARGS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/async_copy_args.r"
    R_SEMANTIC_SHARED_OWNER_CLONE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_shared_owner_clone.r"
    R_SEMANTIC_STANDARD_OUTCOMES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_standard_outcomes.r"
    R_SEMANTIC_STANDARD_OUTCOMES_WRONG_VARIANT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_standard_outcomes_wrong_variant.r"
    R_SEMANTIC_STANDARD_OUTCOMES_MISSING_MOVE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_standard_outcomes_missing_move.r"
)

if(MSVC)
    target_compile_options(r_frontend_semantic_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_frontend_semantic_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

add_test(NAME r_frontend_semantic_unit COMMAND r_frontend_semantic_tests --unit)
add_test(NAME r_frontend_semantic_golden COMMAND r_frontend_semantic_tests --golden)
add_test(NAME r_frontend_semantic_body_golden COMMAND r_frontend_semantic_tests --body-golden)

add_executable(r_frontend_io_read_result_tests io_read_result_tests.c)
target_link_libraries(r_frontend_io_read_result_tests PRIVATE r_frontend)
target_include_directories(r_frontend_io_read_result_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/compiler/source"
)
target_compile_definitions(r_frontend_io_read_result_tests PRIVATE
    R_IO_READ_RESULT_SOURCE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_io_read_result.r"
    R_IO_READ_RESULT_MISSING_VARIANT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_read_result_missing_variant.r"
    R_IO_READ_RESULT_DUPLICATE_VARIANT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_read_result_duplicate_variant.r"
    R_IO_READ_RESULT_WRONG_VARIANT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_read_result_wrong_variant.r"
)

if(MSVC)
    target_compile_options(r_frontend_io_read_result_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_frontend_io_read_result_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_frontend_io_read_result_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_frontend_io_read_result_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_frontend_io_read_result_unit COMMAND r_frontend_io_read_result_tests)

add_executable(r_frontend_io_write_result_tests io_write_result_tests.c)
target_link_libraries(r_frontend_io_write_result_tests PRIVATE r_frontend)
target_include_directories(r_frontend_io_write_result_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/compiler/source"
)
target_compile_definitions(r_frontend_io_write_result_tests PRIVATE
    R_IO_WRITE_RESULT_SOURCE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_io_write_result.r"
    R_IO_WRITE_RESULT_MISSING_VARIANT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_write_result_missing_variant.r"
    R_IO_WRITE_RESULT_DUPLICATE_VARIANT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_write_result_duplicate_variant.r"
    R_IO_WRITE_RESULT_WRONG_VARIANT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_write_result_wrong_variant.r"
)

if(MSVC)
    target_compile_options(r_frontend_io_write_result_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_frontend_io_write_result_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_frontend_io_write_result_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_frontend_io_write_result_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_frontend_io_write_result_unit COMMAND r_frontend_io_write_result_tests)

add_executable(r_frontend_io_write_shared_tests io_write_shared_tests.c)
target_link_libraries(r_frontend_io_write_shared_tests PRIVATE r_frontend)
target_include_directories(r_frontend_io_write_shared_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/compiler/source"
)
target_compile_definitions(r_frontend_io_write_shared_tests PRIVATE
    R_IO_WRITE_SHARED_SOURCE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_io_write_shared.r"
    R_IO_WRITE_SHARED_ARITY_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_write_shared_arity.r"
    R_IO_WRITE_SHARED_STREAM_TYPE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_write_shared_stream_type.r"
    R_IO_WRITE_SHARED_BUFFER_TYPE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_write_shared_buffer_type.r"
    R_IO_WRITE_SHARED_OFFSET_TYPE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_write_shared_offset_type.r"
    R_IO_WRITE_SHARED_LENGTH_TYPE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_write_shared_length_type.r"
    R_IO_WRITE_SHARED_DEADLINE_TYPE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_write_shared_deadline_type.r"
    R_IO_WRITE_SHARED_CONTEXT_TYPE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_write_shared_context_type.r"
    R_IO_WRITE_SHARED_MISSING_MOVE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_write_shared_missing_move.r"
    R_IO_WRITE_SHARED_NESTED_MOVE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_write_shared_nested_move.r"
    R_IO_WRITE_SHARED_SUCCESS_REUSE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_write_shared_success_reuse.r"
    R_IO_WRITE_SHARED_MISSING_VARIANT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_write_shared_missing_variant.r"
    R_IO_WRITE_SHARED_DUPLICATE_VARIANT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_write_shared_duplicate_variant.r"
    R_IO_WRITE_SHARED_WRONG_VARIANT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_write_shared_wrong_variant.r"
)

if(MSVC)
    target_compile_options(r_frontend_io_write_shared_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_frontend_io_write_shared_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_frontend_io_write_shared_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_frontend_io_write_shared_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_frontend_io_write_shared_unit COMMAND r_frontend_io_write_shared_tests)

add_executable(r_frontend_io_as_error_tests io_as_error_tests.c)
target_link_libraries(r_frontend_io_as_error_tests PRIVATE r_frontend)
target_include_directories(r_frontend_io_as_error_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/compiler/source"
)
target_compile_definitions(r_frontend_io_as_error_tests PRIVATE
    R_IO_AS_ERROR_SOURCE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_io_as_error.r"
    R_IO_AS_ERROR_ARITY_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_as_error_arity.r"
    R_IO_AS_ERROR_ARGUMENT_TYPE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_as_error_argument_type.r"
    R_IO_AS_ERROR_RESULT_TYPE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_as_error_result_type.r"
)

if(MSVC)
    target_compile_options(r_frontend_io_as_error_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_frontend_io_as_error_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_frontend_io_as_error_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_frontend_io_as_error_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_frontend_io_as_error_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_frontend_io_as_error_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_frontend_io_as_error_unit COMMAND r_frontend_io_as_error_tests)

add_executable(r_frontend_fs_as_error_tests fs_as_error_tests.c)
target_link_libraries(r_frontend_fs_as_error_tests PRIVATE r_frontend)
target_include_directories(r_frontend_fs_as_error_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/compiler/source"
)
target_compile_definitions(r_frontend_fs_as_error_tests PRIVATE
    R_FS_AS_ERROR_SOURCE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_fs_as_error.r"
    R_FS_AS_ERROR_ARITY_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_fs_as_error_arity.r"
    R_FS_AS_ERROR_ARGUMENT_TYPE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_fs_as_error_argument_type.r"
    R_FS_AS_ERROR_RESULT_TYPE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_fs_as_error_result_type.r"
)

if(MSVC)
    target_compile_options(r_frontend_fs_as_error_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_frontend_fs_as_error_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_frontend_fs_as_error_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_frontend_fs_as_error_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_frontend_fs_as_error_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_frontend_fs_as_error_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_frontend_fs_as_error_unit COMMAND r_frontend_fs_as_error_tests)

add_executable(r_frontend_fs_path_is_absolute_tests fs_path_is_absolute_tests.c)
target_link_libraries(r_frontend_fs_path_is_absolute_tests PRIVATE r_frontend)
target_include_directories(r_frontend_fs_path_is_absolute_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/compiler/source"
)
target_compile_definitions(r_frontend_fs_path_is_absolute_tests PRIVATE
    R_FS_PATH_IS_ABSOLUTE_SOURCE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_fs_path_is_absolute.r"
    R_FS_PATH_IS_ABSOLUTE_ARITY_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_fs_path_is_absolute_arity.r"
    R_FS_PATH_IS_ABSOLUTE_ARGUMENT_TYPE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_fs_path_is_absolute_argument_type.r"
    R_FS_PATH_IS_ABSOLUTE_RESULT_TYPE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_fs_path_is_absolute_result_type.r"
)

if(MSVC)
    target_compile_options(r_frontend_fs_path_is_absolute_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_frontend_fs_path_is_absolute_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_frontend_fs_path_is_absolute_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_frontend_fs_path_is_absolute_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_frontend_fs_path_is_absolute_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_frontend_fs_path_is_absolute_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_frontend_fs_path_is_absolute_unit COMMAND r_frontend_fs_path_is_absolute_tests)

add_executable(r_frontend_io_close_tests io_close_tests.c)
target_link_libraries(r_frontend_io_close_tests PRIVATE r_frontend)
target_include_directories(r_frontend_io_close_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/compiler/source"
)
target_compile_definitions(r_frontend_io_close_tests PRIVATE
    R_IO_CLOSE_INPUT_SOURCE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_io_close_input.r"
    R_IO_CLOSE_OUTPUT_SOURCE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_io_close_output.r"
    R_IO_CLOSE_INPUT_ARITY_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_close_input_arity.r"
    R_IO_CLOSE_OUTPUT_HANDLE_TYPE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_close_output_handle_type.r"
    R_IO_CLOSE_INPUT_CONTEXT_TYPE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_close_input_context_type.r"
    R_IO_CLOSE_OUTPUT_DEADLINE_TYPE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_close_output_deadline_type.r"
    R_IO_CLOSE_INPUT_MISSING_MOVE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_close_input_missing_move.r"
    R_IO_CLOSE_OUTPUT_NESTED_MOVE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_close_output_nested_move.r"
    R_IO_CLOSE_INPUT_SUCCESS_REUSE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_io_close_input_success_reuse.r"
)

if(MSVC)
    target_compile_options(r_frontend_io_close_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_frontend_io_close_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_frontend_io_close_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_frontend_io_close_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_frontend_io_close_unit COMMAND r_frontend_io_close_tests)

add_executable(r_frontend_value_type_tests value_type_tests.c)
target_link_libraries(r_frontend_value_type_tests PRIVATE r_frontend)
target_include_directories(r_frontend_value_type_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/compiler/source"
)
target_compile_definitions(r_frontend_value_type_tests PRIVATE
    R_VALUE_TYPES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_value_types.r"
    R_VALUE_TYPES_HIR_GOLDEN="${CMAKE_CURRENT_SOURCE_DIR}/golden/value_types.hir.sexp"
    R_VALUE_TYPES_MIR_GOLDEN="${CMAKE_CURRENT_SOURCE_DIR}/golden/value_types.mir.sexp"
)

if(MSVC)
    target_compile_options(r_frontend_value_type_tests PRIVATE
        /W4 /WX /permissive-
    )
else()
    target_compile_options(r_frontend_value_type_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

add_test(NAME r_frontend_value_types_unit COMMAND r_frontend_value_type_tests)

add_executable(r_frontend_compiler_interface_tests compiler_interface_tests.c)
target_link_libraries(r_frontend_compiler_interface_tests PRIVATE r_frontend)

if(MSVC)
    target_compile_options(r_frontend_compiler_interface_tests PRIVATE
        /W4 /WX /permissive-
    )
else()
    target_compile_options(r_frontend_compiler_interface_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

add_test(NAME r_frontend_compiler_interface
         COMMAND r_frontend_compiler_interface_tests)

add_test(
    NAME r_frontend_raw_fn_interface
    COMMAND "${CMAKE_COMMAND}"
        -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
        -DSOURCE_MAIN=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audit_std_c_raw_fn_modules.r
        -DSOURCE_LIBRARY=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audit_std_c_callback_module.r
        -P ${CMAKE_CURRENT_SOURCE_DIR}/check_raw_fn_interface.cmake
)

add_executable(r_frontend_bytes_type_tests bytes_type_tests.c)
target_link_libraries(r_frontend_bytes_type_tests PRIVATE r_frontend)
target_include_directories(r_frontend_bytes_type_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/compiler/source"
)

if(MSVC)
    target_compile_options(r_frontend_bytes_type_tests PRIVATE
        /W4 /WX /permissive-
    )
else()
    target_compile_options(r_frontend_bytes_type_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

add_test(NAME r_frontend_bytes_type_unit COMMAND r_frontend_bytes_type_tests)

add_executable(r_frontend_hash_type_tests hash_type_tests.c)
target_link_libraries(r_frontend_hash_type_tests PRIVATE r_frontend)
target_include_directories(r_frontend_hash_type_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/compiler/source"
)

if(MSVC)
    target_compile_options(r_frontend_hash_type_tests PRIVATE
        /W4 /WX /permissive-
    )
else()
    target_compile_options(r_frontend_hash_type_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

add_test(NAME r_frontend_hash_type_unit COMMAND r_frontend_hash_type_tests)

add_executable(r_frontend_aggregate_tests aggregate_tests.c)
target_link_libraries(r_frontend_aggregate_tests PRIVATE r_frontend)
target_include_directories(r_frontend_aggregate_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/compiler/source"
)

if(MSVC)
    target_compile_options(r_frontend_aggregate_tests PRIVATE
        /W4 /WX /permissive-
    )
else()
    target_compile_options(r_frontend_aggregate_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_frontend_aggregate_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_frontend_aggregate_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_frontend_aggregate_unit
         COMMAND r_frontend_aggregate_tests)

add_test(
    NAME r_frontend_lazy_module_map
    COMMAND $<TARGET_FILE:r-front>
        --emit=ast
        --entry compiler.lazy.main::main
        --module-map
        ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/compiler_lazy.modules.map
)
set_tests_properties(r_frontend_lazy_module_map PROPERTIES
    PASS_REGULAR_EXPRESSION "compiler_lazy_dependency.r"
    FAIL_REGULAR_EXPRESSION "file_that_must_not_be_loaded"
)

add_test(
    NAME r_frontend_compiler_cli_options
    COMMAND "${CMAKE_COMMAND}"
        -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
        -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main.r
        -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
        -DUNSUPPORTED_TARGET_MANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/compiler_target_manifest.json
        -DLINK_MANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/compiler_link_manifest.json
        -P ${CMAKE_CURRENT_SOURCE_DIR}/check_compiler_cli_options.cmake
)

add_executable(r_runtime_tests
    runtime_tests.c
    "${PROJECT_SOURCE_DIR}/runtime/source/allocator.c"
    "${PROJECT_SOURCE_DIR}/runtime/source/hosted_entry.c"
    "${PROJECT_SOURCE_DIR}/runtime/source/panic_abort.c"
    "${PROJECT_SOURCE_DIR}/runtime/source/thread_attachment.c"
    "${PROJECT_SOURCE_DIR}/runtime/source/utf8.c"
)
target_include_directories(r_runtime_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/runtime/include"
)
target_compile_definitions(r_runtime_tests PRIVATE
    R_RUNTIME_TESTING=1
)
target_link_libraries(r_runtime_tests PRIVATE Threads::Threads)
if(APPLE)
    target_sources(r_runtime_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/runtime/darwin/source/stack.c"
    )
    target_include_directories(r_runtime_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/runtime/darwin/include"
    )
    target_link_libraries(r_runtime_tests PRIVATE
        r_runtime_darwin_fs_lane
        r_runtime_darwin_process
        r_runtime_darwin_task
    )
endif()

if(MSVC)
    target_compile_options(r_runtime_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_runtime_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_runtime_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_runtime_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_runtime_valid_lifecycle COMMAND r_runtime_tests valid)
add_test(NAME r_runtime_invalid_utf8 COMMAND r_runtime_tests invalid)
add_test(NAME r_runtime_snapshot_oom COMMAND r_runtime_tests oom)
add_test(NAME r_runtime_executor_oom COMMAND r_runtime_tests executor-oom)
add_test(NAME r_runtime_async_root_start_failure
         COMMAND r_runtime_tests root-start-failure)
add_test(NAME r_runtime_invalid_precedes_oom
         COMMAND r_runtime_tests invalid-and-oom)
if(APPLE)
    add_test(NAME r_runtime_console_drain COMMAND r_runtime_tests console-drain)
endif()
set_tests_properties(r_runtime_valid_lifecycle PROPERTIES
    PASS_REGULAR_EXPRESSION "runtime_valid_lifecycle"
    FAIL_REGULAR_EXPRESSION "R startup failure"
)
set_tests_properties(r_runtime_invalid_utf8 PROPERTIES
    PASS_REGULAR_EXPRESSION "argument_encoding_failure"
    FAIL_REGULAR_EXPRESSION "allocation_failure"
)
set_tests_properties(r_runtime_snapshot_oom PROPERTIES
    PASS_REGULAR_EXPRESSION "allocation_failure"
    FAIL_REGULAR_EXPRESSION "argument_encoding_failure"
)
set_tests_properties(r_runtime_executor_oom PROPERTIES
    PASS_REGULAR_EXPRESSION "async_root_start_failure"
    FAIL_REGULAR_EXPRESSION "argument_encoding_failure"
)
set_tests_properties(r_runtime_async_root_start_failure PROPERTIES
    PASS_REGULAR_EXPRESSION
        "async_root_start_failure.*runtime_async_root_start_failure"
    FAIL_REGULAR_EXPRESSION "argument_encoding_failure|allocation_failure"
)
set_tests_properties(r_runtime_invalid_precedes_oom PROPERTIES
    PASS_REGULAR_EXPRESSION "argument_encoding_failure"
    FAIL_REGULAR_EXPRESSION "allocation_failure"
)

add_executable(r_runtime_container_tests runtime_container_tests.c)
target_link_libraries(r_runtime_container_tests PRIVATE r_runtime_0_1)

if(MSVC)
    target_compile_options(r_runtime_container_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_runtime_container_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

add_test(NAME r_runtime_containers COMMAND r_runtime_container_tests)

add_executable(r_runtime_drop_tests runtime_drop_tests.c)
target_link_libraries(r_runtime_drop_tests PRIVATE r_runtime_0_1)

if(MSVC)
    target_compile_options(r_runtime_drop_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_runtime_drop_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

# R-FUNC-0004: self-nesting own/rc/arc graphs are destroyed with a data-independent stack depth.
add_test(NAME r_runtime_iterative_drop COMMAND r_runtime_drop_tests)
set_tests_properties(r_runtime_iterative_drop PROPERTIES LABELS "runtime;stack")

add_executable(r_runtime_utf8_tests runtime_utf8_tests.c)
target_link_libraries(r_runtime_utf8_tests PRIVATE r_runtime_0_1)

if(MSVC)
    target_compile_options(r_runtime_utf8_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_runtime_utf8_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

add_test(NAME r_runtime_utf8 COMMAND r_runtime_utf8_tests)

add_executable(r_runtime_string_tests runtime_string_tests.c)
target_link_libraries(r_runtime_string_tests PRIVATE r_runtime_0_1)

if(MSVC)
    target_compile_options(r_runtime_string_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_runtime_string_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

add_test(NAME r_runtime_string COMMAND r_runtime_string_tests)

add_executable(r_runtime_panic_tests runtime_panic_tests.c)
target_link_libraries(r_runtime_panic_tests PRIVATE r_runtime_0_1)

if(MSVC)
    target_compile_options(r_runtime_panic_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_runtime_panic_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

add_test(NAME r_runtime_panic_categories COMMAND r_runtime_panic_tests)

add_executable(r_runtime_freestanding_tests runtime_freestanding_tests.c)
target_link_libraries(r_runtime_freestanding_tests PRIVATE r_runtime_freestanding Threads::Threads)

if(MSVC)
    target_compile_options(r_runtime_freestanding_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_runtime_freestanding_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

add_test(NAME r_runtime_freestanding COMMAND r_runtime_freestanding_tests)
set_tests_properties(r_runtime_freestanding PROPERTIES LABELS "runtime;freestanding")

find_package(Threads REQUIRED)
add_executable(r_runtime_arc_thread_tests runtime_arc_thread_tests.c)
target_link_libraries(r_runtime_arc_thread_tests PRIVATE r_runtime_0_1 Threads::Threads)

if(MSVC)
    target_compile_options(r_runtime_arc_thread_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_runtime_arc_thread_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

add_test(NAME r_runtime_arc_threads COMMAND r_runtime_arc_thread_tests)

add_executable(r_runtime_c_boundary_tests runtime_c_boundary_tests.c)
target_link_libraries(r_runtime_c_boundary_tests PRIVATE r_runtime_0_1 Threads::Threads)

if(MSVC)
    target_compile_options(r_runtime_c_boundary_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_runtime_c_boundary_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_runtime_c_boundary_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_runtime_c_boundary_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_runtime_c_boundary COMMAND r_runtime_c_boundary_tests)
set_tests_properties(r_runtime_c_boundary PROPERTIES TIMEOUT 30)

add_executable(r_library_async_tests library_async_tests.c)
target_link_libraries(r_library_async_tests PRIVATE r_std_async)

if(MSVC)
    target_compile_options(r_library_async_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_async_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_async_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_async_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_async_unit COMMAND r_library_async_tests)

add_executable(r_library_async_sync_tests library_async_sync_tests.c)
target_link_libraries(r_library_async_sync_tests PRIVATE r_std_sync)

if(MSVC)
    target_compile_options(r_library_async_sync_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_async_sync_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_async_sync_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_async_sync_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_async_sync_unit COMMAND r_library_async_sync_tests)
set_tests_properties(r_library_async_sync_unit PROPERTIES TIMEOUT 60)

add_executable(r_library_async_blocking_tests library_async_blocking_tests.c)
target_link_libraries(r_library_async_blocking_tests PRIVATE r_std_async)

if(MSVC)
    target_compile_options(r_library_async_blocking_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_async_blocking_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_async_blocking_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_async_blocking_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_async_blocking_unit COMMAND r_library_async_blocking_tests)
set_tests_properties(r_library_async_blocking_unit PROPERTIES TIMEOUT 60)

add_executable(r_library_thread_tests library_thread_tests.c)
target_compile_definitions(r_library_thread_tests PRIVATE R_STD_THREAD_TESTING=1)
target_include_directories(r_library_thread_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/library/internal/thread/include"
)
target_link_libraries(r_library_thread_tests PRIVATE r_std_thread Threads::Threads)

if(MSVC)
    target_compile_options(r_library_thread_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_thread_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_thread_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_thread_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_thread_unit COMMAND r_library_thread_tests)
set_tests_properties(r_library_thread_unit PROPERTIES TIMEOUT 20)

add_executable(r_library_sync_tests library_sync_tests.c)
target_include_directories(r_library_sync_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/library/internal/synchronization/include"
)
target_link_libraries(r_library_sync_tests PRIVATE r_std_sync Threads::Threads)

if(MSVC)
    target_compile_options(r_library_sync_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_sync_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_sync_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_sync_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_sync_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_sync_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_sync_unit COMMAND r_library_sync_tests)
set_tests_properties(r_library_sync_unit PROPERTIES TIMEOUT 20)

add_executable(r_library_fs_tests library_fs_tests.c)
target_link_libraries(r_library_fs_tests PRIVATE r_std_fs Threads::Threads)
if(APPLE)
    target_compile_definitions(
        r_library_fs_tests
        PRIVATE
            R_RUNTIME_DARWIN_FS_LANE_TESTING=1
            R_RUNTIME_DARWIN_IO_TESTING=1
    )
endif()
target_include_directories(r_library_fs_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/library/internal/filesystem/include"
)

if(MSVC)
    target_compile_options(r_library_fs_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_fs_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_fs_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_fs_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_fs_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_fs_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_fs_unit COMMAND r_library_fs_tests)
set_tests_properties(r_library_fs_unit PROPERTIES TIMEOUT 20)

add_executable(r_library_fs_deadline_tests library_fs_deadline_tests.c)
target_link_libraries(r_library_fs_deadline_tests PRIVATE r_std_fs Threads::Threads)
target_include_directories(r_library_fs_deadline_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/library/internal/filesystem/include"
)

add_executable(r_library_fs_payload_tests library_fs_payload_tests.c)
target_link_libraries(r_library_fs_payload_tests PRIVATE r_std_fs Threads::Threads)
target_include_directories(r_library_fs_payload_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/library/internal/filesystem/include"
)
if(APPLE)
    target_compile_definitions(
        r_library_fs_payload_tests
        PRIVATE
            R_RUNTIME_DARWIN_FS_LANE_TESTING=1
            R_RUNTIME_DARWIN_IO_TESTING=1
    )
endif()

add_executable(r_library_fs_read_file_tests library_fs_read_file_tests.c)
target_link_libraries(r_library_fs_read_file_tests PRIVATE r_std_fs Threads::Threads)
target_include_directories(r_library_fs_read_file_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/library/internal/filesystem/include"
)
if(APPLE)
    target_compile_definitions(
        r_library_fs_read_file_tests
        PRIVATE
            R_RUNTIME_DARWIN_FS_LANE_TESTING=1
            R_RUNTIME_DARWIN_IO_TESTING=1
    )
endif()

add_executable(r_library_fs_atomic_write_tests library_fs_atomic_write_tests.c)
target_link_libraries(r_library_fs_atomic_write_tests PRIVATE r_std_fs Threads::Threads)
target_include_directories(r_library_fs_atomic_write_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/library/internal/filesystem/include"
)
if(APPLE)
    target_compile_definitions(
        r_library_fs_atomic_write_tests
        PRIVATE
            R_RUNTIME_DARWIN_FS_LANE_TESTING=1
            R_RUNTIME_DARWIN_IO_TESTING=1
    )
endif()

foreach(R_LIBRARY_FS_FOCUSED_TEST IN ITEMS
        r_library_fs_deadline_tests
        r_library_fs_payload_tests
        r_library_fs_read_file_tests
        r_library_fs_atomic_write_tests)
    if(MSVC)
        target_compile_options(${R_LIBRARY_FS_FOCUSED_TEST} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${R_LIBRARY_FS_FOCUSED_TEST} PRIVATE
            -Wall -Wextra -Wpedantic -Werror
            -Wconversion -Wsign-conversion -Wshadow
            -Wstrict-prototypes -Wmissing-prototypes
        )
    endif()

    if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
        target_compile_options(${R_LIBRARY_FS_FOCUSED_TEST} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(${R_LIBRARY_FS_FOCUSED_TEST} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
    endif()

    if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
        target_compile_options(${R_LIBRARY_FS_FOCUSED_TEST} PRIVATE
            -fsanitize=thread
            -fno-omit-frame-pointer
        )
        target_link_options(${R_LIBRARY_FS_FOCUSED_TEST} PRIVATE
            -fsanitize=thread
            -fno-omit-frame-pointer
        )
    endif()
endforeach()

add_test(NAME r_library_fs_deadline_unit COMMAND r_library_fs_deadline_tests)
set_tests_properties(r_library_fs_deadline_unit PROPERTIES TIMEOUT 30)
add_test(NAME r_library_fs_payload_unit COMMAND r_library_fs_payload_tests)
set_tests_properties(r_library_fs_payload_unit PROPERTIES TIMEOUT 30)

add_executable(r_library_fs_scoped_tests library_fs_scoped_tests.c)
target_link_libraries(r_library_fs_scoped_tests PRIVATE r_std_fs Threads::Threads)
target_include_directories(r_library_fs_scoped_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/library/internal/filesystem/include"
)
if(APPLE)
    target_compile_definitions(
        r_library_fs_scoped_tests
        PRIVATE
            R_RUNTIME_DARWIN_FS_LANE_TESTING=1
            R_RUNTIME_DARWIN_IO_TESTING=1
    )
endif()

if(MSVC)
    target_compile_options(r_library_fs_scoped_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_fs_scoped_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_fs_scoped_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_fs_scoped_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_fs_scoped_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_fs_scoped_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_fs_scoped_unit COMMAND r_library_fs_scoped_tests)
set_tests_properties(r_library_fs_scoped_unit PROPERTIES TIMEOUT 30)
add_test(NAME r_library_fs_read_file_unit COMMAND r_library_fs_read_file_tests)
set_tests_properties(r_library_fs_read_file_unit PROPERTIES TIMEOUT 30)
add_test(NAME r_library_fs_atomic_write_unit COMMAND r_library_fs_atomic_write_tests)
set_tests_properties(r_library_fs_atomic_write_unit PROPERTIES TIMEOUT 30)

add_executable(r_library_net_tests library_net_tests.c)
target_link_libraries(r_library_net_tests PRIVATE r_std_net)

if(MSVC)
    target_compile_options(r_library_net_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_net_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_net_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_net_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_net_unit COMMAND r_library_net_tests)

add_executable(r_library_net_socket_tests library_net_socket_tests.c)
target_link_libraries(r_library_net_socket_tests PRIVATE r_std_net Threads::Threads)
target_include_directories(r_library_net_socket_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/library/internal/networking/include"
)

if(MSVC)
    target_compile_options(r_library_net_socket_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_net_socket_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_net_socket_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_net_socket_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_net_socket_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_net_socket_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_net_socket_unit COMMAND r_library_net_socket_tests)
set_tests_properties(r_library_net_socket_unit PROPERTIES TIMEOUT 30)

add_executable(r_library_net_bind_tests library_net_bind_tests.c)
target_link_libraries(r_library_net_bind_tests PRIVATE r_std_net)
target_include_directories(r_library_net_bind_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/library/internal/networking/include"
)

if(MSVC)
    target_compile_options(r_library_net_bind_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_net_bind_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_net_bind_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_net_bind_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_net_bind_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_net_bind_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_net_bind_unit COMMAND r_library_net_bind_tests)
set_tests_properties(r_library_net_bind_unit PROPERTIES TIMEOUT 30)

add_executable(r_library_net_connect_tests library_net_connect_tests.c)
target_link_libraries(r_library_net_connect_tests PRIVATE r_std_net)

if(MSVC)
    target_compile_options(r_library_net_connect_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_net_connect_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_net_connect_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_net_connect_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_net_connect_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_net_connect_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_net_connect_unit COMMAND r_library_net_connect_tests)
set_tests_properties(r_library_net_connect_unit PROPERTIES TIMEOUT 30)

add_executable(r_library_net_accept_tests library_net_accept_tests.c)
target_link_libraries(r_library_net_accept_tests PRIVATE r_std_net)
target_include_directories(r_library_net_accept_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/library/internal/networking/include"
)

if(MSVC)
    target_compile_options(r_library_net_accept_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_net_accept_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_net_accept_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_net_accept_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_net_accept_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_net_accept_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_net_accept_unit COMMAND r_library_net_accept_tests)
set_tests_properties(r_library_net_accept_unit PROPERTIES TIMEOUT 30)

add_executable(r_library_net_listener_close_tests library_net_listener_close_tests.c)
target_link_libraries(r_library_net_listener_close_tests PRIVATE r_std_net)
target_include_directories(r_library_net_listener_close_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/library/internal/networking/include"
)
if(APPLE)
    target_compile_definitions(r_library_net_listener_close_tests PRIVATE R_LIBRARY_NET_TESTING=1)
endif()

if(MSVC)
    target_compile_options(r_library_net_listener_close_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_net_listener_close_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_net_listener_close_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_net_listener_close_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_net_listener_close_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_net_listener_close_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_net_listener_close_unit COMMAND r_library_net_listener_close_tests)
set_tests_properties(r_library_net_listener_close_unit PROPERTIES TIMEOUT 30)

add_executable(r_library_net_read_tests library_net_read_tests.c)
target_link_libraries(r_library_net_read_tests PRIVATE r_std_net)
target_include_directories(r_library_net_read_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/library/internal/networking/include"
)
if(APPLE)
    target_compile_definitions(
        r_library_net_read_tests
        PRIVATE
            R_LIBRARY_NET_TESTING=1
            R_RUNTIME_DARWIN_IO_TESTING=1
    )
endif()

if(MSVC)
    target_compile_options(r_library_net_read_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_net_read_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_net_read_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_net_read_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_net_read_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_net_read_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_net_read_unit COMMAND r_library_net_read_tests)
set_tests_properties(r_library_net_read_unit PROPERTIES TIMEOUT 30)

add_executable(r_library_net_scoped_tests library_net_scoped_tests.c)
target_link_libraries(r_library_net_scoped_tests PRIVATE r_std_net Threads::Threads)
target_include_directories(r_library_net_scoped_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/library/internal/networking/include"
)
if(APPLE)
    target_compile_definitions(
        r_library_net_scoped_tests
        PRIVATE
            R_LIBRARY_NET_TESTING=1
            R_RUNTIME_DARWIN_IO_TESTING=1
    )
endif()

if(MSVC)
    target_compile_options(r_library_net_scoped_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_net_scoped_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_net_scoped_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_net_scoped_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_net_scoped_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_net_scoped_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_net_scoped_unit COMMAND r_library_net_scoped_tests)
set_tests_properties(r_library_net_scoped_unit PROPERTIES TIMEOUT 30)

add_executable(r_library_net_write_tests library_net_write_tests.c)
target_link_libraries(r_library_net_write_tests PRIVATE r_std_net)
target_include_directories(r_library_net_write_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/library/internal/networking/include"
)
if(APPLE)
    target_compile_definitions(
        r_library_net_write_tests
        PRIVATE
            R_LIBRARY_NET_TESTING=1
            R_RUNTIME_DARWIN_IO_TESTING=1
    )
endif()

if(MSVC)
    target_compile_options(r_library_net_write_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_net_write_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_net_write_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_net_write_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_net_write_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_net_write_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_net_write_unit COMMAND r_library_net_write_tests)
set_tests_properties(r_library_net_write_unit PROPERTIES TIMEOUT 30)

add_executable(r_library_net_terminal_tests library_net_terminal_tests.c)
target_link_libraries(r_library_net_terminal_tests PRIVATE r_std_net Threads::Threads)
target_include_directories(r_library_net_terminal_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/library/internal/networking/include"
)
if(APPLE)
    target_compile_definitions(
        r_library_net_terminal_tests
        PRIVATE
            R_LIBRARY_NET_TESTING=1
            R_RUNTIME_DARWIN_IO_TESTING=1
    )
endif()

if(MSVC)
    target_compile_options(r_library_net_terminal_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_net_terminal_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_net_terminal_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_net_terminal_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_net_terminal_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_net_terminal_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_net_terminal_unit COMMAND r_library_net_terminal_tests)
set_tests_properties(r_library_net_terminal_unit PROPERTIES TIMEOUT 30)

add_executable(r_library_net_udp_tests library_net_udp_tests.c)
target_link_libraries(r_library_net_udp_tests PRIVATE r_std_net Threads::Threads)
target_include_directories(r_library_net_udp_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/library/internal/networking/include"
)
if(APPLE)
    target_compile_definitions(
        r_library_net_udp_tests
        PRIVATE
            R_LIBRARY_NET_TESTING=1
            R_RUNTIME_DARWIN_SOCKET_TESTING=1
    )
endif()

if(MSVC)
    target_compile_options(r_library_net_udp_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_net_udp_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_net_udp_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_net_udp_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_net_udp_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_net_udp_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_net_udp_unit COMMAND r_library_net_udp_tests)
set_tests_properties(r_library_net_udp_unit PROPERTIES TIMEOUT 30)

add_executable(r_library_net_resolve_tests library_net_resolve_tests.c)
target_link_libraries(r_library_net_resolve_tests PRIVATE r_std_net Threads::Threads)
target_include_directories(r_library_net_resolve_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/library/internal/networking/include"
)

if(MSVC)
    target_compile_options(r_library_net_resolve_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_net_resolve_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_net_resolve_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_net_resolve_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_library_net_resolve_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_net_resolve_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_net_resolve_unit COMMAND r_library_net_resolve_tests)
set_tests_properties(r_library_net_resolve_unit PROPERTIES TIMEOUT 30)

add_executable(r_library_process_tests library_process_tests.c)
target_link_libraries(r_library_process_tests PRIVATE r_std_process)
target_include_directories(r_library_process_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/library/internal/process/include"
)

if(MSVC)
    target_compile_options(r_library_process_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_library_process_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_library_process_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(r_library_process_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

add_test(NAME r_library_process_unit COMMAND r_library_process_tests)

if(APPLE)
    add_executable(r_process_spawn_helper process_spawn_helper.c)
    add_executable(r_library_process_spawn_tests library_process_spawn_tests.c)
    add_executable(r_library_process_take_tests library_process_take_tests.c)
    add_executable(r_library_process_lifecycle_tests library_process_lifecycle_tests.c)
    add_dependencies(r_library_process_spawn_tests r_process_spawn_helper)
    add_dependencies(r_library_process_take_tests r_process_spawn_helper)
    add_dependencies(r_library_process_lifecycle_tests r_process_spawn_helper)
    target_link_libraries(r_library_process_spawn_tests PRIVATE r_std_process)
    target_link_libraries(r_library_process_take_tests PRIVATE r_std_process)
    target_link_libraries(r_library_process_lifecycle_tests PRIVATE r_std_process)
    target_include_directories(r_library_process_spawn_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/library/internal/process/include"
    )
    target_include_directories(r_library_process_take_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/library/internal/process/include"
    )
    target_include_directories(r_library_process_lifecycle_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/library/internal/process/include"
    )
    target_compile_definitions(
        r_library_process_spawn_tests
        PRIVATE
            R_LIBRARY_PROCESS_TESTING=1
            R_RUNTIME_DARWIN_PROCESS_TESTING=1
            R_PROCESS_SPAWN_HELPER_PATH="$<TARGET_FILE:r_process_spawn_helper>"
    )
    target_compile_definitions(
        r_library_process_take_tests
        PRIVATE
            R_LIBRARY_PROCESS_TESTING=1
            R_RUNTIME_DARWIN_PROCESS_TESTING=1
            R_PROCESS_SPAWN_HELPER_PATH="$<TARGET_FILE:r_process_spawn_helper>"
    )
    target_compile_definitions(
        r_library_process_lifecycle_tests
        PRIVATE
            R_LIBRARY_PROCESS_TESTING=1
            R_RUNTIME_DARWIN_PROCESS_TESTING=1
            R_PROCESS_SPAWN_HELPER_PATH="$<TARGET_FILE:r_process_spawn_helper>"
    )

    foreach(R_PROCESS_TEST_TARGET IN ITEMS
            r_process_spawn_helper
            r_library_process_spawn_tests
            r_library_process_take_tests
            r_library_process_lifecycle_tests)
        target_compile_options(${R_PROCESS_TEST_TARGET} PRIVATE
            -Wall -Wextra -Wpedantic -Werror
            -Wconversion -Wsign-conversion -Wshadow
            -Wstrict-prototypes -Wmissing-prototypes
        )
        if(R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS)
            target_compile_options(${R_PROCESS_TEST_TARGET} PRIVATE
                -fsanitize=address,undefined
                -fno-omit-frame-pointer
            )
            target_link_options(${R_PROCESS_TEST_TARGET} PRIVATE
                -fsanitize=address,undefined
                -fno-omit-frame-pointer
            )
        endif()
        if(R_FRONTEND_ENABLE_THREAD_SANITIZER)
            target_compile_options(${R_PROCESS_TEST_TARGET} PRIVATE
                -fsanitize=thread
                -fno-omit-frame-pointer
            )
            target_link_options(${R_PROCESS_TEST_TARGET} PRIVATE
                -fsanitize=thread
                -fno-omit-frame-pointer
            )
        endif()
    endforeach()

    add_test(NAME r_library_process_spawn_unit COMMAND r_library_process_spawn_tests)
    set_tests_properties(r_library_process_spawn_unit PROPERTIES TIMEOUT 40)
    add_test(NAME r_library_process_take_unit COMMAND r_library_process_take_tests)
    set_tests_properties(r_library_process_take_unit PROPERTIES TIMEOUT 30)
    add_test(NAME r_library_process_lifecycle_unit COMMAND r_library_process_lifecycle_tests)
    set_tests_properties(r_library_process_lifecycle_unit PROPERTIES TIMEOUT 30)
endif()

add_executable(r_frontend_codegen_tests codegen_tests.c)
target_link_libraries(r_frontend_codegen_tests PRIVATE r_frontend)
target_include_directories(r_frontend_codegen_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/compiler/source"
)
target_compile_definitions(r_frontend_codegen_tests PRIVATE
    R_CODEGEN_ARRAY_SLICE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_array_slice.r"
    R_CODEGEN_SYNC_AGGREGATE_DESTINATION_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_sync_aggregate_destination.r"
    R_CODEGEN_CONSTEXPR_STRING_POOL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_constexpr_string_pool.r"
    R_CODEGEN_CONSTEXPR_STRING_POOL_SUPPORT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_constexpr_string_pool_support.r"
    R_CODEGEN_ASYNC_OPTION_PATH_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_option_path.r"
    R_CODEGEN_MATH_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_math.r"
    R_CODEGEN_MAIN_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main.r"
    R_CODEGEN_RECURSIVE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_recursive.r"
    R_CODEGEN_GOLDEN_SOURCE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_golden.r"
    R_CODEGEN_GOLDEN_PATH="${CMAKE_CURRENT_SOURCE_DIR}/golden/codegen.c"
    R_CODEGEN_VALUE_TYPES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_value_types.r"
    R_CODEGEN_ASYNC_SCALAR_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_scalar.r"
    R_CODEGEN_ASYNC_ENUM_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_enum.r"
    R_CODEGEN_ASYNC_SHORT_CIRCUIT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_short_circuit.r"
    R_CODEGEN_ASYNC_FUSED_DEFAULT_OWN_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_fused_default_own.r"
    R_CODEGEN_ASYNC_SYNC_CALL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_sync_call.r"
    R_CODEGEN_ASYNC_IO_READ_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_io_read.r"
    R_CODEGEN_ASYNC_IO_WRITE_ALL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_io_write_all.r"
    R_CODEGEN_STDOUT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_stdout.r"
    R_CODEGEN_ASYNC_STDOUT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_stdout.r"
    R_CODEGEN_STDIN_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_stdin.r"
    R_CODEGEN_ASYNC_STDIN_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_stdin.r"
    R_CODEGEN_SYNC_MAIN_ARGS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_sync_main_args.r"
    R_CODEGEN_ASYNC_MAIN_ARGS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_main_args.r"
    R_CODEGEN_ASYNC_COPY_ARGS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/async_copy_args.r"
    R_CODEGEN_ASYNC_COPY_AGGREGATE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_copy_aggregate.r"
    R_CODEGEN_ASYNC_MOVE_ARGS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/async_move_args.r"
    R_CODEGEN_CORE_ASSUME_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_core_assume.r"
    R_CODEGEN_ASYNC_CORE_ASSUME_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_core_assume.r"
    R_CODEGEN_CORE_SLICE_FROM_RAW_PARTS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_core_slice_from_raw_parts.r"
    R_CODEGEN_ASYNC_CORE_SLICE_FROM_RAW_PARTS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_core_slice_from_raw_parts.r"
    R_CODEGEN_CORE_SLICE_FROM_RAW_PARTS_IN_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_core_slice_from_raw_parts_in.r"
    R_CODEGEN_ASYNC_CORE_SLICE_FROM_RAW_PARTS_IN_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_core_slice_from_raw_parts_in.r"
    R_CODEGEN_CORE_VOLATILE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_core_volatile.r"
    R_CODEGEN_ASYNC_CORE_VOLATILE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_core_volatile.r"
    R_CODEGEN_CORE_ADOPT_RELEASE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_core_adopt_release.r"
    R_CODEGEN_ASYNC_CORE_ADOPT_RELEASE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_core_adopt_release.r"
    R_CODEGEN_CORE_ADOPT_NONTRIVIAL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_core_adopt_nontrivial.r"
    R_CODEGEN_ASYNC_CORE_ADOPT_NONTRIVIAL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_core_adopt_nontrivial.r"
    R_CODEGEN_CORE_ADOPT_TYPE_GLUE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_core_adopt_type_glue.r"
    R_CODEGEN_CORE_ADOPT_RUNTIME_VALUES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_core_adopt_runtime_values.r"
    R_CODEGEN_CORE_ADOPT_NAMED_STANDARD_COPY_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_core_adopt_named_standard_copy.r"
    R_CODEGEN_CORE_ADOPT_NAMED_STANDARD_LAYOUT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_core_adopt_named_standard_layout.r"
    R_CODEGEN_CORE_ADOPT_NAMED_STANDARD_MOVE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_core_adopt_named_standard_move.r"
    R_CODEGEN_CORE_ADOPT_COMPILER_SPECIALIZED_MOVE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_core_adopt_compiler_specialized_move.r"
    R_CODEGEN_NAMED_STANDARD_MOVE_ABI_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_named_move_abi.r"
    R_CODEGEN_TARGET_MANIFEST_PATH="${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
)

if(MSVC)
    target_compile_options(r_frontend_codegen_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(r_frontend_codegen_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()

add_test(NAME r_frontend_codegen_unit COMMAND r_frontend_codegen_tests)

foreach(R_STACK_USAGE_SELF_TEST_CASE IN ITEMS
        positive_deterministic_header
        missing_artifact
        empty_artifact
        malformed_record
        dynamic_frame
        duplicate_function
        ceiling_violation
        merge_ceiling_preserves_header
        fixed_point_merge
        final_under_bound
        final_over_bound
        final_name_mismatch
        wrapper_huge_frame)
    add_test(
        NAME r_frontend_codegen_stack_usage_${R_STACK_USAGE_SELF_TEST_CASE}
        COMMAND "${CMAKE_COMMAND}"
            -DSTACK_USAGE_CHECK_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/check_codegen_stack_usage.cmake
            -DSTACK_USAGE_SELF_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/stack_usage_self_tests
            -DSTACK_USAGE_SELF_TEST_CASE=${R_STACK_USAGE_SELF_TEST_CASE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_codegen_stack_usage_self_test.cmake
    )
endforeach()

set(CLANG_FORMAT "" CACHE FILEPATH
    "Optional path to clang-format 22.1.8 for generated C17 regression tests")
unset(R_CODEGEN_CLANG_FORMAT)
if(CLANG_FORMAT)
    set(R_CODEGEN_CLANG_FORMAT "${CLANG_FORMAT}")
else()
    find_program(R_CODEGEN_CLANG_FORMAT
        NAMES clang-format-22.1.8 clang-format-22 clang-format
        NO_CACHE
    )
endif()
if(R_CODEGEN_CLANG_FORMAT)
    execute_process(
        COMMAND "${R_CODEGEN_CLANG_FORMAT}" --version
        RESULT_VARIABLE R_CODEGEN_CLANG_FORMAT_RESULT
        OUTPUT_VARIABLE R_CODEGEN_CLANG_FORMAT_OUTPUT
        ERROR_VARIABLE R_CODEGEN_CLANG_FORMAT_ERROR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" R_CODEGEN_CLANG_FORMAT_VERSION
        "${R_CODEGEN_CLANG_FORMAT_OUTPUT}")
    if(NOT R_CODEGEN_CLANG_FORMAT_RESULT EQUAL 0 OR
       NOT R_CODEGEN_CLANG_FORMAT_VERSION STREQUAL "22.1.8")
        if(CLANG_FORMAT)
            message(FATAL_ERROR
                "CLANG_FORMAT must name clang-format 22.1.8: "
                "${R_CODEGEN_CLANG_FORMAT_OUTPUT}${R_CODEGEN_CLANG_FORMAT_ERROR}")
        endif()
        set(R_CODEGEN_CLANG_FORMAT "")
    endif()
endif()

if(NOT MSVC)
    if(NOT Python3_Interpreter_FOUND)
        message(FATAL_ERROR
            "Python 3 is required to generate the exhaustive checked-conversion fixture")
    endif()
    set(R_CODEGEN_CHECKED_MATRIX_SOURCE
        "${CMAKE_CURRENT_BINARY_DIR}/generated/codegen_checked_matrix.r")
    set(R_CODEGEN_CHECKED_MATRIX_GENERATOR
        "${PROJECT_SOURCE_DIR}/tools/generate_checked_codegen_fixture.py")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${R_CODEGEN_CHECKED_MATRIX_GENERATOR}")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}"
            "${R_CODEGEN_CHECKED_MATRIX_GENERATOR}"
            --output "${R_CODEGEN_CHECKED_MATRIX_SOURCE}"
        RESULT_VARIABLE R_CODEGEN_CHECKED_MATRIX_RESULT
        ERROR_VARIABLE R_CODEGEN_CHECKED_MATRIX_ERROR
    )
    if(NOT R_CODEGEN_CHECKED_MATRIX_RESULT EQUAL 0)
        message(FATAL_ERROR
            "failed to generate checked-conversion fixture: "
            "${R_CODEGEN_CHECKED_MATRIX_ERROR}")
    endif()
    set(R_CODEGEN_CORE_INTEGER_ALL_SOURCE
        "${CMAKE_CURRENT_BINARY_DIR}/generated/codegen_core_integer_all.r")
    set(R_CODEGEN_CORE_INTEGER_ALL_GENERATOR
        "${PROJECT_SOURCE_DIR}/tools/generate_core_integer_fixture.py")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${R_CODEGEN_CORE_INTEGER_ALL_GENERATOR}")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}"
            "${R_CODEGEN_CORE_INTEGER_ALL_GENERATOR}"
            --output "${R_CODEGEN_CORE_INTEGER_ALL_SOURCE}"
        RESULT_VARIABLE R_CODEGEN_CORE_INTEGER_ALL_RESULT
        ERROR_VARIABLE R_CODEGEN_CORE_INTEGER_ALL_ERROR
    )
    if(NOT R_CODEGEN_CORE_INTEGER_ALL_RESULT EQUAL 0)
        message(FATAL_ERROR
            "failed to generate exhaustive core integer fixture: "
            "${R_CODEGEN_CORE_INTEGER_ALL_ERROR}")
    endif()

    if(R_CODEGEN_CLANG_FORMAT)
        add_test(
            NAME r_frontend_codegen_golden_format
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DCLANG_FORMAT=${R_CODEGEN_CLANG_FORMAT}
                -DSOURCE_1=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_golden.r
                -DFORMAT_STYLE=${PROJECT_SOURCE_DIR}/.clang-format
                -DOUTPUT_C=${CMAKE_CURRENT_BINARY_DIR}/codegen_golden_format.c
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_codegen_format.cmake
        )
    endif()
    include("${PROJECT_SOURCE_DIR}/examples/catalogue.cmake")
    # R-FUNC-0025 (M24): the tests of the standard modules written in R, one test program per
    # file of library/r/tests translated in test mode.
    file(GLOB R_LIBRARY_R_TEST_FILES CONFIGURE_DEPENDS
         "${PROJECT_SOURCE_DIR}/library/r/tests/*.r")
    list(SORT R_LIBRARY_R_TEST_FILES)
    set(R_LIBRARY_R_TEST_CASES)
    foreach(R_LIBRARY_R_TEST_FILE IN LISTS R_LIBRARY_R_TEST_FILES)
        get_filename_component(R_LIBRARY_R_TEST_NAME "${R_LIBRARY_R_TEST_FILE}" NAME_WE)
        list(APPEND R_LIBRARY_R_TEST_CASES "rtest_${R_LIBRARY_R_TEST_NAME}")
    endforeach()
    foreach(R_CODEGEN_CASE IN ITEMS
            ${R_EXAMPLE_CASES}
            ${R_LIBRARY_R_TEST_CASES}
            main_error_cleanup
            main_domain_allocation
            async_process_exit
            async_process_exit_scope
            async_process_exit_thread
            main_domain_io
            main_domain_filesystem
            main_domain_network
            main_domain_conversion
            main_domain_format
            main_domain_async_runtime
            main_domain_threading
            main_domain_math
            main_domain_time
            main_domain_environment
            main_domain_process
            main_domain_bytes
            main_domain_string
            main_domain_c_abi
            main_typed_core_utf8
            main_typed_bits
            main_typed_parse
            main_typed_range
            main_typed_env
            main_typed_fs
            main_typed_path
            main_typed_io
            main_typed_math
            main_typed_net
            main_typed_process
            main_typed_string
            main_typed_duration
            main_typed_time
            main_typed_alloc
            main_await_error
            main_user_status
            main_reserved_status
            main_negative_status
            main_truncated_status
            success
            stack_entries
            stack_recursive_own
            stack_recursive_rc
            stack_recursive_arc
            stack_recursive_enum
            stack_recursive_value
            stack_recursive_array
            stack_recursive_list
            stack_recursive_dict
            stack_recursive_fixed
            stack_recursive_option
            stack_recursive_owner_array
            fixed_array_move
            open_ranges
            methods
            traits
            trait_defaults
            trait_parameters
            opaque_results
            match_expressions
            scoped_tasks
            scoped_borrows
            scoped_views
            scoped_net
            scoped_console
            scoped_file
            positional_file
            file_locks
            scoped_cancel
            scoped_match_cancel
            scoped_start_recovery
            scoped_composition
            scoped_native
            scoped_select
            scoped_select_io
            generic_error_order
            qualified_constants
            dyn_default_constants
            module_strings
            associated_constant_instances
            static_initializers
            type_names
            async_borrow_calls
            async_opaque_starts
            string_index
            constant_contexts
            disjoint_field_borrows
            string_slice_params
            never_results
            exclusive_results
            optional_borrows
            string_containers
            view_containers
            scoped_aggregate_params
            exclusive_view_holders
            view_holder_containers
            parameter_content
            array_index
            async_array_index
            array_index_bounds
            async_array_index_bounds
            async_call_deref
            thread_scoped_exclusive
            async_thread_scoped_exclusive
            tagged_fallthrough
            async_tagged_fallthrough
            async_never_results
            async_never_panic
            atomic_raw_pointers
            async_atomic_raw_pointers
            atomic_raw_modules
            const_checked_locals
            variant_view_moves
            async_variant_view_moves
            caught_borrow_errors
            async_caught_borrow_errors
            stores_through_calls
            async_stores_through_calls
            never_objects
            async_never_objects
            never_objects_panic
            main_only_panics
            view_holding_parameters
            async_view_holding_parameters
            generic_owners
            view_holder_moves
            async_view_holder_moves
            standard_sequences
            dyn_views
            async_dyn_views
            generic_callable_results
            owner_element_containers
            standard_enums
            async_standard_enums
            tcp_shutdown_after_end
            associated_items
            async_associated_items
            deep_statements
            async_deep_statements
            held_results
            async_held_results
            parameter_stores
            async_parameter_stores
            owner_views
            async_owner_views
            lending_methods
            async_lending_methods
            associated_type_parameters
            async_associated_type_parameters
            expected_types
            async_expected_types
            tuples
            async_tuples
            packs
            async_packs
            pack_start_recovery
            chains
            async_chains
            labels
            async_labels
            error_families
            async_error_families
            standard_fault
            async_standard_fault
            error_family_modules
            composed_membership
            async_composed_membership
            match_projection
            async_match_projection
            call_projection
            async_call_projection
            generic_match
            async_generic_match
            match_owners_true
            async_match_owners_true
            match_owners
            async_match_owners
            async_match_expressions
            async_opaque_results
            async_trait_parameters
            async_trait_defaults
            generic_methods
            overloads
            null_type
            async_null_type
            explicit_generics
            async_explicit_generics
            consteval
            async_consteval
            consteval_checked
            async_consteval_checked
            consteval_floats
            async_consteval_floats
            consteval_owners
            consteval_frozen
            async_consteval_frozen
            deadline_blocks
            budget_blocks
            recursion_bounds
            async_for
            expression_forms
            async_channels
            group_detach
            async_owner_matrix
            async_owner_shapes
            resource_contracts
            static_conditions
            async_static_conditions
            static_values
            async_static_values
            computed_bounds
            async_computed_bounds
            typed_constants
            async_typed_constants
            associated_constants
            async_associated_constants
            dyn_interfaces
            async_dyn_interfaces
            replacement
            async_replacement
            replacement_shapes
            async_replacement_shapes
            overload_methods_async
            async_methods
            closures
            closure_modes
            async_closure_modes
            closure_start_recovery
            async_closure_start_recovery
            closure_suspend
            async_closure_suspend
            thread_payload_drop
            associated_types
            range_for
            membership
            collections
            variadic
            variadic_views
            effect_relay
            group_vacancy
            group_capacity
            swap_clone
            async_swap_clone
            clone_failures
            derive
            async_derive
            derive_modules
            cmp_impls
            function_values
            async_function_values
            function_values_modules
            dyn_owners
            async_dyn_owners
            dyn_owners_modules
            async_locks
            async_broadcast
            async_reserve
            async_broadcast_zero
            async_reserve_zero
            async_blocking
            async_blocking_start
            core_format
            async_core_format
            core_format_failures
            field_defaults
            async_field_defaults
            json_field_defaults
            field_defaults_failures
            std_stream
            std_bufio
            std_console
            borrow_fields
            iterator_adapter
            container_len
            generic_set
            library_cmp
            library_iter
            library_containers
            library_discardable
            library_deflate
            library_xml
            library_service
            deep_nesting
            library_slice_heap
            library_sorted_text
            regex
            regex_o2
            regex_driver
            regex_oom
            regex_example
            async_regex
            regex_captures
            library_text
            library_encoding
            library_iter_reductions
            library_byte_cursor
            async_library_text
            library_text_oom
            encoding_driver
            json_schema_driver
            library_random
            library_uuid
            library_hmac
            async_library_tokens
            hash_driver
            hash_digest_values
            library_time_formats
            library_time_interval
            standard_record_constructors
            time_driver
            cbor_driver
            crypto_driver
            cose_driver
            sqlite_driver
            dead_new_helpers
            library_signal
            library_net_connect
            library_net_options
            library_net_unix
            library_args
            library_config
            library_log
            library_async_task_id
            library_task_id_outside
            test_mode
            string_keys
            async_math_records
            numeric_error_codes
            process_outcome_records
            string_outcomes
            fs_metadata_fields
            async_parameter_reassignment
            reflection
            async_reflection
            aggregate
            value_types
            atomic_initialization
            async_atomic_initialization
            core_integer_intrinsics
            async_core_integer_intrinsics
            core_integer_all
            core_integer_all_o2
            core_atomic_intrinsics
            async_core_atomic_intrinsics
            core_atomic_invalid_order
            async_core_atomic_invalid_order
            core_atomic_fetch_add_overflow
            async_core_atomic_fetch_sub_overflow
            repr_c
            implicit_final_return
            enum_drop
            async_enum_drop
            user_drop
            async_user_drop
            payload_enum
            async_payload_enum
            enum_discriminants
            async_enum_discriminants
            generic_enum
            generic_vector
            generic_vector_push_oom
            generic_vector_push_oom_o2
            array_create
            async_array_create
            generic_example
            method_example
            reflection_example
            tables_example
            generic_constants
            closure_checked
            generic_closure
            function_items
            out
            async_function_items
            generic_error_sets
            generic_error_sets_borrow
            async_generic_error_sets
            borrowed_views
            async_borrowed_views
            must_use
            async_must_use
            discardable
            async_discardable
            generic_async_closure
            closure_async_body
            composed_calls
            async_composed_calls
            composed_start_recovery
            composed_suspend
            borrow_components
            async_borrow_components
            generic_borrow_components
            error_borrow_components
            generic_error_borrow_components
            const_generics
            async_const_generics
            const_length_bounds
            const_generic_owners
            async_const_generic_owners
            generic_borrow
            generic_error_borrow
            generic_conditional_borrow_same_origin
            branch_slice_borrow_same_origin
            async_generic_vector
            async_generic_functions
            generic_drop
            async_generic_drop
            generic_errors
            async_generic_errors
            generic_functions
            generic_static
            async_generic_static
            generic_key
            async_generic_key
            copy_move
            async_copy_move
            regression_move_reinitialization
            regression_exclusive_reborrow
            checked_effects
            error_declarations
            async_error_declarations
            conditional_expression
            async_conditional_expression
            regression_ternary_catch_state
            conditional_throw
            async_conditional_throw
            regression_unreachable_throw_catch_state
            regression_async_main_constant_switch_startup_args
            regression_async_main_constant_conditional_startup_args
            regression_async_main_lazy_catch_startup_args
            regression_async_main_short_circuit_startup_args
            regression_async_main_nested_catch_startup_args
            regression_async_main_pre_suspend_catch_startup_args
            regression_async_main_start_error_catch_startup_args
            regression_async_main_scoped_startup_view
            regression_async_checked_conversion
            regression_array_borrow_ends_before_resize
            regression_array_shared_view_from_mut_parameter
            regression_branch_merged_borrow_unrelated_drop
            regression_option_payload_borrow_owner_lifetime
            regression_aggregate_borrow_owner_lifetime
            audit_unary_postfix_precedence
            audit_list_returned_node_identity
            audit_mixed_error_nonborrow_variant
            audit_call_borrow_then_move_own_distinct_origins
            audit_call_borrow_then_nested_move_distinct_origin
            audit_call_disjoint_field_borrows
            audit_call_borrow_then_unreachable_nested_move
            audit_aggregate_initializer_borrow_then_move_distinct_origins
            audit_aggregate_initializer_unreachable_move_same_origin
            audit_aggregate_initializer_shared_borrow_then_read_same_origin
            audit_std_bytes_append_distinct_source
            audit_std_bytes_copy_disjoint_ranges
            audit_std_bytes_fill_read_distinct_origin
            audit_std_string_append_distinct_source
            audit_std_string_mutation_read_distinct_origin
            audit_std_array_reserve_read_distinct_origin
            audit_field_assignment_borrow_provenance_control
            audit_field_assignment_releases_previous_origin
            audit_disjoint_internal_field_assignment
            audit_multi_field_releases_replaced_origin
            audit_multi_field_disjoint_owner_replacement
            audit_move_self_contained_borrow_after_last_use
            audit_nested_aggregate_move_after_last_use
            audit_nested_aggregate_disjoint_owner_replacement
            audit_copy_borrow_aggregate_field_reassignment
            audit_copy_borrow_aggregate_dead_copy_reassignment
            audit_nll_future_overwrite_false_rejection
            audit_nll_overwrite_before_source_mutation
            audit_nll_branch_replaces_borrow
            audit_nll_field_replaces_borrow
            audit_nll_disjoint_field_replacement
            audit_nll_throw_branch_replaces_borrow
            audit_nll_break_branch_replaces_borrow
            audit_nll_continue_branch_replaces_borrow
            audit_nll_throw_else_branch_replaces_borrow
            audit_nll_infinite_loop_branch_replaces_borrow
            audit_nll_terminating_switch_branch_replaces_borrow
            audit_nll_explicit_throw_branches_control
            audit_switch_nested_break_false_rejection
            audit_switch_nested_break_targets
            audit_async_switch_nested_break_targets
            audit_return_nested_finally_reinitializes_owner
            audit_reinitialize_owner_before_nested_finally_control
            audit_finally_transfer_keeps_owner_control
            audit_dict_entry_ref_fields
            audit_std_alloc_bytes_call
            audit_std_bytes_equal_call
            audit_std_bytes_compare_call
            audit_std_c_target_call
            audit_std_c_raw_fn
            audit_std_c_raw_fn_modules
            audit_std_c_raw_fn_null
            audit_async_std_c_raw_fn_null
            audit_std_c_raw_fn_null_entry
            audit_std_c_adopt_handle
            audit_async_std_c_adopt_handle
            audit_std_c_handle_access
            audit_std_c_strings
            audit_std_c_thread_attachment
            audit_std_convert_parse_all
            audit_std_error_erasures
            audit_std_env_surface
            audit_std_fs_paths
            audit_std_fs_complete
            audit_std_fs_sync_start
            audit_std_format_builder
            audit_std_format_integer_all
            audit_std_math_abs_call
            audit_std_net_ip
            audit_std_net_operations
            audit_std_net_sync_starts
            audit_std_process_simple
            audit_std_thread_surface
            audit_std_math_all_operations
            audit_async_std_convert_parse_all
            audit_async_std_math_all_operations
            audit_std_math_sin_call
            audit_std_math_surface_call
            audit_std_string_create_call
            audit_std_string_len_call
            audit_std_string_views
            audit_std_sync_simple
            audit_std_time_duration_call
            audit_std_time_duration_seconds_call
            audit_std_time_sleep
            audit_async_std_library_calls
            regression_shared_reborrow_ends_before_mutation
            regression_unreachable_after_throw_catch_state
            regression_unreachable_break_after_continue
            throw_else
            async_throw_else
            move_error_rethrow
            sync_checked_retry
            sync_async_start_user
            sync_async_start_std
            async_sync_checked_retry
            checked_finally_control
            checked_finally_control_o2
            effect_cleanup
            abort_skips_finally
            async_checked_effects
            async_cancel_finally
            async_shared_finally
            async_finally_owner_lifetime
            float_literals
            constexpr_str
            integer_widths
            regression_implicit_numeric_conversions
            regression_condition_numeric_comparison
            regression_c_abi_numeric_operators
            regression_async_c_abi_numeric_operators
            regression_nullable_pointer_flow
            regression_async_nullable_pointer_flow
            c_abi_compound_overflow
            async_c_abi_compound_overflow
            bitwise_shift
            borrow_parameters
            multi_origin_borrow
            multidimensional_array
            module_storage
            async_module_storage
            unreachable_catch
            module_storage_import
            static_move_storage
            discard
            discard_panic
            panic
            async_panic
            scalar_casts
            async_scalar_casts
            char_compare
            char_cast_failure
            enum_cast_failure
            async_enum_cast_failure
            loop_jumps
            regression_folded_unreachable_loop_borrow
            switch_clauses
            char_switch
            enum_fallthrough
            regression_constant_switch_move
            regression_while_false_catch_state
            regression_constant_switch_catch_state
            sync_main_args
            core_assume
            core_slice_from_raw_parts
            core_slice_from_raw_parts_in
            core_volatile
            core_adopt_release
            core_adopt_nontrivial
            core_adopt_type_glue
            core_adopt_runtime_values
            core_adopt_named_standard_copy
            core_adopt_named_standard_layout
            core_adopt_named_standard_move
            core_adopt_compiler_specialized_move
            named_standard_move_abi
            native_result_abi
            standard_outcomes
            io_as_error
            fs_as_error
            fs_path_is_absolute
            array_slice
            sync_aggregate_destination
            array_operations
            container_operations
            container_ownership_failures
            view_container_failures
            bytes_operations
            secret_buffer
            async_secret_buffer
            moved_local_effect_exit
            deny_panic_alloc
            sync_channel_factories
            sync_value_operations
            sync_lock_operations
            sync_move_payloads
            async_sync_lock_operations
            async_sync_move_payloads
            async_sync_value_operations
            hash_operations
            hash_independent
            async_hash_independent
            async_array_operations
            async_container_operations
            new_expression
            managed_owner_access
            shared_owner_clone
            shared_owner_operations
            async_shared_owner_operations
            async_shared_owner_clone_field
            shared_owner_clone_sync
            async_new_expression
            async_scalar
            async_enum
            async_option_path
            async_short_circuit
            async_fused_default_own
            async_sync_call
            async_max_call_borrow
            async_mir_slice
            async_mir_slice_bounds
            async_array_slice
            async_float_literal
            async_core_assume
            async_core_slice_from_raw_parts
            async_core_slice_from_raw_parts_in
            async_core_volatile
            async_core_adopt_release
            async_core_adopt_nontrivial
            async_main_args
            async_u64
            async_drop
            async_standard_outcomes
            async_cancel
            async_detach
            thread_detach
            thread_join
            thread_outcomes
            dict_custom_keys
            common_error_fields
            sync_initializer_cleanup_growth
            thread_spawn
            stdin
            async_stdin
            stdout
            async_stderr
            async_stdout
            async_flush
            async_io_read
            async_io_read_result
            async_io_read_result_end
            async_io_write_result
            async_io_write_shared
            async_io_write_all
            async_io_close_input
            async_io_close_output
            async_fs_read_file
            async_fs_open_directory
            async_fs_file_metadata
            async_fs_create_directory_beneath
            async_fs_write_file_atomic_no_replace_beneath
            async_copy_args
            async_copy_aggregate
            async_move_args
            await_move_args
            await_generic_errors
            await_cancel_finally
            format_example
            nullable_pointers_example
            c_callbacks_example
            format_literals
            json_tree
            json_tree_o2
            json_decode
            async_json_decode
            json_example
            json_hooks_example
            json_stream_example
            json_reader_example
            async_json_tree
            async_json_tree_o2
            json_stream
            async_json_stream
            json_reader
            json_reader_transports
            async_format_literals
            await_calls
            async_string_views
            async_many_args
            async_large_frame
            slice_import
            range_slice
            range_bounds
            integer_switch
            checked_conversion
            checked_matrix
            link_available
            async_link_available
            ffi_import
            async_ffi_import
            ffi_bridge
            ffi_types
            async_ffi_types
            ffi_records
            ffi_userdata
            ffi_conformance
            ffi_null_ingress
            ffi_enum_ingress
            ffi_repr_c_imports
            ffi_repr_c_objects
            async_ffi_repr_c
            ffi_enum_result_ingress
            ffi_struct_result_ingress
            ffi_struct_null_ingress
            ffi_object_enum_ingress
            async_ffi_object_enum_ingress
            ffi_callback_struct_ingress
            ffi_never_results
            ffi_never_returns
            async_ffi_never_returns
            ffi_wide_declarations
            library_native_merge
            audit_aggregate_slice_field
            audit_std_library_schema_calls
            audit_float_arithmetic
            audit_float_to_integer_cast
            audit_mixed_integer_arithmetic
            audit_for_statement
            audit_aggregate_checked_initializer_cleanup
            audit_async_aggregate_checked_initializer_cleanup
            audit_move_assignment_replacement_cleanup
            audit_async_move_assignment_replacement_cleanup
            audit_move_field_assignment_replacement_cleanup
            audit_async_move_field_assignment_replacement_cleanup
            audit_own_assignment_replacement_cleanup
            audit_assignment_close_cases
            audit_async_assignment_close_cases
            audit_assignment_array_reallocation_close
            audit_async_assignment_array_reallocation_close
            audit_compound_rhs_overlapping_write
            audit_float_compound_assignment
            audit_prefix_increment
            audit_postfix_increment
            audit_async_compound_updates
            audit_update_place_evaluated_once
            bounds
            prefix_increment_overflow
            postfix_decrement_overflow
            overflow
            subtraction_overflow
            multiplication_overflow
            i64_addition_overflow
            i64_multiplication_overflow
            isize_division_overflow
            i8_compound_overflow
            integer_cast_failure
            float_to_integer_cast_failure
            async_float_to_integer_cast_failure
            invalid_shift
            signed_shift_overflow
            division_overflow
            modulo_overflow
            negation_overflow
            division_by_zero
            modulo_by_zero
            u32_division_by_zero
            u32_modulo_by_zero
            regression_loop_exit_states
            regression_loop_carried_borrow_valid
            regression_loop_carried_borrow_overwritten_before_drop
            regression_loop_condition_borrow_ends_before_mutation
            regression_loop_nested_overwrite_before_drop
            regression_return_path_disjoint_borrow
            regression_loop_reassigned_borrow_owner_lifetime
            regression_condition_constant_short_circuit_move
            regression_finally_path_move_cleanup
            regression_array_push_move_value
            regression_list_remove_after_shared_alias_last_use
            regression_list_remove_different_node_alias
            regression_throw_parameter_borrow_payload
            regression_throw_catch_borrow_outer_local
            regression_generic_error_variant_borrow_plain
            regression_short_circuit_move_cleanup
            regression_integer_switch_move_cleanup
            regression_short_circuit_catch_state
            regression_scoped_unborrowed_generic_local
            regression_unused_constant_string
            regression_atomic_glue_unused_drop
            regression_fn_value_unborrowed
            regression_container_helper_kinds
            regression_async_arc_array_index
            regression_async_variant_standard_call
            regression_async_aggregate_argument
            regression_match_wildcard_fields
            regression_fn_type_later_struct
            regression_dispatcher_without_targets
            regression_fn_type_nested_result
            regression_async_variant_call_argument
            regression_unreachable_helpers
            regression_generic_item_in_generic
            regression_select_branch_pending
            regression_standard_element_helpers
            regression_cancelled_await_barrier
            regression_standard_argument_send
            regression_select_branch_declaration
            regression_async_array_element_glue)
        set(R_CODEGEN_MODULE_MAP "")
        set(R_CODEGEN_ENTRY "")
        set(R_CODEGEN_HASH_ONLY OFF)
        if(R_CODEGEN_CASE MATCHES "^(async_)?hash_independent$")
            set(R_CODEGEN_HASH_ONLY ON)
        endif()
        set(R_CODEGEN_PROGRAM_ARGUMENTS)
        set(R_CODEGEN_CHECK_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/check_codegen_program.cmake")
        set(R_CODEGEN_PROGRAM_INPUT_FILE "")
        set(R_CODEGEN_EXPECTED_EXIT_STATUS 0)
        set(R_CODEGEN_EXPECTED_STDERR "")
        set(R_CODEGEN_LINK_MANIFEST "")
        set(R_CODEGEN_EXTRA_C_SOURCES "")
        set(R_CODEGEN_ABI_VERIFY OFF)
        set(R_CODEGEN_ABI_INVENTORY OFF)
        set(R_CODEGEN_HEADER_INCLUDE_DIRS "")
        set(R_CODEGEN_C_WRAPPER "")
        set(R_CODEGEN_CREATE_DIRECTORY_RELATIVE "")
        set(R_CODEGEN_ATOMIC_WRITE_SOURCE "")
        set(R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_SOURCE "")
        set(R_CODEGEN_ATOMIC_WRITE_RELATIVE "")
        set(R_CODEGEN_MEASURE_STACK_USAGE TRUE)
        set(R_CODEGEN_STACK_MAX_FRAME_SIZE 262144)
        set(R_CODEGEN_STACK_EXPECT_FRAME_GREATER_THAN "")
        set(R_CODEGEN_CHECK_STDOUT FALSE)
        set(R_CODEGEN_EXPECTED_STDOUT "")
        set(R_CODEGEN_GENERATED_C_OPTIMIZATION "-O0")
        # R-FUNC-0025 (M24): cases translated in test mode.
        set(R_CODEGEN_TEST_MODE OFF)
        if(R_CODEGEN_CASE IN_LIST R_EXAMPLE_CASES)
            set(R_CODEGEN_SOURCE_1 "")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_MODULE_MAP "${PROJECT_SOURCE_DIR}/${R_EXAMPLE_${R_CODEGEN_CASE}_MAP}")
            set(R_CODEGEN_ENTRY "${R_EXAMPLE_${R_CODEGEN_CASE}_ENTRY}")
            if(DEFINED R_EXAMPLE_${R_CODEGEN_CASE}_LINK_MANIFEST)
                set(R_CODEGEN_LINK_MANIFEST "${PROJECT_SOURCE_DIR}/${R_EXAMPLE_${R_CODEGEN_CASE}_LINK_MANIFEST}")
                set(R_CODEGEN_ABI_VERIFY ON)
            endif()
            if(DEFINED R_EXAMPLE_${R_CODEGEN_CASE}_TEST_MODE)
                set(R_CODEGEN_TEST_MODE ${R_EXAMPLE_${R_CODEGEN_CASE}_TEST_MODE})
            endif()
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "format_literals" OR R_CODEGEN_CASE STREQUAL "async_format_literals")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_format_literals_wrapper.c")
        elseif(R_CODEGEN_CASE STREQUAL "json_reader_transports")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_json_reader_transports.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_json_reader_transports_wrapper.c")
        elseif(R_CODEGEN_CASE STREQUAL "json_reader")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_json_reader.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_json_reader_wrapper.c")
        elseif(R_CODEGEN_CASE STREQUAL "json_tree" OR
               R_CODEGEN_CASE STREQUAL "json_tree_o2" OR
               R_CODEGEN_CASE STREQUAL "async_json_tree" OR
               R_CODEGEN_CASE STREQUAL "async_json_tree_o2" OR
               R_CODEGEN_CASE STREQUAL "json_decode" OR
               R_CODEGEN_CASE STREQUAL "async_json_decode" OR
               R_CODEGEN_CASE STREQUAL "json_stream" OR
               R_CODEGEN_CASE STREQUAL "async_json_stream")
            if(R_CODEGEN_CASE STREQUAL "json_tree_o2")
                set(R_CODEGEN_SOURCE_1
                    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_json_tree.r")
                set(R_CODEGEN_GENERATED_C_OPTIMIZATION "-O2")
            elseif(R_CODEGEN_CASE STREQUAL "async_json_tree_o2")
                set(R_CODEGEN_SOURCE_1
                    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_json_tree.r")
                set(R_CODEGEN_GENERATED_C_OPTIMIZATION "-O2")
            else()
                set(R_CODEGEN_SOURCE_1
                    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            endif()
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            if(R_CODEGEN_CASE STREQUAL "json_decode" OR R_CODEGEN_CASE STREQUAL "async_json_decode" OR R_CODEGEN_CASE STREQUAL "json_stream" OR R_CODEGEN_CASE STREQUAL "async_json_stream")
                set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_json_wrapper.c")
            endif()
        elseif(R_CODEGEN_CASE STREQUAL "format_example")
            set(R_CODEGEN_SOURCE_1 "${PROJECT_SOURCE_DIR}/examples/format/main.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "nullable_pointers_example")
            set(R_CODEGEN_SOURCE_1 "${PROJECT_SOURCE_DIR}/examples/pointers/main.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "c_callbacks_example")
            set(R_CODEGEN_SOURCE_1 "${PROJECT_SOURCE_DIR}/examples/c_callbacks/main.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE MATCHES
               "^audit_(field_assignment_borrow_provenance_control|field_assignment_releases_previous_origin|disjoint_internal_field_assignment)$")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE MATCHES
               "^audit_multi_field_(releases_replaced_origin|disjoint_owner_replacement)$")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "qualified_constants" OR
               R_CODEGEN_CASE STREQUAL "atomic_raw_modules" OR
               R_CODEGEN_CASE STREQUAL "error_family_modules" OR
               R_CODEGEN_CASE STREQUAL "derive_modules" OR
               R_CODEGEN_CASE STREQUAL "function_values_modules" OR
               R_CODEGEN_CASE STREQUAL "dyn_owners_modules")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}_api.r")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "audit_std_c_raw_fn_modules")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audit_std_c_raw_fn_modules.r")
            set(R_CODEGEN_SOURCE_2 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audit_std_c_callback_module.r")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE MATCHES "^audit_(async_)?std_c_raw_fn_null$")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "contract_violation")
        elseif(R_CODEGEN_CASE MATCHES "^async_(broadcast|reserve)_zero$")
            # R-SLIB-ASYNC-0016 and R-LIB-0016 (L30): zero capacity is a contract violation.
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "contract_violation")
        elseif(R_CODEGEN_CASE MATCHES "^(async_)?core_atomic_invalid_order$")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "contract_violation")
        elseif(R_CODEGEN_CASE STREQUAL "audit_std_c_raw_fn_null_entry")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "contract_violation")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_raw_fn_null_entry_wrapper.c")
        elseif(R_CODEGEN_CASE MATCHES "^json_(hooks|stream|reader)_example$")
            set(R_CODEGEN_SOURCE_1 "${PROJECT_SOURCE_DIR}/examples/json/${CMAKE_MATCH_1}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            if(R_CODEGEN_CASE STREQUAL "json_reader_example")
                set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_json_reader_wrapper.c")
            else()
                set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_json_wrapper.c")
            endif()
        elseif(R_CODEGEN_CASE STREQUAL "main_error_cleanup")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_error_cleanup.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_main_error_cleanup_wrapper.c")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 113)
            set(R_CODEGEN_EXPECTED_STDERR "domain=io name=permission_denied code=2 native_code=-9223372036854775808")
            set(R_CODEGEN_CHECK_STDOUT TRUE)
            set(R_CODEGEN_EXPECTED_STDOUT "cleanup complete\n")
        elseif(R_CODEGEN_CASE MATCHES "^async_process_exit(_scope|_thread)?$")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            if(R_CODEGEN_CASE STREQUAL "async_process_exit")
                set(R_CODEGEN_EXPECTED_EXIT_STATUS 7)
            elseif(R_CODEGEN_CASE STREQUAL "async_process_exit_scope")
                set(R_CODEGEN_EXPECTED_EXIT_STATUS 11)
            else()
                set(R_CODEGEN_EXPECTED_EXIT_STATUS 13)
            endif()
        elseif(R_CODEGEN_CASE STREQUAL "main_domain_allocation")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_domain_allocation.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 112)
            set(R_CODEGEN_EXPECTED_STDERR "domain=allocation name=")
        elseif(R_CODEGEN_CASE STREQUAL "main_domain_io")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_domain_io.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 113)
            set(R_CODEGEN_EXPECTED_STDERR "domain=io name=")
        elseif(R_CODEGEN_CASE STREQUAL "main_domain_filesystem")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_domain_filesystem.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 113)
            set(R_CODEGEN_EXPECTED_STDERR "domain=filesystem name=")
        elseif(R_CODEGEN_CASE STREQUAL "main_domain_network")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_domain_network.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 114)
            set(R_CODEGEN_EXPECTED_STDERR "domain=network name=")
        elseif(R_CODEGEN_CASE STREQUAL "main_domain_conversion")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_domain_conversion.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 115)
            set(R_CODEGEN_EXPECTED_STDERR "domain=conversion name=")
        elseif(R_CODEGEN_CASE STREQUAL "main_domain_format")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_domain_format.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 115)
            set(R_CODEGEN_EXPECTED_STDERR "domain=format name=")
        elseif(R_CODEGEN_CASE STREQUAL "main_domain_async_runtime")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_domain_async_runtime.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 116)
            set(R_CODEGEN_EXPECTED_STDERR "domain=async_runtime name=")
        elseif(R_CODEGEN_CASE STREQUAL "main_domain_threading")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_domain_threading.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 116)
            set(R_CODEGEN_EXPECTED_STDERR "domain=threading name=")
        elseif(R_CODEGEN_CASE STREQUAL "main_domain_math")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_domain_math.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 117)
            set(R_CODEGEN_EXPECTED_STDERR "domain=math name=")
        elseif(R_CODEGEN_CASE STREQUAL "main_domain_time")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_domain_time.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 117)
            set(R_CODEGEN_EXPECTED_STDERR "domain=time name=")
        elseif(R_CODEGEN_CASE STREQUAL "main_domain_environment")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_domain_environment.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 117)
            set(R_CODEGEN_EXPECTED_STDERR "domain=environment name=")
        elseif(R_CODEGEN_CASE STREQUAL "main_domain_process")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_domain_process.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 117)
            set(R_CODEGEN_EXPECTED_STDERR "domain=process name=")
        elseif(R_CODEGEN_CASE STREQUAL "main_domain_bytes")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_domain_bytes.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 117)
            set(R_CODEGEN_EXPECTED_STDERR "domain=bytes name=")
        elseif(R_CODEGEN_CASE STREQUAL "main_domain_string")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_domain_string.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 117)
            set(R_CODEGEN_EXPECTED_STDERR "domain=string name=")
        elseif(R_CODEGEN_CASE STREQUAL "main_domain_c_abi")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_domain_c_abi.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 117)
            set(R_CODEGEN_EXPECTED_STDERR "domain=c_abi name=")
        elseif(R_CODEGEN_CASE STREQUAL "main_typed_core_utf8")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_typed_core_utf8.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 117)
            set(R_CODEGEN_EXPECTED_STDERR "R main error: domain=")
        elseif(R_CODEGEN_CASE STREQUAL "main_typed_bits")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_typed_bits.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 117)
            set(R_CODEGEN_EXPECTED_STDERR "R main error: domain=")
        elseif(R_CODEGEN_CASE STREQUAL "main_typed_parse")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_typed_parse.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 115)
            set(R_CODEGEN_EXPECTED_STDERR "R main error: domain=")
        elseif(R_CODEGEN_CASE STREQUAL "main_typed_range")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_typed_range.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 115)
            set(R_CODEGEN_EXPECTED_STDERR "R main error: domain=")
        elseif(R_CODEGEN_CASE STREQUAL "main_typed_env")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_typed_env.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 117)
            set(R_CODEGEN_EXPECTED_STDERR "R main error: domain=")
        elseif(R_CODEGEN_CASE STREQUAL "main_typed_fs")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_typed_fs.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 113)
            set(R_CODEGEN_EXPECTED_STDERR "R main error: domain=")
        elseif(R_CODEGEN_CASE STREQUAL "main_typed_path")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_typed_path.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 113)
            set(R_CODEGEN_EXPECTED_STDERR "R main error: domain=")
        elseif(R_CODEGEN_CASE STREQUAL "main_typed_io")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_typed_io.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 113)
            set(R_CODEGEN_EXPECTED_STDERR "R main error: domain=")
        elseif(R_CODEGEN_CASE STREQUAL "main_typed_math")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_typed_math.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 117)
            set(R_CODEGEN_EXPECTED_STDERR "R main error: domain=")
        elseif(R_CODEGEN_CASE STREQUAL "main_typed_net")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_typed_net.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 114)
            set(R_CODEGEN_EXPECTED_STDERR "R main error: domain=")
        elseif(R_CODEGEN_CASE STREQUAL "main_typed_process")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_typed_process.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 117)
            set(R_CODEGEN_EXPECTED_STDERR "R main error: domain=")
        elseif(R_CODEGEN_CASE STREQUAL "main_typed_string")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_typed_string.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 117)
            set(R_CODEGEN_EXPECTED_STDERR "R main error: domain=")
        elseif(R_CODEGEN_CASE STREQUAL "main_typed_duration")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_typed_duration.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 117)
            set(R_CODEGEN_EXPECTED_STDERR "R main error: domain=")
        elseif(R_CODEGEN_CASE STREQUAL "main_typed_time")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_typed_time.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 117)
            set(R_CODEGEN_EXPECTED_STDERR "R main error: domain=")
        elseif(R_CODEGEN_CASE STREQUAL "main_typed_alloc")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_typed_alloc.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 112)
            set(R_CODEGEN_EXPECTED_STDERR "domain=allocation name=size_overflow code=1 native_code=0")
        elseif(R_CODEGEN_CASE STREQUAL "main_await_error")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_await_error.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 113)
            set(R_CODEGEN_EXPECTED_STDERR "domain=io name=broken_pipe code=1 native_code=32")
        elseif(R_CODEGEN_CASE STREQUAL "main_user_status")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_user_status.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 111)
            set(R_CODEGEN_EXPECTED_STDERR "")
        elseif(R_CODEGEN_CASE STREQUAL "main_reserved_status")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_reserved_status.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 124)
            set(R_CODEGEN_EXPECTED_STDERR "invalid_main_status")
        elseif(R_CODEGEN_CASE STREQUAL "main_negative_status")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_negative_status.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 124)
            set(R_CODEGEN_EXPECTED_STDERR "invalid_main_status")
        elseif(R_CODEGEN_CASE STREQUAL "main_truncated_status")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main_truncated_status.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 124)
            set(R_CODEGEN_EXPECTED_STDERR "invalid_main_status")
        elseif(R_CODEGEN_CASE STREQUAL "json_example")
            set(R_CODEGEN_SOURCE_1 "${PROJECT_SOURCE_DIR}/examples/json/main.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE MATCHES "^regression_")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE MATCHES "^rtest_(.+)$")
            set(R_CODEGEN_SOURCE_1 "${PROJECT_SOURCE_DIR}/library/r/tests/${CMAKE_MATCH_1}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_TEST_MODE ON)
        elseif(R_CODEGEN_CASE MATCHES "^(async_)?array_create$")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE MATCHES "^(async_)?atomic_initialization$")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE MATCHES "^(async_)?core_(integer|atomic)_intrinsics$")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "repr_c")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_repr_c.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "success")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_math.r")
            set(R_CODEGEN_SOURCE_2 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_main.r")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "recursive")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_recursive.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "aggregate")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/aggregate_smoke.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "value_types")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_value_types.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "implicit_final_return")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_implicit_final_return.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "checked_effects")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_checked_effects.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "error_declarations" OR
               R_CODEGEN_CASE STREQUAL "async_error_declarations" OR
               R_CODEGEN_CASE STREQUAL "payload_enum" OR
               R_CODEGEN_CASE STREQUAL "async_payload_enum")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER
                "${CMAKE_CURRENT_SOURCE_DIR}/codegen_error_declarations_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "enum_drop" OR
               R_CODEGEN_CASE STREQUAL "async_enum_drop")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_enum_drop_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "user_drop" OR
               R_CODEGEN_CASE STREQUAL "async_user_drop")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_user_drop_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE MATCHES "^(async_)?composed_membership$" OR R_CODEGEN_CASE STREQUAL "scoped_native" OR
               R_CODEGEN_CASE MATCHES "^(scoped_select(_io)?|generic_error_order|dyn_default_constants|module_strings|associated_constant_instances|static_initializers|type_names|async_borrow_calls|async_opaque_starts|string_index|constant_contexts|disjoint_field_borrows|string_slice_params|never_results|exclusive_results|optional_borrows|string_containers|view_containers|scoped_aggregate_params|exclusive_view_holders|view_holder_containers|parameter_content|array_index|async_array_index|async_call_deref|(async_)?thread_scoped_exclusive|(async_)?tagged_fallthrough|async_never_results|(async_)?atomic_raw_pointers|const_checked_locals|(async_)?variant_view_moves|(async_)?caught_borrow_errors|(async_)?stores_through_calls|(async_)?never_objects|(async_)?view_holding_parameters|generic_owners|(async_)?view_holder_moves|standard_sequences|(async_)?dyn_views|generic_callable_results|owner_element_containers|(async_)?standard_enums|tcp_shutdown_after_end|(async_)?associated_items|(async_)?deep_statements|(async_)?held_results|(async_)?parameter_stores|(async_)?owner_views|(async_)?lending_methods|(async_)?associated_type_parameters|(async_)?expected_types|(async_)?tuples|(async_)?packs|(async_)?chains|(async_)?labels|(async_)?error_families|(async_)?standard_fault)$")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            if(R_CODEGEN_CASE STREQUAL "async_composed_membership")
                set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_async_composed_membership_wrapper.c")
            endif()
        elseif(R_CODEGEN_CASE MATCHES "^(async_)?(match_projection|call_projection)$")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_user_drop_wrapper.c")
        elseif(R_CODEGEN_CASE STREQUAL "scoped_composition")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_scoped_composition.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_scoped_composition_wrapper.c")
        elseif(R_CODEGEN_CASE STREQUAL "scoped_start_recovery")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_scoped_start_recovery.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_scoped_start_recovery_wrapper.c")
        elseif(R_CODEGEN_CASE MATCHES "^scoped_(match_)?cancel$")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_${R_CODEGEN_CASE}_wrapper.c")
        elseif(R_CODEGEN_CASE STREQUAL "scoped_console")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_scoped_console.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_PROGRAM_INPUT_FILE
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_scoped_console.input")
            set(R_CODEGEN_CHECK_STDOUT TRUE)
            set(R_CODEGEN_EXPECTED_STDOUT "scoped console bytes")
        elseif(R_CODEGEN_CASE STREQUAL "scoped_file")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_scoped_file.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_PROGRAM_ARGUMENTS "${CMAKE_CURRENT_BINARY_DIR}/codegen_scoped_file.bin")
        elseif(R_CODEGEN_CASE STREQUAL "positional_file")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_positional_file.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_PROGRAM_ARGUMENTS "${CMAKE_CURRENT_BINARY_DIR}/codegen_positional_file.bin")
        elseif(R_CODEGEN_CASE STREQUAL "file_locks")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_file_locks.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_PROGRAM_ARGUMENTS "${CMAKE_CURRENT_BINARY_DIR}/codegen_file_locks.bin")
        elseif(R_CODEGEN_CASE MATCHES "^scoped_(tasks|borrows|views|net)$")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_owner_matrix")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_owner_matrix.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_async_owner_matrix_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE MATCHES "^(static_conditions|async_static_conditions|replacement|async_replacement|closure_modes|async_closure_modes|closure_checked|generic_closure|composed_calls|async_composed_calls|borrow_components|async_borrow_components|generic_borrow_components|error_borrow_components|generic_error_borrow_components|async_trait_defaults|async_trait_parameters|async_opaque_results|match_owners|async_match_owners|generic_match|async_generic_match|match_owners_true|async_match_owners_true|borrowed_views|async_borrowed_views|must_use|async_must_use|discardable|async_discardable|generic_async_closure|closure_async_body|thread_payload_drop|const_generic_owners|async_const_generic_owners)$")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_user_drop_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE MATCHES "^(out|(async_)?replacement_shapes|(async_)?const_generics|(async_)?function_items|(async_)?generic_error_sets(_borrow)?)$")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "const_length_bounds")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_const_length_bounds.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "bounds")
        elseif(R_CODEGEN_CASE STREQUAL "async_closure_suspend")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_closure_suspend.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_async_closure_suspend_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "closure_suspend")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_closure_suspend.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_closure_suspend_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "composed_suspend")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_composed_suspend.r")
            set(R_CODEGEN_EXPECT_EXIT 0)
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_composed_suspend_wrapper.c")
        elseif(R_CODEGEN_CASE STREQUAL "pack_start_recovery")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_pack_start_recovery.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_pack_start_recovery_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "composed_start_recovery")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_composed_start_recovery.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_composed_start_recovery_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_function_values")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_function_values.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_function_values_start_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE MATCHES "^(async_)?closure_start_recovery$")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_closure_start_recovery_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "resource_contracts")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_resource_contracts.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_resource_contracts_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_generic_errors")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_generic_errors.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_async_generic_errors_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "generic_example")
            set(R_CODEGEN_SOURCE_1 "${PROJECT_SOURCE_DIR}/examples/generics/src/main.r")
            set(R_CODEGEN_SOURCE_2 "${PROJECT_SOURCE_DIR}/examples/generics/src/vector.r")
            set(R_CODEGEN_C_WRAPPER "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "method_example")
            set(R_CODEGEN_SOURCE_1 "${PROJECT_SOURCE_DIR}/examples/methods/src/main.r")
            set(R_CODEGEN_SOURCE_2 "${PROJECT_SOURCE_DIR}/examples/methods/src/shapes.r")
            set(R_CODEGEN_C_WRAPPER "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "reflection_example")
            set(R_CODEGEN_SOURCE_1 "${PROJECT_SOURCE_DIR}/examples/reflection/src/main.r")
            set(R_CODEGEN_SOURCE_2 "${PROJECT_SOURCE_DIR}/examples/reflection/src/model.r")
            set(R_CODEGEN_C_WRAPPER "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "tables_example")
            set(R_CODEGEN_SOURCE_1 "${PROJECT_SOURCE_DIR}/examples/tables/src/main.r")
            set(R_CODEGEN_SOURCE_2 "${PROJECT_SOURCE_DIR}/examples/tables/src/model.r")
            set(R_CODEGEN_C_WRAPPER "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "generic_vector" OR
               R_CODEGEN_CASE STREQUAL "generic_vector_push_oom" OR
               R_CODEGEN_CASE STREQUAL "generic_vector_push_oom_o2" OR
               R_CODEGEN_CASE STREQUAL "async_generic_vector")
            if(R_CODEGEN_CASE STREQUAL "generic_vector_push_oom_o2")
                set(R_CODEGEN_SOURCE_1
                    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_generic_vector_push_oom.r")
                set(R_CODEGEN_GENERATED_C_OPTIMIZATION "-O2")
            else()
                set(R_CODEGEN_SOURCE_1
                    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            endif()
            set(R_CODEGEN_SOURCE_2 "")
            if(R_CODEGEN_CASE STREQUAL "generic_vector_push_oom" OR
               R_CODEGEN_CASE STREQUAL "generic_vector_push_oom_o2")
                set(R_CODEGEN_C_WRAPPER
                    "${CMAKE_CURRENT_SOURCE_DIR}/codegen_generic_vector_push_oom_wrapper.c")
            else()
                set(R_CODEGEN_C_WRAPPER
                    "${CMAKE_CURRENT_SOURCE_DIR}/codegen_generic_vector_wrapper.c")
            endif()
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "generic_drop" OR R_CODEGEN_CASE STREQUAL "async_generic_drop")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_generic_drop_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "generic_constants" OR
               R_CODEGEN_CASE STREQUAL "generic_borrow" OR
               R_CODEGEN_CASE STREQUAL "generic_error_borrow" OR
               R_CODEGEN_CASE STREQUAL "generic_conditional_borrow_same_origin" OR
               R_CODEGEN_CASE STREQUAL "branch_slice_borrow_same_origin" OR
               R_CODEGEN_CASE STREQUAL "generic_static" OR
               R_CODEGEN_CASE STREQUAL "async_generic_static" OR
               R_CODEGEN_CASE STREQUAL "generic_key" OR
               R_CODEGEN_CASE STREQUAL "async_generic_key" OR
               R_CODEGEN_CASE STREQUAL "generic_errors" OR
               R_CODEGEN_CASE STREQUAL "generic_enum" OR
               R_CODEGEN_CASE STREQUAL "async_generic_functions" OR
               R_CODEGEN_CASE STREQUAL "generic_functions" OR
               R_CODEGEN_CASE STREQUAL "enum_discriminants" OR
               R_CODEGEN_CASE STREQUAL "async_enum_discriminants" OR
               R_CODEGEN_CASE STREQUAL "copy_move" OR
               R_CODEGEN_CASE STREQUAL "async_copy_move" OR
               R_CODEGEN_CASE STREQUAL "conditional_expression" OR
               R_CODEGEN_CASE STREQUAL "async_conditional_expression" OR
               R_CODEGEN_CASE STREQUAL "conditional_throw" OR
               R_CODEGEN_CASE STREQUAL "async_conditional_throw" OR
               R_CODEGEN_CASE STREQUAL "throw_else" OR
               R_CODEGEN_CASE STREQUAL "async_throw_else")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "move_error_rethrow")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_move_error_rethrow.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "sync_checked_retry")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_sync_checked_retry.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_sync_checked_retry")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_sync_checked_retry.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "checked_finally_control" OR
               R_CODEGEN_CASE STREQUAL "checked_finally_control_o2")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_checked_finally_control.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER
                "${CMAKE_CURRENT_SOURCE_DIR}/codegen_checked_finally_control_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
            if(R_CODEGEN_CASE STREQUAL "checked_finally_control_o2")
                set(R_CODEGEN_GENERATED_C_OPTIMIZATION "-O2")
            endif()
        elseif(R_CODEGEN_CASE STREQUAL "effect_cleanup")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_effect_cleanup.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER
                "${CMAKE_CURRENT_SOURCE_DIR}/codegen_effect_cleanup_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "abort_skips_finally")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_abort_skips_finally.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "integer_overflow")
        elseif(R_CODEGEN_CASE STREQUAL "async_checked_effects")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_checked_effects.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_cancel_finally")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_cancel_finally.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER
                "${CMAKE_CURRENT_SOURCE_DIR}/codegen_async_cancel_finally_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_shared_finally")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_shared_finally.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER
                "${CMAKE_CURRENT_SOURCE_DIR}/codegen_async_shared_finally_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_finally_owner_lifetime")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_finally_owner_lifetime.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER
                "${CMAKE_CURRENT_SOURCE_DIR}/codegen_async_finally_owner_lifetime_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_detach")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_detach.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "thread_detach")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_thread_detach.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER
                "${CMAKE_CURRENT_SOURCE_DIR}/codegen_thread_detach_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "thread_outcomes")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_thread_outcomes.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_thread_outcomes_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "thread_join")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_thread_join.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER
                "${CMAKE_CURRENT_SOURCE_DIR}/codegen_thread_join_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "thread_spawn")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_thread_spawn.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER
                "${CMAKE_CURRENT_SOURCE_DIR}/codegen_thread_spawn_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "bytes_operations")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_bytes_operations.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE MATCHES "^(sync_channel_factories|sync_value_operations|sync_lock_operations|async_sync_value_operations|async_sync_lock_operations|sync_move_payloads|async_sync_move_payloads)$")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE MATCHES "^(secret_buffer|async_secret_buffer|moved_local_effect_exit|deny_panic_alloc|stack_entries|stack_recursive_(own|rc|arc|enum|value|array|list|dict|fixed|option|owner_array)|fixed_array_move|open_ranges|methods|traits|trait_defaults|trait_parameters|opaque_results|match_expressions|async_match_expressions|generic_methods|async_methods|closures|associated_types|range_for|membership|collections|variadic|variadic_views|borrow_fields|iterator_adapter|container_len|generic_set|library_cmp|library_iter|library_containers|library_discardable|library_deflate|library_xml|library_service|deep_nesting|unreachable_catch|library_slice_heap|library_sorted_text|reflection|async_reflection)$")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE MATCHES "^(async_owner_shapes|async_owner_matrix|null_type|async_null_type|explicit_generics|async_explicit_generics|consteval|async_consteval|consteval_checked|async_consteval_checked|consteval_floats|async_consteval_floats|consteval_owners|consteval_frozen|async_consteval_frozen|deadline_blocks|budget_blocks|recursion_bounds|async_for|expression_forms|async_channels|group_detach|group_vacancy|group_capacity|effect_relay|swap_clone|async_swap_clone|derive|async_derive|cmp_impls|function_values|static_values|async_static_values|computed_bounds|async_computed_bounds|typed_constants|async_typed_constants|associated_constants|async_associated_constants|dyn_interfaces|async_dyn_interfaces|dyn_owners|async_dyn_owners|async_locks|async_broadcast|async_reserve|async_blocking|async_blocking_start|core_format|async_core_format|field_defaults|async_field_defaults|json_field_defaults|overloads|dict_custom_keys|common_error_fields|sync_initializer_cleanup_growth|fs_metadata_fields|string_outcomes|async_parameter_reassignment|process_outcome_records|async_math_records|numeric_error_codes|regex|regex_o2|async_regex|regex_oom|regex_driver|regex_example|regex_captures|library_text|library_encoding|library_iter_reductions|library_byte_cursor|async_library_text|library_text_oom|encoding_driver|json_schema_driver|library_random|library_uuid|library_hmac|async_library_tokens|hash_driver|hash_digest_values|hash_operations|hash_independent|async_hash_independent|library_time_formats|library_time_interval|standard_record_constructors|time_driver|cbor_driver|crypto_driver|cose_driver|sqlite_driver|dead_new_helpers|library_signal|library_net_connect|library_net_options|library_net_unix|library_args|library_config|library_log|library_async_task_id|library_task_id_outside|test_mode|string_keys)$")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            if(R_CODEGEN_CASE STREQUAL "sqlite_driver")
                # M29: a short session of its own, so the build test reads no inherited input.
                set(R_CODEGEN_PROGRAM_INPUT_FILE
                    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_sqlite_driver.input")
                set(R_CODEGEN_CHECK_STDOUT TRUE)
                set(R_CODEGEN_EXPECTED_STDOUT "ok\ndone 1 1\ncolumns 78 747970656f66287829\nrow i42 t696e7465676572\nend\nerror sql 1\n")
            endif()
            if(R_CODEGEN_CASE STREQUAL "regex_o2")
                set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_regex.r")
                set(R_CODEGEN_GENERATED_C_OPTIMIZATION -O2)
            endif()
            if(R_CODEGEN_CASE MATCHES "^(regex|regex_o2|async_regex)$")
                set(R_CODEGEN_SOURCE_2 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regex_cases.r")
            endif()
            if(R_CODEGEN_CASE STREQUAL "regex_example")
                set(R_CODEGEN_SOURCE_1 "${PROJECT_SOURCE_DIR}/examples/regex/main.r")
            endif()
            if(R_CODEGEN_CASE MATCHES "^(regex_oom|library_text_oom)$")
                # M19: library_text_oom fails each allocation of the M19 operations in turn.
                set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_regex_oom_wrapper.c")
            endif()
            if(R_CODEGEN_CASE STREQUAL "async_blocking_start")
                # R-SLIB-ASYNC-0017: the wrapper fails the first two starts.
                set(R_CODEGEN_C_WRAPPER
                    "${CMAKE_CURRENT_SOURCE_DIR}/codegen_async_blocking_start_wrapper.c")
            endif()
            if(R_CODEGEN_CASE STREQUAL "string_keys")
                # M24-2: owned string keys, checked by test functions.
                set(R_CODEGEN_TEST_MODE ON)
            endif()
            if(R_CODEGEN_CASE STREQUAL "test_mode")
                # R-FUNC-0025 (M24): the report of the test entry, which fails five tests.
                set(R_CODEGEN_TEST_MODE ON)
                set(R_CODEGEN_EXPECTED_EXIT_STATUS 1)
                set(R_CODEGEN_CHECK_STDOUT TRUE)
                set(R_CODEGEN_EXPECTED_STDOUT
                    "test adds ... ok\ntest compares ... FAILED: expected 5, got 4\ntest converts ... FAILED: error invalid_digit\ntest customs ... FAILED: error test.codegen.test_mode::Custom\ntest quiet ... ok\ntest expects ... ok\ntest misses ... FAILED: expected the error std.convert::parse_error\ntest awaits ... FAILED: expected \"b\", got \"a\"\ntest builds ... ok (2 allocation failures)\ntest uses_helper ... ok\n10 tests: 5 passed, 5 failed\n")
            endif()
        elseif(R_CODEGEN_CASE STREQUAL "float_literals")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_float_literals.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER
                "${CMAKE_CURRENT_SOURCE_DIR}/codegen_float_literals_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "constexpr_str")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_constexpr_str.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "integer_widths")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_integer_widths.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "bitwise_shift")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_bitwise_shift.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "borrow_parameters")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_borrow_api.r")
            set(R_CODEGEN_SOURCE_2
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_borrow_main.r")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "multi_origin_borrow")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_multi_origin_borrow.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "multidimensional_array")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_multidimensional_array.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "module_storage")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_module_storage.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_module_storage")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_module_storage.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "module_storage_import")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_module_storage_import.r")
            set(R_CODEGEN_SOURCE_2
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_module_storage_api.r")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "static_move_storage")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_static_move_storage.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER
                "${CMAKE_CURRENT_SOURCE_DIR}/codegen_static_move_storage_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "discard")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_discard.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "loop_jumps")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_loop_jumps.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "switch_clauses" OR
               R_CODEGEN_CASE STREQUAL "char_switch" OR
               R_CODEGEN_CASE STREQUAL "enum_fallthrough")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "regression_while_false_catch_state" OR
               R_CODEGEN_CASE STREQUAL "regression_constant_switch_catch_state")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "sync_main_args")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_sync_main_args.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_EXPECTED_EXIT_STATUS 37)
            set(R_CODEGEN_PROGRAM_ARGUMENTS verified)
        elseif(R_CODEGEN_CASE STREQUAL "core_assume")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_core_assume.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "core_slice_from_raw_parts")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_core_slice_from_raw_parts.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "core_slice_from_raw_parts_in")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_core_slice_from_raw_parts_in.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "core_volatile")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_core_volatile.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "core_adopt_release")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_core_adopt_release.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "core_adopt_nontrivial")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_core_adopt_nontrivial.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER
                "${CMAKE_CURRENT_SOURCE_DIR}/codegen_core_adopt_nontrivial_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "core_adopt_type_glue")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_core_adopt_type_glue.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER
                "${CMAKE_CURRENT_SOURCE_DIR}/codegen_core_adopt_type_glue_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "core_adopt_runtime_values")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_core_adopt_runtime_values.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER
                "${CMAKE_CURRENT_SOURCE_DIR}/codegen_core_adopt_runtime_values_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "core_adopt_named_standard_copy")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_core_adopt_named_standard_copy.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "core_adopt_named_standard_layout")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_core_adopt_named_standard_layout.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "core_adopt_named_standard_move")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_core_adopt_named_standard_move.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "core_adopt_compiler_specialized_move")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_core_adopt_compiler_specialized_move.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "named_standard_move_abi")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_named_move_abi.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER
                "${CMAKE_CURRENT_SOURCE_DIR}/codegen_named_move_abi_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "native_result_abi")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_native_result_abi.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER
                "${CMAKE_CURRENT_SOURCE_DIR}/codegen_native_result_abi_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "standard_outcomes")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_standard_outcomes.r")
            set(R_CODEGEN_SOURCE_2
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_integer_switch.r")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "array_slice")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_array_slice.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "array_operations")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_array_operations.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER
                "${CMAKE_CURRENT_SOURCE_DIR}/codegen_array_operations_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "container_operations" OR
               R_CODEGEN_CASE STREQUAL "async_container_operations")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "std_stream")
            # R-SLIB-STREAM-0001..0004 (M18): the scratch file is the argument; the duplex of
            # standard input and output echoes the input, a reader interface owner writes to
            # standard error.
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_std_stream.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_PROGRAM_ARGUMENTS "${CMAKE_CURRENT_BINARY_DIR}/codegen_std_stream.bin")
            set(R_CODEGEN_PROGRAM_INPUT_FILE
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_std_stream.input")
            set(R_CODEGEN_CHECK_STDOUT TRUE)
            set(R_CODEGEN_EXPECTED_STDOUT "stream input")
            set(R_CODEGEN_EXPECTED_STDERR "text")
        elseif(R_CODEGEN_CASE STREQUAL "std_bufio")
            # R-SLIB-BUFIO-0001..0003 (M18): the scratch file is the argument; standard input
            # holds four lines for the buffered reader of standard input.
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_std_bufio.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_PROGRAM_ARGUMENTS "${CMAKE_CURRENT_BINARY_DIR}/codegen_std_bufio.bin")
            set(R_CODEGEN_PROGRAM_INPUT_FILE
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_std_bufio.input")
        elseif(R_CODEGEN_CASE STREQUAL "std_console")
            # R-SLIB-CONSOLE-0001..0002 (M18): two lines are read one byte at a time, and the
            # rest of standard input stays for another reader.
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_std_console.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_PROGRAM_INPUT_FILE
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_std_console.input")
            set(R_CODEGEN_CHECK_STDOUT TRUE)
            set(R_CODEGEN_EXPECTED_STDOUT "hello console\nalpha|beta|rest\n")
            set(R_CODEGEN_EXPECTED_STDERR "warning 3\n")
        elseif(R_CODEGEN_CASE STREQUAL "field_defaults_failures")
            # R-INIT-0004 (L33): the wrapper fails each allocation of the initializers in turn.
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_field_defaults_failures.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER
                "${CMAKE_CURRENT_SOURCE_DIR}/codegen_field_defaults_failures_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "core_format_failures")
            # R-TYPE-0046 (L32): the wrapper fails each allocation of formatting in turn.
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_core_format_failures.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER
                "${CMAKE_CURRENT_SOURCE_DIR}/codegen_core_format_failures_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "clone_failures")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_clone_failures.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_clone_failures_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "container_ownership_failures")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_container_ownership_failures.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER
                "${CMAKE_CURRENT_SOURCE_DIR}/codegen_container_ownership_failures_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "view_container_failures")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_view_container_failures.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER
                "${CMAKE_CURRENT_SOURCE_DIR}/codegen_view_container_failures_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_array_operations")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_array_operations.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "new_expression")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/new_expression.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "managed_owner_access")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_managed_owner_access.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "shared_owner_clone")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_shared_owner_clone.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE MATCHES "^(async_)?shared_owner_operations$")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_shared_owner_clone_field")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_shared_owner_clone_field.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "shared_owner_clone_sync")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_shared_owner_clone_sync.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_new_expression")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_new_expression.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_scalar")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_scalar.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_enum")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_enum.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_option_path")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_option_path.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "sync_aggregate_destination")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_sync_aggregate_destination.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_short_circuit")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_short_circuit.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_fused_default_own")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_fused_default_own.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_MEASURE_STACK_USAGE TRUE)
            set(R_CODEGEN_STACK_MAX_FRAME_SIZE 4096)
        elseif(R_CODEGEN_CASE STREQUAL "async_sync_call")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_sync_call.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_max_call_borrow")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_max_call_borrow.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_mir_slice")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_mir_slice.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_mir_slice_bounds")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_mir_slice_bounds.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "bounds")
        elseif(R_CODEGEN_CASE STREQUAL "async_array_slice")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_array_slice.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_float_literal")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_float_literal.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_core_assume")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_core_assume.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_core_slice_from_raw_parts")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_core_slice_from_raw_parts.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_core_slice_from_raw_parts_in")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_core_slice_from_raw_parts_in.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_core_volatile")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_core_volatile.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_core_adopt_release")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_core_adopt_release.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_core_adopt_nontrivial")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_core_adopt_nontrivial.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER
                "${CMAKE_CURRENT_SOURCE_DIR}/codegen_async_core_adopt_nontrivial_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_main_args")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_main_args.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_PROGRAM_ARGUMENTS verified)
        elseif(R_CODEGEN_CASE STREQUAL "async_u64")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_u64.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_drop")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_drop.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_standard_outcomes")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_standard_outcomes.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_cancel")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_cancel.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "io_as_error")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_io_as_error.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "fs_as_error")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_fs_as_error.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "fs_path_is_absolute")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_fs_path_is_absolute.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "stdin")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_stdin.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_stdin")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_stdin.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "stdout")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_stdout.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_stderr")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_stderr.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_stdout")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_stdout.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_CHECK_STDOUT TRUE)
            set(R_CODEGEN_EXPECTED_STDOUT "R")
        elseif(R_CODEGEN_CASE STREQUAL "async_flush")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_flush.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_io_read")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_io_read.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_PROGRAM_INPUT_FILE
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_io_read.input")
        elseif(R_CODEGEN_CASE STREQUAL "async_io_read_result")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_io_read_result.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_PROGRAM_INPUT_FILE
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_io_read_result.input")
        elseif(R_CODEGEN_CASE STREQUAL "async_io_read_result_end")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_io_read_result_end.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_PROGRAM_INPUT_FILE
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_io_read_result.empty")
        elseif(R_CODEGEN_CASE STREQUAL "async_io_write_result")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_io_write_result.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_CHECK_STDOUT TRUE)
            set(R_CODEGEN_EXPECTED_STDOUT "W")
        elseif(R_CODEGEN_CASE STREQUAL "async_io_write_shared")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_io_write_shared.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_CHECK_STDOUT TRUE)
            set(R_CODEGEN_EXPECTED_STDOUT "S")
        elseif(R_CODEGEN_CASE STREQUAL "async_io_write_all")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_io_write_all.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_io_close_input")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_io_close_input.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_CHECK_STDOUT TRUE)
            set(R_CODEGEN_EXPECTED_STDOUT "")
        elseif(R_CODEGEN_CASE STREQUAL "async_io_close_output")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_io_close_output.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_CHECK_STDOUT TRUE)
            set(R_CODEGEN_EXPECTED_STDOUT "")
        elseif(R_CODEGEN_CASE STREQUAL "async_fs_read_file")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_fs_read_file.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_PROGRAM_ARGUMENTS
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_fs_read_file.r")
        elseif(R_CODEGEN_CASE STREQUAL "async_fs_open_directory")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_fs_open_directory.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_PROGRAM_ARGUMENTS "${PROJECT_SOURCE_DIR}")
        elseif(R_CODEGEN_CASE STREQUAL "audit_std_fs_sync_start")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audit_std_fs_sync_start.r")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "audit_std_fs_complete")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audit_std_fs_complete.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_CHECK_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/check_fs_complete_program.cmake")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_fs_complete_wrapper.c")
        elseif(R_CODEGEN_CASE STREQUAL "async_fs_file_metadata")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_fs_file_metadata.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_PROGRAM_ARGUMENTS
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_fs_file_metadata.r")
        elseif(R_CODEGEN_CASE STREQUAL "async_fs_create_directory_beneath")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_fs_create_directory_beneath.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_CREATE_DIRECTORY_RELATIVE "created/nested")
        elseif(R_CODEGEN_CASE STREQUAL "async_fs_write_file_atomic_no_replace_beneath")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_fs_write_file_atomic_no_replace_beneath.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_ATOMIC_WRITE_SOURCE
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_fs_read_file.r")
            set(R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_SOURCE
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_fs_open_directory.r")
            set(R_CODEGEN_ATOMIC_WRITE_RELATIVE "written.bin")
        elseif(R_CODEGEN_CASE STREQUAL "async_copy_args")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/async_copy_args.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "sync_async_start_user")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_sync_async_start_user.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER
                "${CMAKE_CURRENT_SOURCE_DIR}/codegen_sync_async_start_user_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "overload_methods_async")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_overload_methods_async.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_overload_methods_async_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "sync_async_start_std")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_sync_async_start_std.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_copy_aggregate")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_copy_aggregate.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "await_move_args")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_await_move_args.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_async_move_args_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "await_generic_errors")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_await_generic_errors.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_async_generic_errors_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "await_cancel_finally")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_await_cancel_finally.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_await_cancel_finally_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "await_calls")
            set(R_CODEGEN_SOURCE_1 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_await_calls.r")
            set(R_CODEGEN_SOURCE_2 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_await_calls_lib.r")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_move_args")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/async_move_args.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_C_WRAPPER
                "${CMAKE_CURRENT_SOURCE_DIR}/codegen_async_move_args_wrapper.c")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_string_views")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_string_views.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_many_args")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_many_args.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "async_large_frame")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_large_frame.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_MEASURE_STACK_USAGE TRUE)
            set(R_CODEGEN_STACK_MAX_FRAME_SIZE 1024)
        elseif(R_CODEGEN_CASE STREQUAL "slice_import")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_slice_api.r")
            set(R_CODEGEN_SOURCE_2
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_slice_main.r")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "range_slice")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_range_slice.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "range_bounds")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_range_bounds.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "bounds")
        elseif(R_CODEGEN_CASE STREQUAL "integer_switch")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_integer_switch.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "checked_conversion")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_checked_conversion.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "checked_matrix")
            set(R_CODEGEN_SOURCE_1 "${R_CODEGEN_CHECKED_MATRIX_SOURCE}")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "core_integer_all" OR
               R_CODEGEN_CASE STREQUAL "core_integer_all_o2")
            set(R_CODEGEN_SOURCE_1 "${R_CODEGEN_CORE_INTEGER_ALL_SOURCE}")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            if(R_CODEGEN_CASE STREQUAL "core_integer_all_o2")
                set(R_CODEGEN_GENERATED_C_OPTIMIZATION "-O2")
            endif()
        elseif(R_CODEGEN_CASE STREQUAL "link_available")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_link_available.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_LINK_MANIFEST
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_link_manifest.json")
        elseif(R_CODEGEN_CASE STREQUAL "async_link_available")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_link_available.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_LINK_MANIFEST
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_link_manifest.json")
        elseif(R_CODEGEN_CASE STREQUAL "library_native_merge")
            # Library R-SLIB-RSRC-0003 (M25): the program's own link manifest and the library's.
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_LINK_MANIFEST "${PROJECT_SOURCE_DIR}/examples/offload/link_manifest.json")
            set(R_CODEGEN_ABI_VERIFY ON)
        elseif(R_CODEGEN_CASE STREQUAL "ffi_wide_declarations")
            # R-LIMIT-0001, R-FFI-0042 (L14-N3): a declaration spelled longer than two
            # kilobytes and a member name longer than sixty-three bytes pass the ABI verifier
            # and the bridge.
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_LINK_MANIFEST
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_ffi_wide_link_manifest.json")
            set(R_CODEGEN_EXTRA_C_SOURCES
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ffi/wide_library.c")
            set(R_CODEGEN_ABI_VERIFY ON)
            set(R_CODEGEN_ABI_INVENTORY ON)
            set(R_CODEGEN_HEADER_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ffi")
        elseif(R_CODEGEN_CASE MATCHES
               "^((async_)?ffi_import|ffi_bridge|(async_)?ffi_types|ffi_records|ffi_userdata|ffi_conformance|ffi_null_ingress|ffi_enum_ingress|ffi_repr_c_(imports|objects)|async_ffi_repr_c|ffi_(enum_result|struct_result|struct_null|callback_struct)_ingress|(async_)?ffi_object_enum_ingress|ffi_never_results|(async_)?ffi_never_returns)$")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            set(R_CODEGEN_LINK_MANIFEST
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_ffi_link_manifest.json")
            set(R_CODEGEN_EXTRA_C_SOURCES
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ffi/probe_library.c")
            set(R_CODEGEN_ABI_VERIFY ON)
            set(R_CODEGEN_HEADER_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ffi")
            if(R_CODEGEN_CASE MATCHES
               "^(ffi_records|ffi_conformance|ffi_enum_ingress|ffi_repr_c_(imports|objects)|async_ffi_repr_c|ffi_(enum_result|struct_result|struct_null|callback_struct)_ingress|(async_)?ffi_object_enum_ingress)$")
                set(R_CODEGEN_ABI_INVENTORY ON)
            endif()
            if(R_CODEGEN_CASE MATCHES
               "^(ffi_(null|enum)_ingress|ffi_(enum_result|struct_result|struct_null|callback_struct)_ingress|(async_)?ffi_object_enum_ingress)$")
                # R-CONF-G009 and R-FFI-0056: null-for-non-null and invalid-enum ingress abort
                # at the C entry or panic where a C result or imported object enters R.
                set(R_CODEGEN_EXPECTED_PANIC "contract_violation")
            endif()
            if(R_CODEGEN_CASE MATCHES "^(async_)?ffi_never_returns$")
                # R-TYPE-0007: a C function declared never that returns panics at the call.
                set(R_CODEGEN_EXPECTED_PANIC "contract_violation")
            endif()
        elseif(R_CODEGEN_CASE MATCHES
               "^audit_(aggregate_slice_field|std_library_schema_calls|float_arithmetic|float_to_integer_cast|mixed_integer_arithmetic|for_statement|aggregate_checked_initializer_cleanup|async_aggregate_checked_initializer_cleanup|move_assignment_replacement_cleanup|async_move_assignment_replacement_cleanup|move_field_assignment_replacement_cleanup|async_move_field_assignment_replacement_cleanup|own_assignment_replacement_cleanup|assignment_close_cases|async_assignment_close_cases|assignment_array_reallocation_close|async_assignment_array_reallocation_close|compound_rhs_overlapping_write|float_compound_assignment|prefix_increment|postfix_increment|async_compound_updates|update_place_evaluated_once)$" OR
               R_CODEGEN_CASE MATCHES
               "^audit_(unary_postfix_precedence|list_returned_node_identity|mixed_error_nonborrow_variant|call_borrow_then_move_own_distinct_origins|call_borrow_then_nested_move_distinct_origin|call_disjoint_field_borrows|call_borrow_then_unreachable_nested_move|aggregate_initializer_borrow_then_move_distinct_origins|aggregate_initializer_unreachable_move_same_origin|aggregate_initializer_shared_borrow_then_read_same_origin|std_bytes_append_distinct_source|std_bytes_copy_disjoint_ranges|std_bytes_fill_read_distinct_origin|std_string_append_distinct_source|std_string_mutation_read_distinct_origin|std_array_reserve_read_distinct_origin|move_self_contained_borrow_after_last_use|nested_aggregate_move_after_last_use|nested_aggregate_disjoint_owner_replacement|copy_borrow_aggregate_field_reassignment|copy_borrow_aggregate_dead_copy_reassignment|nll_future_overwrite_false_rejection|nll_overwrite_before_source_mutation|nll_branch_replaces_borrow|nll_field_replaces_borrow|nll_disjoint_field_replacement|nll_throw_branch_replaces_borrow|nll_break_branch_replaces_borrow|nll_continue_branch_replaces_borrow|nll_throw_else_branch_replaces_borrow|nll_infinite_loop_branch_replaces_borrow|nll_terminating_switch_branch_replaces_borrow|nll_explicit_throw_branches_control|switch_nested_break_false_rejection|switch_nested_break_targets|async_switch_nested_break_targets|return_nested_finally_reinitializes_owner|reinitialize_owner_before_nested_finally_control|finally_transfer_keeps_owner_control|dict_entry_ref_fields)$" OR
               R_CODEGEN_CASE MATCHES
               "^audit_std_(alloc_bytes|bytes_equal|bytes_compare|c_target|math_abs|math_sin|math_surface|string_create|string_len|time_duration|time_duration_seconds)_call$" OR
               R_CODEGEN_CASE STREQUAL "audit_std_error_erasures" OR
               R_CODEGEN_CASE STREQUAL "audit_std_c_handle_access" OR
               R_CODEGEN_CASE STREQUAL "audit_std_c_raw_fn" OR
               R_CODEGEN_CASE STREQUAL "audit_std_c_adopt_handle" OR
               R_CODEGEN_CASE STREQUAL "audit_async_std_c_adopt_handle" OR
               R_CODEGEN_CASE STREQUAL "audit_std_c_strings" OR
               R_CODEGEN_CASE STREQUAL "audit_std_c_thread_attachment" OR
               R_CODEGEN_CASE STREQUAL "audit_std_env_surface" OR
               R_CODEGEN_CASE STREQUAL "audit_std_fs_paths" OR
               R_CODEGEN_CASE STREQUAL "audit_std_format_builder" OR
               R_CODEGEN_CASE STREQUAL "audit_std_format_integer_all" OR
               R_CODEGEN_CASE STREQUAL "audit_std_net_ip" OR
               R_CODEGEN_CASE STREQUAL "audit_std_net_operations" OR
               R_CODEGEN_CASE STREQUAL "audit_std_net_sync_starts" OR
               R_CODEGEN_CASE STREQUAL "audit_std_process_simple" OR
               R_CODEGEN_CASE STREQUAL "audit_std_thread_surface" OR
               R_CODEGEN_CASE STREQUAL "audit_std_string_views" OR
               R_CODEGEN_CASE STREQUAL "audit_std_sync_simple" OR
               R_CODEGEN_CASE STREQUAL "audit_std_time_sleep" OR
               R_CODEGEN_CASE STREQUAL "audit_std_convert_parse_all" OR
               R_CODEGEN_CASE STREQUAL "audit_async_std_convert_parse_all" OR
               R_CODEGEN_CASE STREQUAL "audit_std_math_all_operations" OR
               R_CODEGEN_CASE STREQUAL "audit_async_std_math_all_operations" OR
               R_CODEGEN_CASE STREQUAL "audit_async_std_library_calls")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
            if(R_CODEGEN_CASE STREQUAL "audit_std_c_raw_fn")
                set(R_CODEGEN_C_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/codegen_raw_fn_wrapper.c")
            endif()
        elseif(R_CODEGEN_CASE MATCHES
               "^regression_(implicit_numeric_conversions|condition_numeric_comparison|c_abi_numeric_operators|async_c_abi_numeric_operators)$")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE STREQUAL "bounds")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_bounds.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "bounds")
        elseif(R_CODEGEN_CASE MATCHES "^(async_)?array_index_bounds$")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "bounds")
        elseif(R_CODEGEN_CASE MATCHES
               "^(division_by_zero|modulo_by_zero|u32_division_by_zero|u32_modulo_by_zero)$")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "division_by_zero")
        elseif(R_CODEGEN_CASE MATCHES "^(async_)?float_to_integer_cast_failure$" OR
               R_CODEGEN_CASE STREQUAL "integer_cast_failure")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_integer_cast_failure.r")
            if(R_CODEGEN_CASE MATCHES "float_to_integer_cast_failure$")
                set(R_CODEGEN_SOURCE_1
                    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            endif()
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "invalid_conversion")
        elseif(R_CODEGEN_CASE STREQUAL "invalid_shift")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_invalid_shift.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "invalid_shift")
        elseif(R_CODEGEN_CASE MATCHES
               "^((async_)?panic|async_never_panic|never_objects_panic|main_only_panics)$")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "explicit")
        elseif(R_CODEGEN_CASE MATCHES "^(async_)?(scalar_casts|char_compare)$")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "")
        elseif(R_CODEGEN_CASE MATCHES "^(async_)?(char|enum)_cast_failure$")
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "invalid_conversion")
        else()
            set(R_CODEGEN_SOURCE_1
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_${R_CODEGEN_CASE}.r")
            set(R_CODEGEN_SOURCE_2 "")
            set(R_CODEGEN_EXPECTED_PANIC "integer_overflow")
        endif()
        add_test(
            NAME r_frontend_codegen_${R_CODEGEN_CASE}
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
                -DTARGET_TOOLCHAIN_CHECK=${PROJECT_SOURCE_DIR}/tools/check_target_toolchain.py
                -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
                -DMODULE_MAP=${R_CODEGEN_MODULE_MAP}
                -DENTRY=${R_CODEGEN_ENTRY}
                -DSOURCE_1=${R_CODEGEN_SOURCE_1}
                -DSOURCE_2=${R_CODEGEN_SOURCE_2}
                -DLIBRARY_MAP=${PROJECT_SOURCE_DIR}/library/r/library.map
                -DLINK_MANIFEST=${R_CODEGEN_LINK_MANIFEST}
                -DEXTRA_C_SOURCES=${R_CODEGEN_EXTRA_C_SOURCES}
                -DABI_VERIFY=${R_CODEGEN_ABI_VERIFY}
                -DABI_INVENTORY=${R_CODEGEN_ABI_INVENTORY}
                -DABI_RECORD_TOOL=${PROJECT_SOURCE_DIR}/tools/generate_c_abi_record.py
                -DHEADER_INCLUDE_DIRS=${R_CODEGEN_HEADER_INCLUDE_DIRS}
                -DOUTPUT_C=${CMAKE_CURRENT_BINARY_DIR}/codegen_${R_CODEGEN_CASE}.c
                -DOUTPUT_EXE=${CMAKE_CURRENT_BINARY_DIR}/codegen_${R_CODEGEN_CASE}
                -DHASH_ONLY=${R_CODEGEN_HASH_ONLY}
                -DC_WRAPPER=${R_CODEGEN_C_WRAPPER}
                -DRUNTIME_INCLUDE=${PROJECT_SOURCE_DIR}/runtime/include
                -DRUNTIME_DARWIN_INCLUDE=${PROJECT_SOURCE_DIR}/runtime/darwin/include
                -DR_STD_ALLOC_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/alloc/include
                -DR_STD_ASYNC_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/async/include
                -DR_STD_C_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/c/include
                -DR_STD_CONVERT_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/convert/include
                -DR_STD_ERROR_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/error/include
                -DR_STD_STRING_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/string/include
                -DR_LIBRARY_ROOT=${PROJECT_SOURCE_DIR}/library
                -DR_STD_ALLOC_LIBRARY=$<TARGET_FILE:r_std_alloc>
                -DR_STD_ARC_LIBRARY=$<TARGET_FILE:r_std_arc>
                -DR_STD_ARRAY_LIBRARY=$<TARGET_FILE:r_std_array>
                -DR_STD_LIST_LIBRARY=$<TARGET_FILE:r_std_list>
                -DR_STD_DICT_LIBRARY=$<TARGET_FILE:r_std_dict>
                -DR_STD_BITS_LIBRARY=$<TARGET_FILE:r_std_bits>
                -DR_STD_BYTES_LIBRARY=$<TARGET_FILE:r_std_bytes>
                -DR_STD_SECRET_LIBRARY=$<TARGET_FILE:r_std_secret>
                -DR_STD_RANDOM_LIBRARY=$<TARGET_FILE:r_std_random>
                -DR_STD_SIGNAL_LIBRARY=$<TARGET_FILE:r_std_signal>
                -DR_STD_TEST_LIBRARY=$<TARGET_FILE:r_std_test>
                "-DSTD_NATIVE_INCLUDE_DIRS=${R_STD_NATIVE_INCLUDE_DIRS}"
                "-DSTD_NATIVE_LIBRARIES=${R_STD_NATIVE_LINK_ARCHIVES}"
                -DR_STD_HASH_LIBRARY=$<TARGET_FILE:r_std_hash>
                -DR_STD_MATH_LIBRARY=$<TARGET_FILE:r_std_math>
                -DR_STD_FORMAT_LIBRARY=$<TARGET_FILE:r_std_format>
                -DR_STD_JSON_LIBRARY=$<TARGET_FILE:r_std_json>
                -DR_STD_NET_LIBRARY=$<TARGET_FILE:r_std_net>
                -DR_STD_PROCESS_LIBRARY=$<TARGET_FILE:r_std_process>
                -DRUNTIME_DARWIN_SOCKET_LIBRARY=$<TARGET_FILE:r_runtime_darwin_socket>
                -DRUNTIME_DARWIN_DNS_LIBRARY=$<TARGET_FILE:r_runtime_darwin_dns>
                -DR_STD_STRING_LIBRARY=$<TARGET_FILE:r_std_string>
                -DR_STD_UTF8_LIBRARY=$<TARGET_FILE:r_std_utf8>
                -DR_CORE_LIBRARY=$<TARGET_FILE:r_core>
                -DR_STD_ASYNC_LIBRARY=$<TARGET_FILE:r_std_async>
                -DR_STD_C_LIBRARY=$<TARGET_FILE:r_std_c>
                -DR_STD_CONVERT_LIBRARY=$<TARGET_FILE:r_std_convert>
                -DR_STD_ENV_LIBRARY=$<TARGET_FILE:r_std_env>
                -DR_STD_ERROR_LIBRARY=$<TARGET_FILE:r_std_error>
                -DR_STD_FS_LIBRARY=$<TARGET_FILE:r_std_fs>
                -DR_STD_IO_LIBRARY=$<TARGET_FILE:r_std_io>
                -DR_STD_RC_LIBRARY=$<TARGET_FILE:r_std_rc>
                -DR_STD_SYNC_LIBRARY=$<TARGET_FILE:r_std_sync>
                -DR_STD_THREAD_LIBRARY=$<TARGET_FILE:r_std_thread>
                -DR_STD_TIME_LIBRARY=$<TARGET_FILE:r_std_time>
                -DRUNTIME_HOSTED_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/hosted_entry.c
                -DRUNTIME_ALLOCATOR_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/allocator.c
                -DRUNTIME_ARRAY_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/array.c
                -DRUNTIME_LIST_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/list.c
                -DRUNTIME_DICT_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/dict.c
                -DRUNTIME_ARC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/arc.c
                -DRUNTIME_RC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/rc.c
                -DRUNTIME_DROP_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/drop.c
                -DRUNTIME_OWN_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/own.c
                -DRUNTIME_THREAD_ATTACHMENT_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/thread_attachment.c
                -DRUNTIME_TASK_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/task.c
                -DRUNTIME_STACK_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/stack.c
                -DRUNTIME_DARWIN_IO_LIBRARY=$<TARGET_FILE:r_runtime_darwin_io>
                -DRUNTIME_DARWIN_PROCESS_LIBRARY=$<TARGET_FILE:r_runtime_darwin_process>
                -DRUNTIME_DARWIN_EVENT_LIBRARY=$<TARGET_FILE:r_runtime_darwin_event>
                -DRUNTIME_DARWIN_TIMER_LIBRARY=$<TARGET_FILE:r_runtime_darwin_timer>
                -DRUNTIME_FS_LANE_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_lane.c
                -DRUNTIME_FS_LANE_ASYNC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_lane_async.c
                -DRUNTIME_FS_LANE_SYSCALLS_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_lane_syscalls.c
                -DRUNTIME_FS_SERVICE_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_service.c
                -DRUNTIME_STRING_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/string.c
                -DRUNTIME_UTF8_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/utf8.c
                -DRUNTIME_PANIC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/panic_abort.c
                -DEXPECTED_PANIC=${R_CODEGEN_EXPECTED_PANIC}
                -DEXPECTED_EXIT_STATUS=${R_CODEGEN_EXPECTED_EXIT_STATUS}
                "-DEXPECTED_STDERR=${R_CODEGEN_EXPECTED_STDERR}"
                -DPROGRAM_ARGUMENTS=${R_CODEGEN_PROGRAM_ARGUMENTS}
                -DPROGRAM_INPUT_FILE=${R_CODEGEN_PROGRAM_INPUT_FILE}
                -DCHECK_STDOUT=${R_CODEGEN_CHECK_STDOUT}
                -DEXPECTED_STDOUT=${R_CODEGEN_EXPECTED_STDOUT}
                -DTEST_MODE=${R_CODEGEN_TEST_MODE}
                -DCREATE_DIRECTORY_RELATIVE=${R_CODEGEN_CREATE_DIRECTORY_RELATIVE}
                -DATOMIC_WRITE_SOURCE=${R_CODEGEN_ATOMIC_WRITE_SOURCE}
                -DATOMIC_WRITE_REPLACEMENT_SOURCE=${R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_SOURCE}
                -DATOMIC_WRITE_RELATIVE=${R_CODEGEN_ATOMIC_WRITE_RELATIVE}
                -DMEASURE_GENERATED_STACK_USAGE=${R_CODEGEN_MEASURE_STACK_USAGE}
                -DSTACK_USAGE_MAX_FRAME_SIZE=${R_CODEGEN_STACK_MAX_FRAME_SIZE}
                -DSTACK_USAGE_EXPECT_FRAME_GREATER_THAN=${R_CODEGEN_STACK_EXPECT_FRAME_GREATER_THAN}
                -DENABLE_SANITIZERS=${R_FRONTEND_ENABLE_SANITIZERS}
                -DENABLE_THREAD_SANITIZER=${R_FRONTEND_ENABLE_THREAD_SANITIZER}
                -DGENERATED_C_OPTIMIZATION=${R_CODEGEN_GENERATED_C_OPTIMIZATION}
                -P ${R_CODEGEN_CHECK_SCRIPT}
        )
        if(R_CODEGEN_CASE MATCHES "^async_io_close_(input|output)$" OR
           R_CODEGEN_CASE STREQUAL "async_io_write_shared")
            set_tests_properties(
                r_frontend_codegen_${R_CODEGEN_CASE}
                PROPERTIES TIMEOUT 30
            )
        endif()
        unset(R_CODEGEN_PROGRAM_ARGUMENTS)
        unset(R_CODEGEN_PROGRAM_INPUT_FILE)
        unset(R_CODEGEN_EXPECTED_EXIT_STATUS)
        unset(R_CODEGEN_CREATE_DIRECTORY_RELATIVE)
        unset(R_CODEGEN_ATOMIC_WRITE_SOURCE)
        unset(R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_SOURCE)
        unset(R_CODEGEN_ATOMIC_WRITE_RELATIVE)
        unset(R_CODEGEN_MEASURE_STACK_USAGE)
        unset(R_CODEGEN_STACK_MAX_FRAME_SIZE)
        unset(R_CODEGEN_STACK_EXPECT_FRAME_GREATER_THAN)
        unset(R_CODEGEN_CHECK_STDOUT)
        unset(R_CODEGEN_EXPECTED_STDOUT)
        unset(R_CODEGEN_GENERATED_C_OPTIMIZATION)
        if(R_CODEGEN_CLANG_FORMAT AND NOT R_CODEGEN_CASE MATCHES "_o2$")
            add_test(
                NAME r_frontend_codegen_${R_CODEGEN_CASE}_format
                COMMAND "${CMAKE_COMMAND}"
                    -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                    -DCLANG_FORMAT=${R_CODEGEN_CLANG_FORMAT}
                    -DC_COMPILER=${CMAKE_C_COMPILER}
                    -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
                    -DABI_RECORD_TOOL=${PROJECT_SOURCE_DIR}/tools/generate_c_abi_record.py
                    -DABI_INVENTORY=${R_CODEGEN_ABI_INVENTORY}
                    -DHEADER_INCLUDE_DIRS=${R_CODEGEN_HEADER_INCLUDE_DIRS}
                    -DMODULE_MAP=${R_CODEGEN_MODULE_MAP}
                    -DENTRY=${R_CODEGEN_ENTRY}
                -DSOURCE_1=${R_CODEGEN_SOURCE_1}
                    -DSOURCE_2=${R_CODEGEN_SOURCE_2}
                    -DLIBRARY_MAP=${PROJECT_SOURCE_DIR}/library/r/library.map
                    -DLINK_MANIFEST=${R_CODEGEN_LINK_MANIFEST}
                    -DFORMAT_STYLE=${PROJECT_SOURCE_DIR}/.clang-format
                    -DOUTPUT_C=${CMAKE_CURRENT_BINARY_DIR}/codegen_${R_CODEGEN_CASE}_format.c
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_codegen_format.cmake
            )
        endif()
        unset(R_CODEGEN_LINK_MANIFEST)
        unset(R_CODEGEN_EXTRA_C_SOURCES)
        unset(R_CODEGEN_ABI_VERIFY)
        unset(R_CODEGEN_ABI_INVENTORY)
        unset(R_CODEGEN_HEADER_INCLUDE_DIRS)
    endforeach()
    add_test(
        NAME r_frontend_audit_std_library_calls_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audit_async_std_library_calls.r
            -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_std_library_calls_artifacts.cmake
    )
    set_tests_properties(
        r_frontend_audit_std_library_calls_artifacts
        PROPERTIES LABELS "audit;codegen;link-plan;regression"
    )

    set_tests_properties(
        r_frontend_codegen_audit_aggregate_checked_initializer_cleanup
        r_frontend_codegen_audit_async_aggregate_checked_initializer_cleanup
        r_frontend_codegen_audit_move_assignment_replacement_cleanup
        r_frontend_codegen_audit_async_move_assignment_replacement_cleanup
        r_frontend_codegen_audit_move_field_assignment_replacement_cleanup
        r_frontend_codegen_audit_async_move_field_assignment_replacement_cleanup
        r_frontend_codegen_audit_own_assignment_replacement_cleanup
        r_frontend_codegen_audit_assignment_close_cases
        r_frontend_codegen_audit_async_assignment_close_cases
        r_frontend_codegen_audit_assignment_array_reallocation_close
        r_frontend_codegen_audit_async_assignment_array_reallocation_close
        r_frontend_codegen_audit_update_place_evaluated_once
        r_frontend_codegen_audit_call_borrow_then_move_own_distinct_origins
        r_frontend_codegen_audit_call_borrow_then_nested_move_distinct_origin
        r_frontend_codegen_audit_call_disjoint_field_borrows
        r_frontend_codegen_audit_call_borrow_then_unreachable_nested_move
        r_frontend_codegen_audit_aggregate_initializer_borrow_then_move_distinct_origins
        r_frontend_codegen_audit_aggregate_initializer_unreachable_move_same_origin
        r_frontend_codegen_audit_aggregate_initializer_shared_borrow_then_read_same_origin
        r_frontend_codegen_audit_std_bytes_append_distinct_source
        r_frontend_codegen_audit_std_bytes_copy_disjoint_ranges
        r_frontend_codegen_audit_std_bytes_fill_read_distinct_origin
        r_frontend_codegen_audit_std_string_append_distinct_source
        r_frontend_codegen_audit_std_string_mutation_read_distinct_origin
        r_frontend_codegen_audit_std_array_reserve_read_distinct_origin
        r_frontend_codegen_audit_field_assignment_borrow_provenance_control
        r_frontend_codegen_audit_field_assignment_releases_previous_origin
        r_frontend_codegen_audit_disjoint_internal_field_assignment
        r_frontend_codegen_audit_multi_field_releases_replaced_origin
        r_frontend_codegen_audit_multi_field_disjoint_owner_replacement
        r_frontend_codegen_audit_dict_entry_ref_fields
        PROPERTIES
            LABELS "audit;runtime;safety-regression"
    )

    set_tests_properties(
        r_frontend_codegen_regression_loop_carried_borrow_valid
        r_frontend_codegen_regression_loop_carried_borrow_overwritten_before_drop
        r_frontend_codegen_regression_loop_condition_borrow_ends_before_mutation
        r_frontend_codegen_regression_loop_nested_overwrite_before_drop
        r_frontend_codegen_regression_return_path_disjoint_borrow
        r_frontend_codegen_regression_condition_constant_short_circuit_move
        r_frontend_codegen_regression_finally_path_move_cleanup
        r_frontend_codegen_regression_array_shared_view_from_mut_parameter
        r_frontend_codegen_regression_branch_merged_borrow_unrelated_drop
        r_frontend_codegen_regression_async_main_constant_conditional_startup_args
        r_frontend_codegen_regression_async_main_lazy_catch_startup_args
        r_frontend_codegen_regression_async_main_short_circuit_startup_args
        r_frontend_codegen_regression_short_circuit_move_cleanup
        r_frontend_codegen_regression_generic_error_variant_borrow_plain
        r_frontend_codegen_regression_integer_switch_move_cleanup
        r_frontend_codegen_regression_short_circuit_catch_state
        r_frontend_codegen_regression_while_false_catch_state
        r_frontend_codegen_regression_constant_switch_catch_state
        PROPERTIES LABELS "safety-regression"
    )

    # R-FUNC-0025 (M24): test mode rejects a main of the entry module; M24-1: the link plan of
    # a synchronous program names the libraries of the table operations it calls.
    add_test(
        NAME r_frontend_semantic_regression_test_mode_main
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_test_mode_main.r
            -DLIBRARY_MAP=${PROJECT_SOURCE_DIR}/library/r/library.map
            -DTEST_MODE=ON
            -DEXPECTATION=reject
            "-DREQUIRED_DIAGNOSTIC=R-DIAG-FLOW-001 \\[R-FUNC-0025\\]"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(r_frontend_semantic_regression_test_mode_main
        PROPERTIES LABELS "semantic;tests")
    add_test(
        NAME r_frontend_semantic_regression_link_plan_table_operations
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_link_plan_table_operations.r
            -DEMIT=link-plan
            -DENTRY=test.regression.link_plan_table_operations
            -DEXPECTATION=accept
            "-DREQUIRED_OUTPUT=target=\"r_std_async\".*target=\"r_std_test\"|target=\"r_std_test\".*target=\"r_std_async\""
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(r_frontend_semantic_regression_link_plan_table_operations
        PROPERTIES LABELS "semantic;link-plan")
    # M24-6: a rejected parameter of a generic scoped function adds no diagnostic at its call.
    add_test(
        NAME r_frontend_semantic_regression_rejected_parameter_cascade
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_rejected_parameter_cascade.r
            -DLIBRARY_MAP=${PROJECT_SOURCE_DIR}/library/r/library.map
            -DTEST_MODE=ON
            -DEXPECTATION=reject
            "-DREQUIRED_DIAGNOSTIC=R-DIAG-NAME-003 \\[R-NAME-0009\\]"
            "-DFORBIDDEN_DIAGNOSTIC=R-EXPR-0020|R-BORROW-"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(r_frontend_semantic_regression_rejected_parameter_cascade
        PROPERTIES LABELS "semantic;tests")

    foreach(R_CALL_BORROW_MOVE_CASE IN ITEMS
            audit_call_borrow_then_move_own_same_origin
            audit_call_exclusive_borrow_then_move_own_same_origin
            audit_generic_call_borrow_then_move_own_same_origin
            audit_call_borrow_then_move_rc_same_origin
            audit_call_exclusive_borrow_then_read_same_origin
            audit_call_borrow_then_aggregate_move_same_origin
            audit_call_borrow_then_variant_move_same_origin
            audit_call_borrow_then_new_aggregate_move_same_origin
            audit_aggregate_initializer_borrow_then_move_same_origin
            audit_generic_aggregate_initializer_borrow_then_move_same_origin
            audit_error_initializer_borrow_then_move_same_origin
            audit_aggregate_initializer_borrow_then_conditional_move_same_origin
            audit_aggregate_initializer_borrow_then_nested_move_same_origin
            audit_aggregate_initializer_borrow_then_new_move_same_origin
            audit_aggregate_initializer_exclusive_borrow_then_read_same_origin
            audit_std_bytes_append_self_alias
            audit_async_std_bytes_append_self_alias
            audit_std_bytes_copy_overlapping_ranges
            audit_std_bytes_fill_read_same_origin
            audit_std_string_append_str_self_alias
            audit_async_std_string_append_str_self_alias
            audit_std_string_append_utf8_self_alias
            audit_std_string_reserve_read_same_origin
            audit_std_string_truncate_read_same_origin
            audit_std_array_reserve_read_same_origin
            audit_field_assignment_borrow_provenance_uaf
            audit_field_assignment_slice_provenance_uaf
            audit_nested_field_assignment_borrow_provenance_uaf
            audit_index_assignment_borrow_provenance_uaf
            audit_generic_field_assignment_borrow_provenance_uaf
            audit_error_field_assignment_borrow_provenance_uaf
            audit_async_field_assignment_borrow_provenance_uaf
            audit_throw_field_assignment_borrow_provenance_uaf
            audit_update_field_borrow_provenance_conflict
            audit_multi_field_assignment_borrow_provenance_uaf
            audit_branch_field_assignment_borrow_provenance_uaf
            audit_multi_view_array_reallocation_uaf
            audit_generic_multi_field_assignment_borrow_provenance_uaf
            audit_error_multi_field_assignment_borrow_provenance_uaf
            audit_async_multi_field_assignment_borrow_provenance_uaf)
        add_test(
            NAME r_frontend_semantic_regression_${R_CALL_BORROW_MOVE_CASE}
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${R_CALL_BORROW_MOVE_CASE}.r
                -DEXPECTATION=reject
                -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
        )
        set_tests_properties(
            r_frontend_semantic_regression_${R_CALL_BORROW_MOVE_CASE}
            PROPERTIES LABELS "semantic;ownership;safety-regression"
        )
    endforeach()

    # A conditional Move argument can no longer reach the borrow analysis: the fork itself is
    # rejected before name resolution (R-EXPR-0014).
    add_test(
        NAME r_frontend_semantic_regression_audit_call_borrow_then_conditional_move_same_origin
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audit_call_borrow_then_conditional_move_same_origin.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-FLOW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_semantic_regression_audit_call_borrow_then_conditional_move_same_origin
        PROPERTIES LABELS "semantic;ownership;safety-regression"
    )

    foreach(R_CALL_BORROW_MOVE_CONTROL IN ITEMS
            audit_call_borrow_then_move_own_distinct_origins
            audit_call_borrow_then_nested_move_distinct_origin
            audit_call_disjoint_field_borrows
            audit_call_borrow_then_unreachable_nested_move
            audit_aggregate_initializer_borrow_then_move_distinct_origins
            audit_aggregate_initializer_unreachable_move_same_origin
            audit_aggregate_initializer_shared_borrow_then_read_same_origin
            audit_std_bytes_append_distinct_source
            audit_std_bytes_copy_disjoint_ranges
            audit_std_bytes_fill_read_distinct_origin
            audit_std_string_append_distinct_source
            audit_std_string_mutation_read_distinct_origin
            audit_std_array_reserve_read_distinct_origin
            audit_field_assignment_borrow_provenance_control
            audit_field_assignment_releases_previous_origin
            audit_disjoint_internal_field_assignment
            audit_multi_field_releases_replaced_origin
            audit_multi_field_disjoint_owner_replacement
            audit_alloc_new_error_catch_field_compiler_uaf
            audit_alloc_new_error_parameter_control)
        add_test(
            NAME r_frontend_semantic_regression_${R_CALL_BORROW_MOVE_CONTROL}
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${R_CALL_BORROW_MOVE_CONTROL}.r
                -DEXPECTATION=accept
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
        )
        set_tests_properties(
            r_frontend_semantic_regression_${R_CALL_BORROW_MOVE_CONTROL}
            PROPERTIES LABELS "semantic;ownership;safety-regression"
        )
    endforeach()

    foreach(R_FINALLY_MOVED_REJECTION IN ITEMS
            audit_switch_break_finally_drops_owner
            audit_loop_break_finally_drops_owner
            audit_throw_finally_drops_owner_before_catch)
        add_test(
            NAME r_frontend_semantic_regression_${R_FINALLY_MOVED_REJECTION}
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${R_FINALLY_MOVED_REJECTION}.r
                -DEXPECTATION=reject
                -DREQUIRED_DIAGNOSTIC=R-DIAG-MOVE-002
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
        )
        set_tests_properties(
            r_frontend_semantic_regression_${R_FINALLY_MOVED_REJECTION}
            PROPERTIES LABELS "semantic;ownership;safety-regression;finally"
        )
    endforeach()

    add_test(
        NAME r_frontend_semantic_regression_audit_loop_continue_finally_drops_owner
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audit_loop_continue_finally_drops_owner.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-MOVE-003
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    foreach(R_FINALLY_BORROW_REJECTION IN ITEMS
            audit_nested_finally_drops_borrow_origin
            audit_async_nested_finally_drops_borrow_origin)
        add_test(
            NAME r_frontend_semantic_regression_${R_FINALLY_BORROW_REJECTION}
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${R_FINALLY_BORROW_REJECTION}.r
                -DEXPECTATION=reject
                -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-002
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
        )
        set_tests_properties(
            r_frontend_semantic_regression_${R_FINALLY_BORROW_REJECTION}
            PROPERTIES LABELS "semantic;ownership;safety-regression;finally"
        )
    endforeach()
    add_test(
        NAME r_frontend_semantic_regression_audit_nested_finally_rebinds_borrow_origin
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audit_nested_finally_rebinds_borrow_origin.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_audit_nested_finally_clears_nullable_owner
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audit_nested_finally_clears_nullable_owner.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-003
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_semantic_regression_audit_loop_continue_finally_drops_owner
        r_frontend_semantic_regression_audit_nested_finally_rebinds_borrow_origin
        r_frontend_semantic_regression_audit_nested_finally_clears_nullable_owner
        PROPERTIES LABELS "semantic;ownership;safety-regression;finally"
    )

    add_test(
        NAME r_frontend_semantic_regression_constant_switch_default_fallthrough_reinit
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_constant_switch_default_fallthrough_reinit.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-MOVE-002
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_async_constant_switch_default_fallthrough_reinit
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_async_constant_switch_default_fallthrough_reinit.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-MOVE-002
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_semantic_regression_constant_switch_default_fallthrough_reinit
        r_frontend_semantic_regression_async_constant_switch_default_fallthrough_reinit
        PROPERTIES LABELS "semantic;safety-regression"
    )

    # Library profile gates (R-CONF-G005): each entry is fixture:profile:expectation.
    foreach(R_PROFILE_CASE IN ITEMS
            "freestanding_core_ok:freestanding:accept"
            "freestanding_new:freestanding:reject"
            "freestanding_container:freestanding:reject"
            "freestanding_std_string:freestanding:reject"
            "freestanding_own:freestanding:reject"
            "freestanding_adopt:freestanding:reject"
            "allocation_ok:allocation:accept"
            "allocation_std_string:allocation:reject"
            "hosted_ok:hosted:accept"
            "hosted_async_main:hosted:reject"
            "hosted_std_fs:hosted:reject"
            "hosted_std_thread:hosted:reject"
            "hosted_thread_scope:hosted:reject"
            "hosted_thread_ok:hosted-thread:accept"
            "hosted_thread_async:hosted-thread:reject"
            "hosted_thread_sleep:hosted-thread:reject"
            "native_async_ok:hosted-native-async:accept")
        string(REPLACE ":" ";" R_PROFILE_PARTS "${R_PROFILE_CASE}")
        list(GET R_PROFILE_PARTS 0 R_PROFILE_FIXTURE)
        list(GET R_PROFILE_PARTS 1 R_PROFILE_NAME)
        list(GET R_PROFILE_PARTS 2 R_PROFILE_EXPECTATION)
        add_test(
            NAME r_frontend_profile_${R_PROFILE_FIXTURE}
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/profile_${R_PROFILE_FIXTURE}.r
                -DPROFILE=${R_PROFILE_NAME}
                -DEXPECTATION=${R_PROFILE_EXPECTATION}
                -DREQUIRED_DIAGNOSTIC=R-DIAG-PROFILE-001
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
        )
        set_tests_properties(
            r_frontend_profile_${R_PROFILE_FIXTURE}
            PROPERTIES LABELS "semantic;profile"
        )
    endforeach()

    # extern "C" import slice: fixture:emit:manifest:expectation:diagnostic.
    foreach(R_FFI_CASE IN ITEMS
            "ffi_import_missing_safety:hir:none:reject:R-DIAG-FFI-003"
            "ffi_import_missing_header:hir:none:reject:R-DIAG-FFI-006"
            "ffi_import_bad_kind:hir:none:reject:R-DIAG-LINK-001"
            "ffi_import_unsafe_context:hir:none:reject:R-DIAG-UNSAFE-001"
            "ffi_import_aggregate:hir:none:reject:R-DIAG-FFI-004"
            "ffi_import_reserved_name:hir:none:accept:none"
            "ffi_import_reserved_no_header:hir:none:reject:R-DIAG-FFI-003"
            "ffi_import_definition_conflict:c17:conflict:reject:R-DIAG-FFI-004"
            "codegen_ffi_import:c17:none:reject:R-DIAG-LINK-001"
            "ffi_import_unknown_link:c17:probe:reject:R-DIAG-LINK-001"
            "ffi_import_missing_symbol:c17:probe:reject:R-DIAG-LINK-003"
            "ffi_import_kind_mismatch:c17:probe:reject:R-DIAG-LINK-002"
            "ffi_import_data_symbol:c17:probe:reject:R-DIAG-LINK-003"
            "ffi_import_implicit_ok:c17:implicit:accept:none"
            "codegen_ffi_import:c17:probe:accept:none"
            "ffi_opaque_by_value:hir:none:reject:R-DIAG-FFI-003"
            "ffi_opaque_deref:hir:none:reject:R-DIAG-FFI-003"
            "ffi_opaque_enum_kind:hir:none:reject:R-DIAG-FFI-003"
            "ffi_opaque_typedef_no_header:hir:none:reject:R-DIAG-FFI-003"
            "ffi_constant_bad_initializer:hir:none:reject:R-DIAG-FFI-003"
            "ffi_constant_not_representable:hir:none:reject:R-DIAG-CONST-001"
            "ffi_constant_missing_attribute:hir:none:reject:R-DIAG-FFI-003"
            "ffi_object_safe_access:hir:none:reject:R-DIAG-UNSAFE-001"
            "ffi_object_bad_type:hir:none:reject:R-DIAG-FFI-001"
            "ffi_variadic_no_fixed:hir:none:reject:R-DIAG-SYN-001"
            "ffi_variadic_bad_argument:hir:none:reject:R-DIAG-FFI-002"
            "ffi_object_kind_mismatch:c17:probe:reject:R-DIAG-LINK-003"
            "codegen_ffi_types:c17:probe:accept:none"
            "ffi_record_missing_abi:hir:none:reject:R-DIAG-FFI-004"
            "ffi_record_missing_c_type:hir:none:reject:R-DIAG-FFI-003"
            "ffi_record_union_kind:hir:none:reject:R-DIAG-FFI-003"
            "codegen_ffi_records:c17:probe:reject:R-DIAG-FFI-004"
            "ffi_variadic_definition:hir:none:reject:R-DIAG-SYN-001"
            "ffi_import_unavailable:c17:probe:reject:R-DIAG-LINK-001"
            "ffi_two_provider_collision:hir:none:reject:R-DIAG-FFI-003"
            "ffi_import_r_struct_no_header:hir:none:reject:R-DIAG-FFI-003"
            "ffi_import_r_struct_variadic:hir:none:reject:R-DIAG-FFI-003"
            "ffi_object_r_struct_missing_abi:hir:none:reject:R-DIAG-FFI-004"
            "ffi_object_reserved_no_header:hir:none:reject:R-DIAG-FFI-003")
        string(REPLACE ":" ";" R_FFI_PARTS "${R_FFI_CASE}")
        list(GET R_FFI_PARTS 0 R_FFI_FIXTURE)
        list(GET R_FFI_PARTS 1 R_FFI_EMIT)
        list(GET R_FFI_PARTS 2 R_FFI_MANIFEST_NAME)
        list(GET R_FFI_PARTS 3 R_FFI_EXPECTATION)
        list(GET R_FFI_PARTS 4 R_FFI_DIAGNOSTIC)
        set(R_FFI_MANIFEST "")
        if(R_FFI_MANIFEST_NAME STREQUAL "probe")
            set(R_FFI_MANIFEST
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_ffi_link_manifest.json")
        elseif(R_FFI_MANIFEST_NAME STREQUAL "implicit")
            set(R_FFI_MANIFEST
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ffi_implicit_link_manifest.json")
        elseif(R_FFI_MANIFEST_NAME STREQUAL "conflict")
            set(R_FFI_MANIFEST
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ffi_conflict_link_manifest.json")
        endif()
        if(R_FFI_DIAGNOSTIC STREQUAL "none")
            set(R_FFI_DIAGNOSTIC "")
        endif()
        add_test(
            NAME r_frontend_ffi_${R_FFI_FIXTURE}_${R_FFI_EMIT}_${R_FFI_MANIFEST_NAME}
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${R_FFI_FIXTURE}.r
                -DEMIT=${R_FFI_EMIT}
                -DLINK_MANIFEST=${R_FFI_MANIFEST}
                -DEXPECTATION=${R_FFI_EXPECTATION}
                -DREQUIRED_DIAGNOSTIC=${R_FFI_DIAGNOSTIC}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
        )
        set_tests_properties(
            r_frontend_ffi_${R_FFI_FIXTURE}_${R_FFI_EMIT}_${R_FFI_MANIFEST_NAME}
            PROPERTIES LABELS "semantic;ffi"
        )
    endforeach()

    add_test(NAME r_frontend_overload_contract
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_overloads.py"
            --frontend $<TARGET_FILE:r-front>)
    set_tests_properties(r_frontend_overload_contract PROPERTIES LABELS "semantic;overloads")
    add_test(NAME r_frontend_static_condition_metadata
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_static_conditions.py"
            --front $<TARGET_FILE:r-front>)
    set_tests_properties(r_frontend_static_condition_metadata PROPERTIES LABELS "semantic;generics")
    add_test(NAME r_frontend_const_generic_metadata
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_const_generics.py"
            --front "$<TARGET_FILE:r-front>")
    set_tests_properties(r_frontend_const_generic_metadata PROPERTIES LABELS "semantic;generics")
    add_test(NAME r_frontend_consteval_metadata
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_consteval.py"
            --front "$<TARGET_FILE:r-front>")
    set_tests_properties(r_frontend_consteval_metadata PROPERTIES LABELS "semantic;consteval")
    add_test(NAME r_frontend_closure_mode_metadata
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_closure_modes.py"
            --front "$<TARGET_FILE:r-front>")
    set_tests_properties(r_frontend_closure_mode_metadata PROPERTIES LABELS "semantic;closures")
    add_test(NAME r_frontend_function_item_metadata
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_function_items.py"
            --front "$<TARGET_FILE:r-front>")
    set_tests_properties(r_frontend_function_item_metadata PROPERTIES LABELS "semantic;generics;closures")
    add_test(NAME r_frontend_scoped_task_metadata
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_scoped_contracts.py"
            --front "$<TARGET_FILE:r-front>")
    set_tests_properties(r_frontend_scoped_task_metadata PROPERTIES LABELS "semantic;generics;async")
    add_test(NAME r_frontend_opaque_result_metadata
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_opaque_results.py"
            --front "$<TARGET_FILE:r-front>")
    set_tests_properties(r_frontend_opaque_result_metadata PROPERTIES LABELS "semantic;generics;opaque")
    add_test(NAME r_frontend_trait_contract_metadata
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_trait_contracts.py"
            --front "$<TARGET_FILE:r-front>")
    set_tests_properties(r_frontend_trait_contract_metadata PROPERTIES LABELS "semantic;generics;traits")
    add_test(NAME r_frontend_derived_implementations
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_derive.py"
            --front "$<TARGET_FILE:r-front>"
            --library-map "${PROJECT_SOURCE_DIR}/library/r/library.map")
    set_tests_properties(r_frontend_derived_implementations PROPERTIES LABELS "semantic;traits")
    add_test(NAME r_frontend_function_values
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_function_values.py"
            --front "$<TARGET_FILE:r-front>")
    set_tests_properties(r_frontend_function_values PROPERTIES LABELS "semantic")
    add_test(NAME r_frontend_projection_constraints
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_projection_constraints.py"
            --front "$<TARGET_FILE:r-front>")
    set_tests_properties(r_frontend_projection_constraints PROPERTIES LABELS "semantic;generics;traits")
    add_test(NAME r_library_source_profiles
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_library_profiles.py"
            --front "$<TARGET_FILE:r-front>"
            --library-map "${PROJECT_SOURCE_DIR}/library/r/library.map")
    set_tests_properties(r_library_source_profiles PROPERTIES LABELS "semantic;library;profiles")
    add_test(NAME r_frontend_borrow_component_metadata
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_borrow_components.py"
            --front "$<TARGET_FILE:r-front>")
    set_tests_properties(r_frontend_borrow_component_metadata PROPERTIES LABELS "semantic;generics")
    add_test(NAME r_frontend_borrowed_callable_contract
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_borrowed_callables.py"
            --front "$<TARGET_FILE:r-front>")
    set_tests_properties(r_frontend_borrowed_callable_contract PROPERTIES LABELS "semantic;closures;borrow")
    add_test(NAME r_frontend_resource_contract_metadata
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_resource_contracts.py"
            --front $<TARGET_FILE:r-front>)
    set_tests_properties(r_frontend_resource_contract_metadata PROPERTIES LABELS "semantic;effects")
    add_test(NAME r_standard_method_catalogue
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/generate_standard_methods.py" --check)

    add_test(NAME r_example_catalogue
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
            -DSOURCE_DIRECTORY=${PROJECT_SOURCE_DIR}
            -DBUILD_DIRECTORY=${PROJECT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_example_catalogue.cmake)
    add_test(NAME r_example_syntax_catalogue
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_example_syntax.py"
            --frontend $<TARGET_FILE:r-front> --check --require-complete)
    add_test(NAME r_example_coverage_audit
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_example_coverage.py")

    set_tests_properties(r_frontend_codegen_example_calculator
        PROPERTIES FIXTURES_SETUP r_example_calculator)
    add_test(NAME r_example_calculator_commands
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_calculator_examples.py"
            --executable "${CMAKE_CURRENT_BINARY_DIR}/codegen_example_calculator"
            --stack-header
            "${CMAKE_CURRENT_BINARY_DIR}/codegen_example_calculator.c.stack-usage.h")
    set_tests_properties(r_example_calculator_commands PROPERTIES
        FIXTURES_REQUIRED r_example_calculator TIMEOUT 300 LABELS "examples;math")
    add_test(NAME r_example_calculator_generated
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/generate_calculator.py" --check)

    set_tests_properties(r_frontend_codegen_example_numbers
        PROPERTIES FIXTURES_SETUP r_example_numbers)
    add_test(NAME r_example_numbers_commands
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_numeric_examples.py"
            --executable "${CMAKE_CURRENT_BINARY_DIR}/codegen_example_numbers")
    set_tests_properties(r_example_numbers_commands PROPERTIES
        FIXTURES_REQUIRED r_example_numbers TIMEOUT 300 LABELS "examples;numeric")
    add_test(NAME r_example_numbers_generated
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/generate_numeric_example.py" --check)

    foreach(R_EXAMPLE_APP IN ITEMS preflight jobs tournament c_bridge logbook registers quotes ledger workers netlab json_tool workspace bytepipe dispatch environment process_status runner binary text deflate xml statistics snapshots playlist buffer_compare warehouse clock receipt fanout streams keystore pipeline wordstats tally settings service offload status relay textkit tokens ipc testing tls http realtime assistant notary registry telemetry ingest journal)
        set_tests_properties(r_frontend_codegen_example_${R_EXAMPLE_APP}
            PROPERTIES FIXTURES_SETUP r_example_${R_EXAMPLE_APP})
        add_test(NAME r_example_${R_EXAMPLE_APP}_commands
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_${R_EXAMPLE_APP}_examples.py"
                --executable "${CMAKE_CURRENT_BINARY_DIR}/codegen_example_${R_EXAMPLE_APP}")
        set_tests_properties(r_example_${R_EXAMPLE_APP}_commands PROPERTIES
            FIXTURES_REQUIRED r_example_${R_EXAMPLE_APP} TIMEOUT 300 LABELS "examples")
    endforeach()

    # Library R-SLIB-PG-0001..0012 (M35): the std.postgres tests run against a temporary
    # PostgreSQL cluster when its server programs are found; the program built by
    # r_frontend_codegen_rtest_postgres passes without a server there.
    find_program(R_POSTGRES_PG_CTL NAMES pg_ctl
        HINTS /opt/homebrew/opt/postgresql@17/bin /usr/local/opt/postgresql@17/bin
              /opt/homebrew/opt/postgresql/bin)
    if(R_POSTGRES_PG_CTL)
        get_filename_component(R_POSTGRES_BIN_DIR "${R_POSTGRES_PG_CTL}" DIRECTORY)
        set_tests_properties(r_frontend_codegen_rtest_postgres
            PROPERTIES FIXTURES_SETUP r_rtest_postgres)
        set_tests_properties(r_frontend_codegen_example_orders
            PROPERTIES FIXTURES_SETUP r_example_orders)
        add_test(NAME r_example_orders_commands
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_orders_examples.py"
                --executable "${CMAKE_CURRENT_BINARY_DIR}/codegen_example_orders"
                --bin "${R_POSTGRES_BIN_DIR}")
        set_tests_properties(r_example_orders_commands PROPERTIES
            FIXTURES_REQUIRED r_example_orders TIMEOUT 300 LABELS "examples;postgres")
        add_test(NAME r_postgres_tests
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_postgres_tests.py"
                --executable "${CMAKE_CURRENT_BINARY_DIR}/codegen_rtest_postgres"
                --bin "${R_POSTGRES_BIN_DIR}")
        set_tests_properties(r_postgres_tests PROPERTIES
            FIXTURES_REQUIRED r_rtest_postgres TIMEOUT 600 LABELS "library;postgres")
    else()
        message(STATUS "PostgreSQL server programs not found: r_postgres_tests is not registered")
    endif()

    set_tests_properties(r_frontend_codegen_regex_driver
        PROPERTIES FIXTURES_SETUP r_regex_driver)
    add_test(NAME r_regex_differential
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/regex_differential.py"
            --executable "${CMAKE_CURRENT_BINARY_DIR}/codegen_regex_driver")
    set_tests_properties(r_regex_differential PROPERTIES
        FIXTURES_REQUIRED r_regex_driver TIMEOUT 300 LABELS "regex;differential")
    # M19: std.encoding against the Python standard library.
    set_tests_properties(r_frontend_codegen_encoding_driver
        PROPERTIES FIXTURES_SETUP r_encoding_driver)
    add_test(NAME r_encoding_differential
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/encoding_differential.py"
            --executable "${CMAKE_CURRENT_BINARY_DIR}/codegen_encoding_driver")
    set_tests_properties(r_encoding_differential PROPERTIES
        FIXTURES_REQUIRED r_encoding_driver TIMEOUT 300 LABELS "encoding;differential")
    # M32.6: std.json::schema::<T>() against the values of std.json::marshal.
    set_tests_properties(r_frontend_codegen_json_schema_driver
        PROPERTIES FIXTURES_SETUP r_json_schema_driver)
    add_test(NAME r_json_schema_differential
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/json_schema_differential.py"
            --executable "${CMAKE_CURRENT_BINARY_DIR}/codegen_json_schema_driver")
    set_tests_properties(r_json_schema_differential PROPERTIES
        FIXTURES_REQUIRED r_json_schema_driver TIMEOUT 300 LABELS "json;differential")
    # M20: SHA-256/512 in pieces, HMAC and the seeded generator against Python.
    set_tests_properties(r_frontend_codegen_hash_driver
        PROPERTIES FIXTURES_SETUP r_hash_driver)
    add_test(NAME r_hash_differential
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/hash_differential.py"
            --executable "${CMAKE_CURRENT_BINARY_DIR}/codegen_hash_driver")
    set_tests_properties(r_hash_differential PROPERTIES
        FIXTURES_REQUIRED r_hash_driver TIMEOUT 300 LABELS "hash;differential")
    # M28: std.cbor against the examples of RFC 8949 Appendix A and an independent codec, std.crypto
    # against RFC vectors and the Python cryptography package, std.cose against the COSE examples.
    set_tests_properties(r_frontend_codegen_cbor_driver PROPERTIES FIXTURES_SETUP r_cbor_driver)
    add_test(NAME r_cbor_vectors
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cbor_vectors.py"
            --executable "${CMAKE_CURRENT_BINARY_DIR}/codegen_cbor_driver"
            --vectors "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cbor/rfc8949_appendix_a.json")
    set_tests_properties(r_cbor_vectors PROPERTIES
        FIXTURES_REQUIRED r_cbor_driver TIMEOUT 300 LABELS "cbor;differential")
    set_tests_properties(r_frontend_codegen_crypto_driver PROPERTIES FIXTURES_SETUP r_crypto_driver)
    add_test(NAME r_crypto_vectors
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/crypto_vectors.py"
            --executable "${CMAKE_CURRENT_BINARY_DIR}/codegen_crypto_driver")
    set_tests_properties(r_crypto_vectors PROPERTIES
        FIXTURES_REQUIRED r_crypto_driver TIMEOUT 300 LABELS "crypto;differential")
    set_tests_properties(r_frontend_codegen_cose_driver PROPERTIES FIXTURES_SETUP r_cose_driver)
    # The examples of the IETF COSE working group (public domain, the Unlicense) at a pinned commit.
    include("${PROJECT_SOURCE_DIR}/cmake/RThirdParty.cmake")
    r_third_party_archive(R_COSE_EXAMPLES_ROOT
        NAME cose-examples-53c9d634
        URL "https://codeload.github.com/cose-wg/Examples/tar.gz/53c9d634333bb4f529d78f5980fffa2667ee2c12"
        FILE cose-examples-53c9d634333bb4f529d78f5980fffa2667ee2c12.tar.gz
        HASH "SHA256=91a7e7be17fd805fdd9fbf758a1bf7f55d8ed50d201ffa3cfda2666365d4e365")
    add_test(NAME r_cose_vectors
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cose_vectors.py"
            --executable "${CMAKE_CURRENT_BINARY_DIR}/codegen_cose_driver"
            --vectors "${R_COSE_EXAMPLES_ROOT}/Examples-53c9d634333bb4f529d78f5980fffa2667ee2c12")
    set_tests_properties(r_cose_vectors PROPERTIES
        FIXTURES_REQUIRED r_cose_driver TIMEOUT 300 LABELS "cose;differential")
    # Library R-SLIB-SQLITE-0001..0008 (M29): values, statements, failures and transactions of
    # std.sqlite against the sqlite3 module of Python over the same SQL.
    set_tests_properties(r_frontend_codegen_sqlite_driver PROPERTIES FIXTURES_SETUP r_sqlite_driver)
    add_test(NAME r_sqlite_vectors
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/sqlite_vectors.py"
            --executable "${CMAKE_CURRENT_BINARY_DIR}/codegen_sqlite_driver"
            --header "${R_SQLITE3_HEADER}")
    set_tests_properties(r_sqlite_vectors PROPERTIES
        FIXTURES_REQUIRED r_sqlite_driver TIMEOUT 300 LABELS "sqlite;differential")
    # M21: RFC 3339 and HTTP dates against a reference of the rules and the Python standard library.
    set_tests_properties(r_frontend_codegen_time_driver
        PROPERTIES FIXTURES_SETUP r_time_driver)
    add_test(NAME r_time_differential
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/time_differential.py"
            --executable "${CMAKE_CURRENT_BINARY_DIR}/codegen_time_driver")
    set_tests_properties(r_time_differential PROPERTIES
        FIXTURES_REQUIRED r_time_driver TIMEOUT 300 LABELS "time;differential")
    add_test(NAME r_frontend_regex_requires_hosted
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${PROJECT_SOURCE_DIR}/examples/regex/main.r
            -DLIBRARY_MAP=${PROJECT_SOURCE_DIR}/library/r/library.map
            -DPROFILE=allocation
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-PROFILE-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake)
    if(R_FRONTEND_ENABLE_SANITIZERS)
        set_tests_properties(r_regex_differential r_encoding_differential r_hash_differential
            r_time_differential r_cbor_vectors r_crypto_vectors r_cose_vectors r_sqlite_vectors
            PROPERTIES ENVIRONMENT
            "ASAN_OPTIONS=abort_on_error=1;UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")
    endif()

    # R-OBJ-0012: @deny_panic_alloc and --deny-panic-alloc reject panicking allocation.
    # fixture:option:expectation:diagnostic
    foreach(R_DENY_CASE IN ITEMS
            "deny_panic_alloc_new:off:reject:R-DIAG-ALLOC-001"
            "deny_panic_alloc_arc:off:reject:R-DIAG-ALLOC-001"
            "deny_panic_alloc_rc:off:reject:R-DIAG-ALLOC-001"
            "deny_panic_alloc_try_new:off:accept:none"
            "deny_panic_alloc_try_new:on:accept:none"
            "deny_panic_alloc_option:off:accept:none"
            "deny_panic_alloc_option:on:reject:R-DIAG-ALLOC-001"
            "deny_panic_alloc_misplaced:off:reject:R-DIAG-SYN-002"
            "deny_panic_alloc_duplicate:off:reject:R-DIAG-SYN-002"
            "codegen_deny_panic_alloc:on:accept:none")
        string(REPLACE ":" ";" R_DENY_PARTS "${R_DENY_CASE}")
        list(GET R_DENY_PARTS 0 R_DENY_FIXTURE)
        list(GET R_DENY_PARTS 1 R_DENY_OPTION)
        list(GET R_DENY_PARTS 2 R_DENY_EXPECTATION)
        list(GET R_DENY_PARTS 3 R_DENY_DIAGNOSTIC)
        if(R_DENY_DIAGNOSTIC STREQUAL "none")
            set(R_DENY_DIAGNOSTIC "")
        endif()
        set(R_DENY_FLAG OFF)
        if(R_DENY_OPTION STREQUAL "on")
            set(R_DENY_FLAG ON)
        endif()
        add_test(
            NAME r_frontend_deny_panic_alloc_${R_DENY_FIXTURE}_${R_DENY_OPTION}
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${R_DENY_FIXTURE}.r
                -DEMIT=hir
                -DDENY_PANIC_ALLOC=${R_DENY_FLAG}
                -DEXPECTATION=${R_DENY_EXPECTATION}
                -DREQUIRED_DIAGNOSTIC=${R_DENY_DIAGNOSTIC}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
        )
        set_tests_properties(
            r_frontend_deny_panic_alloc_${R_DENY_FIXTURE}_${R_DENY_OPTION}
            PROPERTIES LABELS "semantic;alloc-policy"
        )
    endforeach()

    # R-FUNC-0004: recursive call chains and recursive ownership have no static stack bound.
    foreach(R_STACK_FIXTURE IN ITEMS
            stack_recursive_direct
            stack_recursive_mutual
            stack_recursive_standard
            stack_recursive_json
            stack_recursive_async
            codegen_recursive)
        add_test(
            NAME r_frontend_stack_${R_STACK_FIXTURE}
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${R_STACK_FIXTURE}.r
                -DEMIT=hir
                -DEXPECTATION=reject
                -DREQUIRED_DIAGNOSTIC=R-DIAG-STACK-001
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
        )
        set_tests_properties(
            r_frontend_stack_${R_STACK_FIXTURE}
            PROPERTIES LABELS "semantic;stack"
        )
    endforeach()

    # Normative diagnostics and accepted forms classified from the former slice notices:
    # R-ERR-0004 explicit panic, R-TYPE-0029 never statements, R-EXPR-0016 cast operands.
    foreach(R_NORMATIVE_CASE IN ITEMS
            "semantic_regex_unhandled:reject:R-DIAG-EFFECT-001"
            "semantic_regex_moved:reject:R-DIAG-MOVE-002"
            "semantic_regex_private:reject:R-DIAG-NAME-001"
            "semantic_regex_forged:reject:R-DIAG-INIT-002"
            "semantic_never_statement:accept:none"
            "semantic_consteval_panic:reject:R-DIAG-CONST-003"
            "semantic_consteval_limit:reject:R-DIAG-LIMIT-001"
            "semantic_consteval_runtime_callee:reject:R-DIAG-CONST-002"
            "semantic_consteval_argument:reject:R-DIAG-CONST-002"
            "semantic_consteval_runtime_panic:accept:none"
            "semantic_consteval_module_scope:reject:R-DIAG-CONST-003"
            "semantic_panic_message_type:reject:R-DIAG-TYPE-001"
            "semantic_cast_operands:reject:R-DIAG-TYPE-001"
            "semantic_methods_accept:accept:none"
            "semantic_method_receiver_type:reject:R-DIAG-TYPE-001"
            "semantic_self_outside_trait:reject:R-DIAG-TYPE-001"
            "semantic_impl_incomplete:reject:R-DIAG-TRAIT-001"
            "semantic_impl_signature:reject:R-DIAG-TRAIT-001"
            "semantic_impl_duplicate:reject:R-DIAG-TRAIT-001"
            "semantic_trait_bound_unsatisfied:reject:R-DIAG-TYPE-001"
            "semantic_method_recursion:reject:R-DIAG-STACK-001"
            "semantic_method_name_field:reject:R-DIAG-NAME-002"
            "semantic_method_call_argument:accept:"
            "semantic_closures_accept:accept:none"
            "semantic_closure_constraint_unsatisfied:reject:R-DIAG-TYPE-001"
            "semantic_closure_moved_capture:reject:R-DIAG-MOVE-002"
            "semantic_closure_borrow_conflict:reject:R-DIAG-BORROW-001"
            "semantic_closure_copy_owner_last_use:accept:none"
            "semantic_closure_copy_owner_lifetime:reject:R-DIAG-BORROW-001"
            "semantic_lambda_in_generic:accept:none"
            "semantic_auto_storage:reject:R-DIAG-INIT-001"
            "semantic_range_for_accept:accept:none"
            "semantic_for_in_type_mismatch:reject:R-DIAG-TYPE-001"
            "semantic_for_in_not_iterable:reject:R-DIAG-TYPE-001"
            "semantic_for_in_move_required:reject:R-DIAG-MOVE-001"
            "semantic_membership_operand:reject:R-DIAG-TYPE-001"
            "semantic_membership_unsupported:reject:R-DIAG-TYPE-001"
            "semantic_impl_associated_missing:reject:R-DIAG-TRAIT-001"
            "semantic_projection_unknown:reject:R-DIAG-TYPE-001"
            "semantic_collections_accept:accept:none"
            "semantic_collection_no_destination:reject:R-DIAG-TYPE-001"
            "semantic_dict_into_array:reject:R-DIAG-TYPE-001"
            "semantic_comprehension_element_mismatch:reject:R-DIAG-TYPE-001"
            "semantic_spread_non_variadic:reject:R-DIAG-TYPE-001"
            "semantic_variadic_generic:reject:R-DIAG-TYPE-001"
            "semantic_variadic_argument_mismatch:reject:R-DIAG-TYPE-001"
            "semantic_variadic_result_borrow:reject:R-DIAG-BORROW-002"
            "semantic_variadic_accept:accept:none"
            "semantic_borrow_fields_accept:accept:none"
            "semantic_borrow_field_escape:reject:R-DIAG-BORROW-002"
            "semantic_borrow_field_conflict:reject:R-DIAG-BORROW-001"
            "semantic_method_borrow_owner_last_use:accept:none"
            "semantic_method_borrow_owner_lifetime:reject:R-DIAG-BORROW-001"
            "semantic_qualified_constraint_unknown:reject:R-DIAG-TYPE-001"
            "semantic_reflection_accept:accept:none"
            "semantic_reflection_not_enum:reject:R-DIAG-TYPE-001"
            "semantic_reflection_tagged_form:reject:R-DIAG-TYPE-001"
            "semantic_reflection_field_index:reject:R-DIAG-CONST-001"
            "semantic_reflection_instantiation:reject:R-DIAG-TYPE-001"
            "semantic_callable_signature_mismatch:reject:R-DIAG-TYPE-001"
            "semantic_qualified_constraint_accept:accept:none"
            "semantic_library_import_accept:accept:none")
        string(REPLACE ":" ";" R_NORMATIVE_PARTS "${R_NORMATIVE_CASE}")
        list(GET R_NORMATIVE_PARTS 0 R_NORMATIVE_FIXTURE)
        list(GET R_NORMATIVE_PARTS 1 R_NORMATIVE_EXPECTATION)
        list(GET R_NORMATIVE_PARTS 2 R_NORMATIVE_DIAGNOSTIC)
        if(R_NORMATIVE_DIAGNOSTIC STREQUAL "none")
            set(R_NORMATIVE_DIAGNOSTIC "")
        endif()
        add_test(
            NAME r_frontend_${R_NORMATIVE_FIXTURE}
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${R_NORMATIVE_FIXTURE}.r
                -DEMIT=hir
                -DLIBRARY_MAP=${PROJECT_SOURCE_DIR}/library/r/library.map
                -DEXPECTATION=${R_NORMATIVE_EXPECTATION}
                -DREQUIRED_DIAGNOSTIC=${R_NORMATIVE_DIAGNOSTIC}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
        )
        set_tests_properties(
            r_frontend_${R_NORMATIVE_FIXTURE}
            PROPERTIES LABELS "semantic;normative"
        )
    endforeach()

    # R-CONF-0005: a freestanding program links only the core runtime and its environment.
    add_test(
        NAME r_frontend_freestanding_program
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
            -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.freestanding.json
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/freestanding_program.r
            -DENVIRONMENT_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/freestanding/environment.c
            -DALLOWED_UNDEFINED_SYMBOLS=${CMAKE_CURRENT_SOURCE_DIR}/freestanding/allowed_undefined_symbols.txt
            -DPROJECT_ROOT=${PROJECT_SOURCE_DIR}
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/freestanding_program
            -DEXPECTED_STDOUT=freestanding\ program\ passed
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_freestanding_program.cmake
    )
    set_tests_properties(
        r_frontend_freestanding_program
        PROPERTIES LABELS "codegen;freestanding"
    )

    # R-SLIB-SECRET-0001: std.secret::buffer is Send and not Sync.
    foreach(R_SECRET_CASE IN ITEMS
            "secret_send:accept:none"
            "secret_not_sync:reject:R-DIAG-ASYNC-001")
        string(REPLACE ":" ";" R_SECRET_PARTS "${R_SECRET_CASE}")
        list(GET R_SECRET_PARTS 0 R_SECRET_FIXTURE)
        list(GET R_SECRET_PARTS 1 R_SECRET_EXPECTATION)
        list(GET R_SECRET_PARTS 2 R_SECRET_DIAGNOSTIC)
        if(R_SECRET_DIAGNOSTIC STREQUAL "none")
            set(R_SECRET_DIAGNOSTIC "")
        endif()
        add_test(
            NAME r_frontend_secret_${R_SECRET_FIXTURE}
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${R_SECRET_FIXTURE}.r
                -DEMIT=hir
                -DEXPECTATION=${R_SECRET_EXPECTATION}
                -DREQUIRED_DIAGNOSTIC=${R_SECRET_DIAGNOSTIC}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
        )
        set_tests_properties(
            r_frontend_secret_${R_SECRET_FIXTURE}
            PROPERTIES LABELS "semantic;secret"
        )
    endforeach()

    # R-FFI-0041 record inventory: fixture:expectation:diagnostic, record generated from the
    # fixture's own headers.
    foreach(R_RECORD_CASE IN ITEMS
            "codegen_ffi_records:accept:none"
            "ffi_record_omitted_member:reject:R-DIAG-FFI-004"
            "ffi_record_wrong_field_type:reject:R-DIAG-FFI-004"
            "ffi_record_enum_missing:reject:R-DIAG-FFI-004"
            "ffi_record_enum_representation:reject:R-DIAG-FFI-004"
            "ffi_record_hidden_member:reject:R-DIAG-FFI-004"
            "codegen_ffi_repr_c_imports:accept:none"
            "codegen_ffi_repr_c_objects:accept:none"
            "ffi_record_r_struct_field_name:reject:R-DIAG-FFI-004"
            "ffi_record_r_struct_hidden_member:reject:R-DIAG-FFI-004"
            "ffi_record_r_struct_shape:reject:R-DIAG-FFI-004"
            "ffi_record_r_enum_field:reject:R-DIAG-FFI-004")
        string(REPLACE ":" ";" R_RECORD_PARTS "${R_RECORD_CASE}")
        list(GET R_RECORD_PARTS 0 R_RECORD_FIXTURE)
        list(GET R_RECORD_PARTS 1 R_RECORD_EXPECTATION)
        list(GET R_RECORD_PARTS 2 R_RECORD_DIAGNOSTIC)
        if(R_RECORD_DIAGNOSTIC STREQUAL "none")
            set(R_RECORD_DIAGNOSTIC "")
        endif()
        add_test(
            NAME r_frontend_abi_record_${R_RECORD_FIXTURE}
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
                -DABI_RECORD_TOOL=${PROJECT_SOURCE_DIR}/tools/generate_c_abi_record.py
                -DABI_INVENTORY=ON
                -DHEADER_INCLUDE_DIRS=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ffi
                -DOUTPUT_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/abi_record_${R_RECORD_FIXTURE}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${R_RECORD_FIXTURE}.r
                -DEMIT=c17
                -DLINK_MANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_ffi_link_manifest.json
                -DEXPECTATION=${R_RECORD_EXPECTATION}
                -DREQUIRED_DIAGNOSTIC=${R_RECORD_DIAGNOSTIC}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
        )
        set_tests_properties(
            r_frontend_abi_record_${R_RECORD_FIXTURE}
            PROPERTIES LABELS "semantic;ffi"
        )
    endforeach()

    # R-FFI-0040 record identity against a static record: fixture:diagnostic:record-file.
    foreach(R_IDENTITY_CASE IN ITEMS
            "ffi_record_wrong_provider:R-DIAG-FFI-004:probe_abi_wrong_provider.json"
            "ffi_record_wrong_target:R-DIAG-FFI-004:probe_abi_wrong_target.json"
            "ffi_record_wrong_options:R-DIAG-FFI-004:probe_abi_wrong_options.json"
            "ffi_record_wrong_compiler:R-DIAG-FFI-004:probe_abi_wrong_compiler.json"
            "ffi_record_stale_header:R-DIAG-FFI-004:probe_abi_stale_header.json")
        string(REPLACE ":" ";" R_IDENTITY_PARTS "${R_IDENTITY_CASE}")
        list(GET R_IDENTITY_PARTS 0 R_IDENTITY_FIXTURE)
        list(GET R_IDENTITY_PARTS 1 R_IDENTITY_DIAGNOSTIC)
        list(GET R_IDENTITY_PARTS 2 R_IDENTITY_RECORD)
        add_test(
            NAME r_frontend_abi_record_${R_IDENTITY_FIXTURE}
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${R_IDENTITY_FIXTURE}.r
                -DEMIT=c17
                -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
                -DLINK_MANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_ffi_link_manifest.json
                -DABI_RECORD=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ffi/${R_IDENTITY_RECORD}
                -DABI_HEADER_DIRS=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ffi
                -DEXPECTATION=reject
                -DREQUIRED_DIAGNOSTIC=${R_IDENTITY_DIAGNOSTIC}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
        )
        set_tests_properties(
            r_frontend_abi_record_${R_IDENTITY_FIXTURE}
            PROPERTIES LABELS "semantic;ffi"
        )
    endforeach()

    # R-FFI-0044: the record document digest and every header digest enter the interface.
    add_test(
        NAME r_frontend_abi_record_interface_fingerprint
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
            -DABI_RECORD_TOOL=${PROJECT_SOURCE_DIR}/tools/generate_c_abi_record.py
            -DABI_INVENTORY=ON
            -DHEADER_INCLUDE_DIRS=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ffi
            -DOUTPUT_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/abi_record_interface_fingerprint
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_ffi_records.r
            -DEMIT=interface
            -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
            -DLINK_MANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_ffi_link_manifest.json
            -DEXPECTATION=accept
            "-DREQUIRED_OUTPUT=\\(abi-record present=true sha256=\"[0-9a-f]+\"\\).*\\(abi-header record=\"probe-abi\" spelling=\"probe_library.h\" sha256=\"[0-9a-f]+\"\\)"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_abi_record_interface_fingerprint
        PROPERTIES LABELS "semantic;ffi"
    )

    # R-FFI-0042 verifier outcome: fixture:expectation:failure-pattern:record, where record is
    # none, generate (from the fixture's headers) or a static fixtures/ffi/*.json file name.
    foreach(R_ABI_CASE IN ITEMS
            "codegen_ffi_import:pass:none:none"
            "codegen_ffi_bridge:pass:none:none"
            "codegen_ffi_types:pass:none:none"
            "codegen_async_ffi_types:pass:none:none"
            "codegen_ffi_records:pass:none:generate"
            "codegen_ffi_conformance:pass:none:generate"
            "codegen_ffi_userdata:pass:none:none"
            "ffi_verify_mismatch:fail:incompatible:none"
            "ffi_verify_constant_mismatch:fail:static.assertion:none"
            "ffi_verify_constant_type_mismatch:fail:static.assertion:none"
            "ffi_verify_object_mismatch:fail:static.assertion:none"
            "ffi_verify_opaque_mismatch:fail:does.not.match:none"
            "ffi_verify_record_claim:fail:static.assertion:probe_abi_claim.json"
            "codegen_ffi_repr_c_imports:pass:none:generate"
            "codegen_ffi_repr_c_objects:pass:none:generate"
            "ffi_verify_r_struct_claim:fail:incompatible:probe_abi_r_struct_claim.json"
            "codegen_ffi_never_results:pass:none:none"
            "ffi_verify_never_mismatch:fail:incompatible:none")
        string(REPLACE ":" ";" R_ABI_PARTS "${R_ABI_CASE}")
        list(GET R_ABI_PARTS 0 R_ABI_FIXTURE)
        list(GET R_ABI_PARTS 1 R_ABI_EXPECTATION)
        list(GET R_ABI_PARTS 2 R_ABI_PATTERN)
        list(GET R_ABI_PARTS 3 R_ABI_RECORD_MODE)
        set(R_ABI_RECORD_FILE "")
        set(R_ABI_INVENTORY OFF)
        if(R_ABI_RECORD_MODE STREQUAL "generate")
            set(R_ABI_INVENTORY ON)
        elseif(NOT R_ABI_RECORD_MODE STREQUAL "none")
            set(R_ABI_RECORD_FILE "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ffi/${R_ABI_RECORD_MODE}")
        endif()
        add_test(
            NAME r_frontend_abi_verifier_${R_ABI_FIXTURE}
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
                -DABI_RECORD_TOOL=${PROJECT_SOURCE_DIR}/tools/generate_c_abi_record.py
                -DABI_INVENTORY=${R_ABI_INVENTORY}
                -DABI_RECORD=${R_ABI_RECORD_FILE}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${R_ABI_FIXTURE}.r
                -DLINK_MANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_ffi_link_manifest.json
                -DHEADER_INCLUDE_DIRS=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ffi
                -DEXPECTATION=${R_ABI_EXPECTATION}
                -DFAIL_PATTERN=${R_ABI_PATTERN}
                -DOUTPUT_C=${CMAKE_CURRENT_BINARY_DIR}/abi_verifier_${R_ABI_FIXTURE}.c
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_abi_verifier.cmake
        )
        set_tests_properties(
            r_frontend_abi_verifier_${R_ABI_FIXTURE}
            PROPERTIES LABELS "semantic;ffi"
        )
    endforeach()

    add_test(
        NAME r_frontend_semantic_regression_implicit_numeric_conversions
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_implicit_numeric_conversions.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_condition_numeric_comparison
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_condition_numeric_comparison.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_c_abi_numeric_operators
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_c_abi_numeric_operators.r
            -DEXPECTATION=accept
            -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_semantic_regression_implicit_numeric_conversions
        r_frontend_semantic_regression_condition_numeric_comparison
        r_frontend_semantic_regression_c_abi_numeric_operators
        PROPERTIES LABELS "semantic;safety-regression"
    )

    add_test(
        NAME r_frontend_semantic_regression_while_false_catch_state
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_while_false_catch_state.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_semantic_regression_while_false_catch_state
        PROPERTIES LABELS "semantic;safety-regression"
    )

    add_test(
        NAME r_frontend_semantic_regression_constant_switch_catch_state
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_constant_switch_catch_state.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_semantic_regression_constant_switch_catch_state
        PROPERTIES LABELS "semantic;safety-regression"
    )

    add_test(
        NAME r_frontend_semantic_regression_async_main_completion_catch_startup_args
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_async_main_completion_catch_startup_args.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-ASYNC-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_async_main_constant_switch_startup_args
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_async_main_constant_switch_startup_args.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_async_main_finally_startup_args
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_async_main_finally_startup_args.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-ASYNC-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_async_main_constant_conditional_startup_args
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_async_main_constant_conditional_startup_args.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_async_main_short_circuit_startup_args
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_async_main_short_circuit_startup_args.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_async_main_lazy_catch_startup_args
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_async_main_lazy_catch_startup_args.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_async_main_selected_conditional_startup_args
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_async_main_selected_conditional_startup_args.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-ASYNC-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_async_main_evaluated_short_circuit_startup_args
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_async_main_evaluated_short_circuit_startup_args.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-ASYNC-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_generic_conditional_borrow_escape
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_generic_conditional_borrow_escape.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-002
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(r_frontend_semantic_regression_generic_conditional_borrow_escape
                         PROPERTIES LABELS "semantic;safety-regression")
    add_test(
        NAME r_frontend_semantic_regression_generic_local_conditional_borrow_escape
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_generic_local_conditional_borrow_escape.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-002
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(r_frontend_semantic_regression_generic_local_conditional_borrow_escape
                         PROPERTIES LABELS "semantic;safety-regression")
    add_test(
        NAME r_frontend_semantic_regression_generic_assigned_conditional_borrow_escape
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_generic_assigned_conditional_borrow_escape.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-002
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(r_frontend_semantic_regression_generic_assigned_conditional_borrow_escape
                         PROPERTIES LABELS "semantic;safety-regression")
    add_test(
        NAME r_frontend_semantic_regression_generic_if_assigned_borrow_escape
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_generic_if_assigned_borrow_escape.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-002
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(r_frontend_semantic_regression_generic_if_assigned_borrow_escape
                         PROPERTIES LABELS "semantic;safety-regression")
    add_test(
        NAME r_frontend_semantic_regression_generic_parameter_call_borrow_escape
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_generic_parameter_call_borrow_escape.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-002
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(r_frontend_semantic_regression_generic_parameter_call_borrow_escape
                         PROPERTIES LABELS "semantic;safety-regression")
    foreach(R_GENERIC_AUTOMATIC_BORROW_ESCAPE IN ITEMS
            direct
            assigned
            conditional
            reborrow
            call
            switch
            option
            conditional_wrapped
            conditional_optional
            branch_optional
            field
            array_element
            array_slice
            call_bounded_array)
        add_test(
            NAME r_frontend_semantic_regression_generic_${R_GENERIC_AUTOMATIC_BORROW_ESCAPE}_automatic_borrow_escape
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_generic_${R_GENERIC_AUTOMATIC_BORROW_ESCAPE}_automatic_borrow_escape.r
                -DEXPECTATION=reject
                -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-002.*R-BORROW-0007
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
        )
        set_tests_properties(
            r_frontend_semantic_regression_generic_${R_GENERIC_AUTOMATIC_BORROW_ESCAPE}_automatic_borrow_escape
            PROPERTIES LABELS "semantic;safety-regression;generic"
        )
    endforeach()
    add_test(
        NAME r_frontend_semantic_regression_generic_switch_binding_automatic_borrow_escape
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_generic_switch_binding_automatic_borrow_escape.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-002.*R-BORROW-0007
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_semantic_regression_generic_switch_binding_automatic_borrow_escape
        PROPERTIES LABELS "semantic;safety-regression;generic"
    )
    add_test(
        NAME r_frontend_semantic_regression_generic_switch_reborrow_automatic_borrow_escape
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_generic_switch_reborrow_automatic_borrow_escape.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-002.*R-BORROW-0007
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_semantic_regression_generic_switch_reborrow_automatic_borrow_escape
        PROPERTIES LABELS "semantic;safety-regression;generic"
    )
    add_test(
        NAME r_frontend_semantic_regression_generic_borrowed_switch_storage_return
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_generic_borrowed_switch_storage_return.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_semantic_regression_generic_borrowed_switch_storage_return
        PROPERTIES LABELS "semantic;safety-regression;generic"
    )
    add_test(
        NAME r_frontend_semantic_regression_generic_constant_dead_automatic_borrow
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_generic_constant_dead_automatic_borrow.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_semantic_regression_generic_constant_dead_automatic_borrow
        PROPERTIES LABELS "semantic;safety-regression;generic"
    )
    add_test(
        NAME r_frontend_semantic_regression_generic_borrowed_storage_return
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_generic_borrowed_storage_return.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_semantic_regression_generic_borrowed_storage_return
        PROPERTIES LABELS "semantic;safety-regression;generic"
    )
    add_test(
        NAME r_frontend_semantic_regression_generic_error_automatic_borrow_escape
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_generic_error_automatic_borrow_escape.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-002.*R-BORROW-0007
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_semantic_regression_generic_error_automatic_borrow_escape
        PROPERTIES LABELS "semantic;safety-regression;generic;checked-errors"
    )
    add_test(
        NAME r_frontend_semantic_regression_generic_error_call_automatic_borrow_escape
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_generic_error_call_automatic_borrow_escape.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-002.*R-BORROW-0007
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_semantic_regression_generic_error_call_automatic_borrow_escape
        PROPERTIES LABELS "semantic;safety-regression;generic;checked-errors"
    )
    add_test(
        NAME r_frontend_semantic_regression_generic_error_variant_automatic_borrow_escape
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_generic_error_variant_automatic_borrow_escape.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-002.*R-BORROW-0007
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_semantic_regression_generic_error_variant_automatic_borrow_escape
        PROPERTIES LABELS "semantic;safety-regression;generic;checked-errors"
    )
    add_test(
        NAME r_frontend_semantic_regression_slice_parameter_index_borrow_escape
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_slice_parameter_index_borrow_escape.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-002
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(r_frontend_semantic_regression_slice_parameter_index_borrow_escape
                         PROPERTIES LABELS "semantic;safety-regression")
    add_test(
        NAME r_frontend_semantic_regression_aggregate_slice_local_escape
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_aggregate_slice_local_escape.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-002
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_aggregate_slice_reinitialized
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_aggregate_slice_reinitialized.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_aggregate_slice_reborrow_after_local_escape
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_aggregate_slice_reborrow_after_local_escape.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-002
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_semantic_regression_aggregate_slice_local_escape
        r_frontend_semantic_regression_aggregate_slice_reborrow_after_local_escape
        r_frontend_semantic_regression_aggregate_slice_reinitialized
        PROPERTIES LABELS "semantic;safety-regression"
    )
    add_test(
        NAME r_frontend_semantic_regression_parameter_field_borrow_escape
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_parameter_field_borrow_escape.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-002
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(r_frontend_semantic_regression_parameter_field_borrow_escape
                         PROPERTIES LABELS "semantic;safety-regression")
    add_test(
        NAME r_frontend_semantic_regression_array_alias_resize_uaf
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_array_alias_resize_uaf.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(r_frontend_semantic_regression_array_alias_resize_uaf
                         PROPERTIES LABELS "semantic;safety-regression")
    add_test(
        NAME r_frontend_semantic_regression_shared_borrow_conflicts_with_exclusive
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_shared_borrow_conflicts_with_exclusive.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(r_frontend_semantic_regression_shared_borrow_conflicts_with_exclusive
                         PROPERTIES LABELS "semantic;safety-regression")
    add_test(
        NAME r_frontend_semantic_regression_saved_exclusive_call_conflict
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_saved_exclusive_call_conflict.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(r_frontend_semantic_regression_saved_exclusive_call_conflict
                         PROPERTIES LABELS "semantic;safety-regression")
    add_test(
        NAME r_frontend_semantic_regression_json_saved_exclusive_mutation_uaf
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_json_saved_exclusive_mutation_uaf.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(r_frontend_semantic_regression_json_saved_exclusive_mutation_uaf
                         PROPERTIES LABELS "semantic;safety-regression")
    add_test(
        NAME r_frontend_semantic_regression_branch_merged_borrow_drop_uaf
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_branch_merged_borrow_drop_uaf.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_branch_merged_json_mutation_uaf
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_branch_merged_json_mutation_uaf.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_semantic_regression_branch_merged_borrow_drop_uaf
        r_frontend_semantic_regression_branch_merged_json_mutation_uaf
        PROPERTIES LABELS "semantic;safety-regression"
    )
    add_test(
        NAME r_frontend_semantic_regression_option_payload_borrow_drop_uaf
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_option_payload_borrow_drop_uaf.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_semantic_regression_option_payload_borrow_drop_uaf
        PROPERTIES
            LABELS "semantic;safety-regression"
    )
    add_test(
        NAME r_frontend_semantic_regression_option_payload_borrow_parameter_return
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_option_payload_borrow_parameter_return.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_semantic_regression_option_payload_borrow_parameter_return
        PROPERTIES
            LABELS "semantic;safety-regression"
    )
    add_test(
        NAME r_frontend_semantic_regression_array_create_internal_error
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_array_create_internal_error.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_semantic_regression_array_create_internal_error
        PROPERTIES
            LABELS "semantic;regression"
    )
    foreach(R_JSON_MUTATION_CASE IN ITEMS
            append_borrow
            insert_borrow
            take_index_uaf)
        add_test(
            NAME r_frontend_semantic_regression_json_saved_exclusive_${R_JSON_MUTATION_CASE}
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_json_saved_exclusive_${R_JSON_MUTATION_CASE}.r
                -DEXPECTATION=reject
                -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
        )
        set_tests_properties(
            r_frontend_semantic_regression_json_saved_exclusive_${R_JSON_MUTATION_CASE}
            PROPERTIES LABELS "semantic;safety-regression")
    endforeach()
    add_test(
        NAME r_frontend_semantic_regression_fixed_array_shared_after_exclusive
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_fixed_array_shared_after_exclusive.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(r_frontend_semantic_regression_fixed_array_shared_after_exclusive
                         PROPERTIES LABELS "semantic;safety-regression")
    add_test(
        NAME r_frontend_semantic_regression_exclusive_reborrow
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_exclusive_reborrow.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(r_frontend_semantic_regression_exclusive_reborrow
                         PROPERTIES LABELS "semantic;safety-regression")
    add_test(
        NAME r_frontend_semantic_regression_async_main_pre_suspend_catch_startup_args
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_async_main_pre_suspend_catch_startup_args.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_async_main_nested_catch_startup_args
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_async_main_nested_catch_startup_args.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_async_main_nested_rethrow_startup_args
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_async_main_nested_rethrow_startup_args.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-ASYNC-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_async_main_start_error_catch_startup_args
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_async_main_start_error_catch_startup_args.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_unreachable_after_throw_catch_state
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_unreachable_after_throw_catch_state.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_unreachable_break_after_continue
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_unreachable_break_after_continue.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_semantic_regression_async_main_completion_catch_startup_args
        r_frontend_semantic_regression_async_main_constant_conditional_startup_args
        r_frontend_semantic_regression_async_main_constant_switch_startup_args
        r_frontend_semantic_regression_async_main_evaluated_short_circuit_startup_args
        r_frontend_semantic_regression_async_main_finally_startup_args
        r_frontend_semantic_regression_async_main_lazy_catch_startup_args
        r_frontend_semantic_regression_async_main_nested_catch_startup_args
        r_frontend_semantic_regression_async_main_nested_rethrow_startup_args
        r_frontend_semantic_regression_async_main_pre_suspend_catch_startup_args
        r_frontend_semantic_regression_async_main_selected_conditional_startup_args
        r_frontend_semantic_regression_async_main_short_circuit_startup_args
        r_frontend_semantic_regression_async_main_start_error_catch_startup_args
        r_frontend_semantic_regression_unreachable_after_throw_catch_state
        r_frontend_semantic_regression_unreachable_break_after_continue
        PROPERTIES
            LABELS "semantic;safety-regression"
    )

    add_test(
        NAME r_frontend_semantic_regression_loop_move
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_loop_move.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-MOVE-003
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_loop_borrow
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_loop_borrow.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_loop_reassigned_borrow_drop_uaf
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_loop_reassigned_borrow_drop_uaf.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_loop_carried_borrow_entry_uaf
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_loop_carried_borrow_entry_uaf.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_loop_condition_carried_borrow_uaf
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_loop_condition_carried_borrow_uaf.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_loop_nested_overwrite_before_drop
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_loop_nested_overwrite_before_drop.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_finally_disjoint_borrow
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_finally_disjoint_borrow.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_return_path_disjoint_borrow
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_return_path_disjoint_borrow.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_catch_disjoint_borrow
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_catch_disjoint_borrow.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_finally_reaching_borrow_drop_uaf
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_finally_reaching_borrow_drop_uaf.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_loop_conditional_overwrite_before_drop_uaf
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_loop_conditional_overwrite_before_drop_uaf.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_return_future_borrow_use_uaf
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_return_future_borrow_use_uaf.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_catch_reaching_borrow_drop_uaf
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_catch_reaching_borrow_drop_uaf.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_loop_continue_reassigned_borrow_drop_uaf
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_loop_continue_reassigned_borrow_drop_uaf.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_loop_reassigned_borrow_owner_lifetime
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_loop_reassigned_borrow_owner_lifetime.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_parenthesized_local_type
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_parenthesized_local_type.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_parenthesized_condition_operand
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_parenthesized_condition_operand.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_split_greater_before_comma
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_split_greater_before_comma.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_condition_constant_short_circuit_move
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_condition_constant_short_circuit_move.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_condition_short_circuit_inconsistent_move
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_condition_short_circuit_inconsistent_move.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-MOVE-003
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_semantic_regression_parenthesized_local_type
        r_frontend_semantic_regression_parenthesized_condition_operand
        r_frontend_semantic_regression_split_greater_before_comma
        PROPERTIES LABELS "parser-regression;semantic"
    )
    add_test(
        NAME r_frontend_semantic_regression_branch_disjoint_borrow
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_branch_disjoint_borrow.r
            -DEXPECTATION=accept
            -DREQUIRED_OUTPUT=standard_call.operation=std[.]array::push
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_array_push_move_value
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_array_push_move_value.r
            -DEXPECTATION=accept
            -DREQUIRED_OUTPUT=standard_call.operation=std[.]array::push
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_semantic_regression_loop_move
        r_frontend_semantic_regression_loop_borrow
        r_frontend_semantic_regression_loop_carried_borrow_entry_uaf
        r_frontend_semantic_regression_loop_continue_reassigned_borrow_drop_uaf
        r_frontend_semantic_regression_loop_reassigned_borrow_drop_uaf
        r_frontend_semantic_regression_loop_reassigned_borrow_owner_lifetime
        r_frontend_semantic_regression_branch_disjoint_borrow
        r_frontend_semantic_regression_array_push_move_value
        PROPERTIES LABELS "semantic;safety-regression"
    )
    set_tests_properties(
        r_frontend_semantic_regression_condition_constant_short_circuit_move
        r_frontend_semantic_regression_condition_short_circuit_inconsistent_move
        PROPERTIES LABELS "semantic;safety-regression"
    )
    set_tests_properties(
        r_frontend_semantic_regression_loop_condition_carried_borrow_uaf
        r_frontend_semantic_regression_loop_nested_overwrite_before_drop
        r_frontend_semantic_regression_finally_disjoint_borrow
        r_frontend_semantic_regression_return_path_disjoint_borrow
        r_frontend_semantic_regression_catch_disjoint_borrow
        r_frontend_semantic_regression_finally_reaching_borrow_drop_uaf
        r_frontend_semantic_regression_loop_conditional_overwrite_before_drop_uaf
        r_frontend_semantic_regression_return_future_borrow_use_uaf
        r_frontend_semantic_regression_catch_reaching_borrow_drop_uaf
        PROPERTIES
            LABELS "semantic;safety-regression"
    )

    add_test(
        NAME r_frontend_semantic_regression_unreachable_break_move
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_unreachable_break_move.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_unreachable_break_infinite
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_unreachable_break_infinite.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_unreachable_loop_borrow_use
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_unreachable_loop_borrow_use.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_conditional_disjoint_borrow
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_conditional_disjoint_borrow.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_semantic_regression_unreachable_break_move
        r_frontend_semantic_regression_unreachable_break_infinite
        r_frontend_semantic_regression_unreachable_loop_borrow_use
        r_frontend_semantic_regression_conditional_disjoint_borrow
        PROPERTIES LABELS "semantic;safety-regression"
    )

    add_test(
        NAME r_frontend_semantic_regression_folded_unreachable_loop_borrow
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_folded_unreachable_loop_borrow.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_switch_disjoint_borrow
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_switch_disjoint_borrow.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_constant_switch_move
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_constant_switch_move.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_unreachable_throw_catch_state
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_unreachable_throw_catch_state.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_ternary_catch_state
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_ternary_catch_state.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_move_reinitialization
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_move_reinitialization.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_assignment_rhs_moves_destination
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_assignment_rhs_moves_destination.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-MOVE-003
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_len_after_move
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_len_after_move.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-MOVE-002
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_len_after_async_move
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_len_after_async_move.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-MOVE-002
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_sync_guard_borrow_after_unlock
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_sync_guard_borrow_after_unlock.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_async_sync_guard_across_await
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_async_sync_guard_across_await.r
            -DEMIT=mir
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-ASYNC-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_safe_mutable_module_access
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_safe_mutable_module_access.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-MEM-001.*R-OBJ-0009
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_multi_origin_mutation
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_multi_origin_mutation.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001.*R-BORROW-0002
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_audit_assignment_array_reallocation_uaf
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audit_assignment_array_reallocation_uaf.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_audit_async_assignment_array_reallocation_uaf
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audit_async_assignment_array_reallocation_uaf.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_assignment_rhs_exclusive_access
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_assignment_rhs_exclusive_access.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_short_circuit_constant_move
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_short_circuit_constant_move.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_integer_switch_disjoint_move
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_integer_switch_disjoint_move.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_short_circuit_catch_state
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_short_circuit_catch_state.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_short_circuit_inconsistent_move
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_short_circuit_inconsistent_move.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-MOVE-003
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_integer_switch_inconsistent_move
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_integer_switch_inconsistent_move.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-MOVE-003
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_integer_switch_fallthrough_move
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_integer_switch_fallthrough_move.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-MOVE-003
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_enum_switch_disjoint_move
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_enum_switch_disjoint_move.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    foreach(R_NULLABLE_REJECTION IN ITEMS
            nullable_dereference_without_proof
            nullable_proof_invalidated_by_assignment
            nullable_proof_lost_at_merge
            nullable_ambiguous_short_circuit)
        add_test(
            NAME r_frontend_semantic_regression_${R_NULLABLE_REJECTION}
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_${R_NULLABLE_REJECTION}.r
                -DEXPECTATION=reject
                -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-003.*R-BORROW-0005
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
        )
        set_tests_properties(
            r_frontend_semantic_regression_${R_NULLABLE_REJECTION}
            PROPERTIES LABELS "semantic;safety-regression;nullable"
        )
    endforeach()
    add_test(
        NAME r_frontend_semantic_regression_null_without_context
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_null_without_context.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-TYPE-001.*R-OBJ-0007
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_raw_const_implicit_conversion
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_raw_const_implicit_conversion.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-TYPE-001.*R-INIT-0002
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_semantic_regression_null_without_context
        r_frontend_semantic_regression_raw_const_implicit_conversion
        PROPERTIES LABELS "semantic;safety-regression;nullable"
    )
    foreach(R_NUMERIC_REJECTION IN ITEMS
            numeric_implicit_narrowing
            numeric_implicit_sign_change)
        add_test(
            NAME r_frontend_semantic_regression_${R_NUMERIC_REJECTION}
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_${R_NUMERIC_REJECTION}.r
                -DEXPECTATION=reject
                -DREQUIRED_DIAGNOSTIC=R-DIAG-TYPE-001.*R-INIT-0002
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
        )
    endforeach()
    add_test(
        NAME r_frontend_semantic_regression_mixed_r_c_abi_numeric
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_mixed_r_c_abi_numeric.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-TYPE-001.*R-EXPR-0024
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_c_bool_update
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_c_bool_update.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-TYPE-001.*R-EXPR-0013
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_c_float_modulo
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_c_float_modulo.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-TYPE-001.*R-EXPR-0025
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_semantic_regression_numeric_implicit_narrowing
        r_frontend_semantic_regression_numeric_implicit_sign_change
        r_frontend_semantic_regression_mixed_r_c_abi_numeric
        r_frontend_semantic_regression_c_bool_update
        r_frontend_semantic_regression_c_float_modulo
        PROPERTIES LABELS "semantic;safety-regression;numeric"
    )
    set_tests_properties(
        r_frontend_semantic_regression_folded_unreachable_loop_borrow
        r_frontend_semantic_regression_switch_disjoint_borrow
        r_frontend_semantic_regression_constant_switch_move
        r_frontend_semantic_regression_unreachable_throw_catch_state
        r_frontend_semantic_regression_ternary_catch_state
        r_frontend_semantic_regression_move_reinitialization
        r_frontend_semantic_regression_assignment_rhs_moves_destination
        r_frontend_semantic_regression_len_after_move
        r_frontend_semantic_regression_len_after_async_move
        r_frontend_semantic_regression_sync_guard_borrow_after_unlock
        r_frontend_semantic_regression_async_sync_guard_across_await
        r_frontend_semantic_regression_safe_mutable_module_access
        r_frontend_semantic_regression_multi_origin_mutation
        r_frontend_semantic_regression_assignment_rhs_exclusive_access
        r_frontend_semantic_audit_assignment_array_reallocation_uaf
        r_frontend_semantic_audit_async_assignment_array_reallocation_uaf
        PROPERTIES
            LABELS "semantic;safety-regression"
    )
    set_tests_properties(
        r_frontend_semantic_regression_short_circuit_constant_move
        r_frontend_semantic_regression_integer_switch_disjoint_move
        r_frontend_semantic_regression_short_circuit_catch_state
        r_frontend_semantic_regression_short_circuit_inconsistent_move
        r_frontend_semantic_regression_integer_switch_inconsistent_move
        r_frontend_semantic_regression_integer_switch_fallthrough_move
        r_frontend_semantic_regression_enum_switch_disjoint_move
        PROPERTIES LABELS "semantic;safety-regression"
    )

    foreach(R_AUDIT_REGRESSION IN ITEMS
            aggregate_slice_field
            std_library_schema_calls
            dict_entry_ref_fields
            std_alloc_bytes_call
            std_bytes_equal_call
            std_bytes_compare_call
            std_c_target_call
            std_math_abs_call
            std_math_sin_call
            std_string_create_call
            std_string_len_call
            std_time_duration_call
            std_time_duration_seconds_call
            float_arithmetic
            float_to_integer_cast
            mixed_integer_arithmetic
            for_statement)
        add_test(
            NAME r_frontend_audit_${R_AUDIT_REGRESSION}
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audit_${R_AUDIT_REGRESSION}.r
                -DEXPECTATION=accept
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
        )
        set_tests_properties(
            r_frontend_audit_${R_AUDIT_REGRESSION}
            PROPERTIES
                LABELS "audit;semantic;regression"
        )
    endforeach()

    add_test(
        NAME r_frontend_audit_regression_update_noninteger
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_update_noninteger.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-TYPE-001.*R-EXPR-0013
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_audit_regression_update_value_context
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_update_value_context.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-TYPE-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_audit_regression_update_const
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_update_const.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-TYPE-001.*R-TYPE-0008
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_audit_regression_float_invalid_compound
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_float_invalid_compound.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-TYPE-001.*R-EXPR-0013
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_audit_regression_float_compound_narrowing
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_float_compound_narrowing.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-TYPE-001.*R-EXPR-0013
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )

    add_test(NAME r_frontend_regression_time_opaque_duration_field
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_time_opaque_duration_field.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-TYPE-001.*R-AGG-0002
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake)
    add_test(NAME r_frontend_regression_process_outcome_missing_variant
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_process_outcome_missing_variant.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-SWITCH-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake)
    add_test(NAME r_frontend_regression_thread_outcome_missing_variant
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_thread_outcome_missing_variant.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-SWITCH-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake)
    add_test(NAME r_frontend_regression_thread_void_returned_variant
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_thread_void_returned_variant.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-TYPE-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake)
    add_test(NAME r_frontend_regression_thread_outcome_non_send
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_thread_outcome_non_send.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-ASYNC-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake)
    add_test(NAME r_frontend_regression_string_outcome_missing_variant
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_string_outcome_missing_variant.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-SWITCH-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake)

    add_test(NAME r_frontend_regression_list_lookup_live_alias
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_list_lookup_live_alias.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001.*R-LIB-0023
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake)
    add_test(NAME r_frontend_regression_list_lookup_wrong_origin
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_list_lookup_wrong_origin.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001.*R-LIB-0022
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake)
    add_test(
        NAME r_frontend_audit_regression_list_remove_live_shared_alias
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_list_remove_live_shared_alias.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001.*R-LIB-0023
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_audit_regression_list_remove_after_shared_alias_last_use
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_list_remove_after_shared_alias_last_use.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_audit_regression_list_remove_wrong_origin_rule
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_list_remove_wrong_origin_rule.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-LIB-0022
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_audit_regression_list_remove_different_node_alias
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_list_remove_different_node_alias.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_audit_regression_sync_main_throws
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_sync_main_throws.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-FLOW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_audit_regression_async_main_throws
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_async_main_throws.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-FLOW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_audit_regression_throw_local_borrow_payload
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_throw_local_borrow_payload.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-002
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_audit_regression_throw_parameter_borrow_payload
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_throw_parameter_borrow_payload.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_audit_regression_throw_catch_borrow_outer_local
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_throw_catch_borrow_outer_local.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_audit_regression_aggregate_borrow_owner_lifetime
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_aggregate_borrow_owner_lifetime.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_audit_regression_list_remove_live_shared_alias
        r_frontend_audit_regression_list_remove_after_shared_alias_last_use
        r_frontend_audit_regression_list_remove_different_node_alias
        r_frontend_audit_regression_sync_main_throws
        r_frontend_audit_regression_async_main_throws
        r_frontend_audit_regression_throw_local_borrow_payload
        r_frontend_audit_regression_throw_parameter_borrow_payload
        r_frontend_audit_regression_throw_catch_borrow_outer_local
        r_frontend_audit_regression_aggregate_borrow_owner_lifetime
        PROPERTIES LABELS "audit;semantic;safety-regression"
    )
    set_tests_properties(
        r_frontend_audit_regression_list_remove_wrong_origin_rule
        PROPERTIES LABELS "audit;semantic;regression"
    )

    add_test(
        NAME r_frontend_audit_regression_dict_mutation_borrow_rule
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_dict_mutation_borrow_rule.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001.*R-LIB-0021
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_audit_regression_dict_mutation_borrow_rule
        PROPERTIES LABELS "audit;semantic;regression"
    )

    add_test(
        NAME r_frontend_audit_unary_postfix_precedence
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audit_unary_postfix_precedence.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_audit_list_returned_node_identity
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audit_list_returned_node_identity.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_audit_list_conditional_same_referent
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_list_conditional_same_referent.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_audit_mixed_error_nonborrow_variant
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audit_mixed_error_nonborrow_variant.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    foreach(R_SELF_BORROW_REGRESSION IN ITEMS
            move_self_contained_borrow_uaf
            generic_move_self_contained_borrow_uaf
            nested_aggregate_move_self_contained_borrow_uaf
            field_move_self_contained_borrow_uaf
            assign_move_self_contained_borrow_uaf
            conditional_move_self_contained_borrow_uaf
            borrowed_view_field_storage_reassignment
            nll_partial_branch_replacement_uaf
            nll_assignment_rhs_reads_old_borrow
            nll_whole_aggregate_keeps_other_borrow_uaf)
        add_test(
            NAME r_frontend_audit_${R_SELF_BORROW_REGRESSION}
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audit_${R_SELF_BORROW_REGRESSION}.r
                -DEXPECTATION=reject
                -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
        )
        set_tests_properties(
            r_frontend_audit_${R_SELF_BORROW_REGRESSION}
            PROPERTIES LABELS "audit;semantic;safety-regression"
        )
    endforeach()
    add_test(
        NAME r_frontend_audit_aggregate_borrow_drop_uaf
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_aggregate_borrow_drop_uaf.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_audit_catch_call_borrow_drop_uaf
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_catch_call_borrow_drop_uaf.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_audit_regression_generic_error_borrow_argument
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_generic_error_borrow_argument.r
            -DEXPECTATION=accept
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_audit_regression_generic_error_borrow_argument
        PROPERTIES LABELS "audit;semantic;safety-regression"
    )
    add_test(
        NAME r_frontend_audit_regression_generic_error_borrow_owner_lifetime
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_generic_error_borrow_owner_lifetime.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    set_tests_properties(
        r_frontend_audit_regression_generic_error_borrow_owner_lifetime
        PROPERTIES LABELS "audit;semantic;safety-regression"
    )
    add_test(
        NAME r_frontend_semantic_regression_payload_variant_borrow_owner_lifetime
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_payload_variant_borrow_owner_lifetime.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_semantic_regression_generic_error_variant_borrow_owner_lifetime
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/regression_generic_error_variant_borrow_owner_lifetime.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    foreach(R_NLL_TRANSFER_CONTROL IN ITEMS
            throw_branch_finally_uses_old_borrow
            break_target_uses_old_borrow
            continue_backedge_uses_old_borrow)
        add_test(
            NAME r_frontend_semantic_regression_nll_${R_NLL_TRANSFER_CONTROL}
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audit_nll_${R_NLL_TRANSFER_CONTROL}.r
                -DEXPECTATION=reject
                -DREQUIRED_DIAGNOSTIC=R-DIAG-BORROW-001
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
        )
    endforeach()
    add_test(
        NAME r_frontend_semantic_regression_switch_break_escaping_finally
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audit_switch_break_escaping_finally.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=R-DIAG-EFFECT-001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
    )
    add_test(
        NAME r_frontend_parser_regression_nesting_performance
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/check_parser_nesting_performance.py"
            --compiler $<TARGET_FILE:r-front>
            --depth 32000
            --timeout 3
    )
    set_tests_properties(
        r_frontend_semantic_regression_payload_variant_borrow_owner_lifetime
        r_frontend_semantic_regression_generic_error_variant_borrow_owner_lifetime
        r_frontend_semantic_regression_nll_throw_branch_finally_uses_old_borrow
        r_frontend_semantic_regression_nll_break_target_uses_old_borrow
        r_frontend_semantic_regression_nll_continue_backedge_uses_old_borrow
        r_frontend_semantic_regression_switch_break_escaping_finally
        r_frontend_parser_regression_nesting_performance
        PROPERTIES LABELS "audit;regression"
    )
    set_tests_properties(
        r_frontend_semantic_regression_payload_variant_borrow_owner_lifetime
        r_frontend_semantic_regression_generic_error_variant_borrow_owner_lifetime
        r_frontend_semantic_regression_nll_throw_branch_finally_uses_old_borrow
        r_frontend_semantic_regression_nll_break_target_uses_old_borrow
        r_frontend_semantic_regression_nll_continue_backedge_uses_old_borrow
        r_frontend_semantic_regression_switch_break_escaping_finally
        PROPERTIES LABELS "audit;semantic;safety-regression"
    )
    set_tests_properties(
        r_frontend_audit_unary_postfix_precedence
        r_frontend_audit_list_returned_node_identity
        r_frontend_audit_list_conditional_same_referent
        r_frontend_audit_mixed_error_nonborrow_variant
        r_frontend_audit_aggregate_borrow_drop_uaf
        r_frontend_audit_catch_call_borrow_drop_uaf
        PROPERTIES LABELS "audit;semantic;safety-regression"
    )

    foreach(R_ATOMIC_ORDER_REGRESSION IN ITEMS
            load_release_order
            store_acquire_order
            compare_exchange_failure_release
            compare_exchange_failure_stronger)
        add_test(
            NAME r_frontend_semantic_atomic_${R_ATOMIC_ORDER_REGRESSION}
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_atomic_${R_ATOMIC_ORDER_REGRESSION}.r
                -DEXPECTATION=reject
                -DREQUIRED_DIAGNOSTIC=R-DIAG-ATOMIC-001.*R-MEM-0007
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake
        )
        set_tests_properties(
            r_frontend_semantic_atomic_${R_ATOMIC_ORDER_REGRESSION}
            PROPERTIES LABELS "atomic;semantic;regression"
        )
    endforeach()

    add_test(
        NAME r_frontend_checked_effect_diagnostics
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DCASE_COUNT=21
            -DSOURCE_1=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_finally_return_escape.r
            -DRULE_1=R-ERR-0003
            -DSOURCE_2=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_finally_break_escape.r
            -DRULE_2=R-ERR-0003
            -DSOURCE_3=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_finally_continue_escape.r
            -DRULE_3=R-ERR-0003
            -DSOURCE_4=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_finally_throw_escape.r
            -DRULE_4=R-ERR-0003
            -DSOURCE_5=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_finally_uncaught_call.r
            -DRULE_5=R-ERR-0003
            -DSOURCE_6=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_finally_await.r
            -DRULE_6=R-ERR-0003
            -DSOURCE_7=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_rethrow_outside_catch.r
            -DRULE_7=R-ERR-0002
            -DSOURCE_8=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_rethrow_after_move.r
            -DRULE_8=R-ERR-0002
            -DSOURCE_9=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_checked_duplicate_throws.r
            -DRULE_9=R-TYPE-0012
            -DSOURCE_10=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_checked_unhandled_effect.r
            -DRULE_10=R-ERR-0001
            -DSOURCE_11=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_finally_try_local_in_finally.r
            -DRULE_11=R-NAME-0003
            -DSOURCE_12=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_finally_catch_local_in_finally.r
            -DRULE_12=R-NAME-0003
            -DSOURCE_13=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_try_local_after.r
            -DRULE_13=R-NAME-0003
            -DSOURCE_14=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_catch_local_after.r
            -DRULE_14=R-NAME-0003
            -DSOURCE_15=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_thread_unresolved_handle.r
            -DRULE_15=R-DIAG-MEM-001
            -DSOURCE_16=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_thread_scoped_detach.r
            -DRULE_16=R-DIAG-TYPE-001
            -DSOURCE_17=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_thread_scoped_unresolved_handle.r
            -DRULE_17=R-DIAG-MEM-001
            -DSOURCE_18=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_rethrow_after_drop.r
            -DRULE_18=R-ERR-0002
            -DSOURCE_19=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_checked_duplicate_catch.r
            -DRULE_19=R-ERR-0003
            -DSOURCE_20=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_throw_named_move_without_move.r
            -DRULE_20=R-OWN-0003
            -DSOURCE_21=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_throw_ambiguous_aggregate.r
            -DRULE_21=R-ERR-0002
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_checked_effect_diagnostics.cmake
    )

    add_test(
        NAME r_frontend_thread_checked_handle_resolution
        COMMAND $<TARGET_FILE:r-front>
            --emit=hir
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_thread_handle_resolution.r
    )

    add_test(
        NAME r_frontend_checked_effect_set_permutation
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DALPHA_FIRST_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/interface_checked_effect_order_alpha_first.r
            -DBRAVO_FIRST_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/interface_checked_effect_order_bravo_first.r
            -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
            -DOUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/checked_effect_set_permutation
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_checked_effect_set_permutation.cmake
    )

    add_test(
        NAME r_frontend_array_codegen_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSYNC_SOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_array_operations.r
            -DASYNC_SOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_array_operations.r
            -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_array_codegen_artifacts.cmake
    )

    add_test(
        NAME r_frontend_sync_checked_retry_codegen_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSYNC_SOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_sync_checked_retry.r
            -DASYNC_SOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_sync_checked_retry.r
            -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_sync_checked_retry_codegen_artifacts.cmake
    )
    add_test(
        NAME r_frontend_shared_finally_codegen_artifacts
        COMMAND
            ${CMAKE_COMMAND}
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_checked_finally_control.r
            -DEFFECT_CLEANUP_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_effect_cleanup.r
            -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_shared_finally_codegen_artifacts.cmake
    )
    add_test(
        NAME r_frontend_async_shared_finally_codegen_artifacts
        COMMAND
            ${CMAKE_COMMAND}
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_shared_finally.r
            -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_async_shared_finally_artifacts.cmake
    )

    add_test(
        NAME r_frontend_array_slice_codegen_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_array_slice.r
            -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_array_slice_codegen_artifacts.cmake
    )

    add_test(
        NAME r_frontend_bytes_codegen_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_bytes_operations.r
            -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_bytes_codegen_artifacts.cmake
    )

    add_test(
        NAME r_frontend_async_enum_codegen_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_enum.r
            -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_async_enum_codegen_artifacts.cmake
    )

    add_test(
        NAME r_frontend_async_option_path_codegen_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_option_path.r
            -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_async_option_path_codegen_artifacts.cmake
    )

    add_test(
        NAME r_frontend_sync_aggregate_destination_codegen_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_sync_aggregate_destination.r
            -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DRUNTIME_INCLUDE=${PROJECT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_sync_aggregate_destination_codegen_artifacts.cmake
    )

    add_test(
        NAME r_frontend_async_short_circuit_codegen_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_short_circuit.r
            -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_async_short_circuit_codegen_artifacts.cmake
    )

    add_test(
        NAME r_frontend_async_fused_default_own_codegen_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_fused_default_own.r
            -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_async_fused_default_own_codegen_artifacts.cmake
    )

    add_test(
        NAME r_frontend_async_sync_call_codegen_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_sync_call.r
            -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_async_sync_call_codegen_artifacts.cmake
    )

    add_test(
        NAME r_frontend_async_mir_slice_codegen_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_mir_slice.r
            -DARRAY_SOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_array_slice.r
            -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_async_mir_slice_codegen_artifacts.cmake
    )

    add_test(
        NAME r_frontend_codegen_unzip_pure
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
            -DTARGET_TOOLCHAIN_CHECK=${PROJECT_SOURCE_DIR}/tools/check_target_toolchain.py
            -DMODULE_MAP=${PROJECT_SOURCE_DIR}/examples/unzip/modules.map
            -DENTRY=example.unzip.tests.main::main
            -DPROFILE=hosted-native-async
            -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
            -DOUTPUT_C=${CMAKE_CURRENT_BINARY_DIR}/codegen_unzip_pure.c
            -DOUTPUT_EXE=${CMAKE_CURRENT_BINARY_DIR}/codegen_unzip_pure
            -DRUNTIME_INCLUDE=${PROJECT_SOURCE_DIR}/runtime/include
            -DRUNTIME_DARWIN_INCLUDE=${PROJECT_SOURCE_DIR}/runtime/darwin/include
            -DR_STD_ALLOC_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/alloc/include
            -DR_STD_ASYNC_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/async/include
            -DR_STD_C_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/c/include
            -DR_STD_CONVERT_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/convert/include
            -DR_STD_ERROR_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/error/include
            -DR_STD_STRING_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/string/include
            -DR_LIBRARY_ROOT=${PROJECT_SOURCE_DIR}/library
            -DR_STD_ARC_LIBRARY=$<TARGET_FILE:r_std_arc>
            -DR_STD_ALLOC_LIBRARY=$<TARGET_FILE:r_std_alloc>
            -DR_STD_ARRAY_LIBRARY=$<TARGET_FILE:r_std_array>
            -DR_STD_LIST_LIBRARY=$<TARGET_FILE:r_std_list>
            -DR_STD_DICT_LIBRARY=$<TARGET_FILE:r_std_dict>
            -DR_STD_BITS_LIBRARY=$<TARGET_FILE:r_std_bits>
            -DR_STD_BYTES_LIBRARY=$<TARGET_FILE:r_std_bytes>
            -DR_STD_SECRET_LIBRARY=$<TARGET_FILE:r_std_secret>
            -DR_STD_RANDOM_LIBRARY=$<TARGET_FILE:r_std_random>
            -DR_STD_SIGNAL_LIBRARY=$<TARGET_FILE:r_std_signal>
            -DR_STD_TEST_LIBRARY=$<TARGET_FILE:r_std_test>
            "-DSTD_NATIVE_INCLUDE_DIRS=${R_STD_NATIVE_INCLUDE_DIRS}"
            "-DSTD_NATIVE_LIBRARIES=${R_STD_NATIVE_LINK_ARCHIVES}"
            -DR_STD_HASH_LIBRARY=$<TARGET_FILE:r_std_hash>
            -DR_STD_ERROR_LIBRARY=$<TARGET_FILE:r_std_error>
                -DR_STD_FORMAT_LIBRARY=$<TARGET_FILE:r_std_format>
                -DR_STD_JSON_LIBRARY=$<TARGET_FILE:r_std_json>
                -DR_STD_NET_LIBRARY=$<TARGET_FILE:r_std_net>
                -DR_STD_PROCESS_LIBRARY=$<TARGET_FILE:r_std_process>
                -DRUNTIME_DARWIN_SOCKET_LIBRARY=$<TARGET_FILE:r_runtime_darwin_socket>
                -DRUNTIME_DARWIN_DNS_LIBRARY=$<TARGET_FILE:r_runtime_darwin_dns>
                -DR_STD_STRING_LIBRARY=$<TARGET_FILE:r_std_string>
            -DR_STD_UTF8_LIBRARY=$<TARGET_FILE:r_std_utf8>
            -DR_CORE_LIBRARY=$<TARGET_FILE:r_core>
            -DR_STD_ASYNC_LIBRARY=$<TARGET_FILE:r_std_async>
            -DR_STD_C_LIBRARY=$<TARGET_FILE:r_std_c>
            -DR_STD_CONVERT_LIBRARY=$<TARGET_FILE:r_std_convert>
            -DR_STD_FS_LIBRARY=$<TARGET_FILE:r_std_fs>
            -DR_STD_IO_LIBRARY=$<TARGET_FILE:r_std_io>
            -DR_STD_RC_LIBRARY=$<TARGET_FILE:r_std_rc>
            -DR_STD_SYNC_LIBRARY=$<TARGET_FILE:r_std_sync>
            -DR_STD_THREAD_LIBRARY=$<TARGET_FILE:r_std_thread>
            -DR_STD_TIME_LIBRARY=$<TARGET_FILE:r_std_time>
            -DRUNTIME_HOSTED_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/hosted_entry.c
            -DRUNTIME_ALLOCATOR_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/allocator.c
            -DRUNTIME_ARRAY_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/array.c
            -DRUNTIME_LIST_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/list.c
            -DRUNTIME_DICT_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/dict.c
            -DRUNTIME_ARC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/arc.c
            -DRUNTIME_RC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/rc.c
            -DRUNTIME_DROP_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/drop.c
            -DRUNTIME_OWN_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/own.c
            -DRUNTIME_THREAD_ATTACHMENT_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/thread_attachment.c
            -DRUNTIME_TASK_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/task.c
            -DRUNTIME_STACK_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/stack.c
            -DRUNTIME_DARWIN_IO_LIBRARY=$<TARGET_FILE:r_runtime_darwin_io>
            -DRUNTIME_DARWIN_PROCESS_LIBRARY=$<TARGET_FILE:r_runtime_darwin_process>
            -DRUNTIME_DARWIN_EVENT_LIBRARY=$<TARGET_FILE:r_runtime_darwin_event>
            -DRUNTIME_DARWIN_TIMER_LIBRARY=$<TARGET_FILE:r_runtime_darwin_timer>
            -DRUNTIME_FS_LANE_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_lane.c
            -DRUNTIME_FS_LANE_ASYNC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_lane_async.c
            -DRUNTIME_FS_LANE_SYSCALLS_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_lane_syscalls.c
            -DRUNTIME_FS_SERVICE_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_service.c
            -DRUNTIME_STRING_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/string.c
            -DRUNTIME_UTF8_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/utf8.c
            -DRUNTIME_PANIC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/panic_abort.c
            -DEXPECTED_PANIC=
            -DMEASURE_GENERATED_STACK_USAGE=TRUE
            -DSTACK_USAGE_MAX_FRAME_SIZE=262144
            -DENABLE_SANITIZERS=${R_FRONTEND_ENABLE_SANITIZERS}
            -DENABLE_THREAD_SANITIZER=${R_FRONTEND_ENABLE_THREAD_SANITIZER}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_codegen_program.cmake
    )
    add_test(
        NAME r_frontend_codegen_unzip_full
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
            -DTARGET_TOOLCHAIN_CHECK=${PROJECT_SOURCE_DIR}/tools/check_target_toolchain.py
            -DMODULE_MAP=${PROJECT_SOURCE_DIR}/examples/unzip/modules.map
            -DENTRY=example.unzip.main::main
            -DPROFILE=hosted-native-async
            -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
            -DOUTPUT_C=${CMAKE_CURRENT_BINARY_DIR}/codegen_unzip_full.c
            -DOUTPUT_EXE=${CMAKE_CURRENT_BINARY_DIR}/codegen_unzip_full
            -DC_WRAPPER=${CMAKE_CURRENT_SOURCE_DIR}/codegen_unzip_full_wrapper.c
            -DMEASURE_GENERATED_STACK_USAGE=TRUE
            -DSTACK_USAGE_MAX_FRAME_SIZE=262144
            -DSTACK_USAGE_EXPECT_FRAME_GREATER_THAN=4096
            -DRUNTIME_INCLUDE=${PROJECT_SOURCE_DIR}/runtime/include
            -DRUNTIME_DARWIN_INCLUDE=${PROJECT_SOURCE_DIR}/runtime/darwin/include
            -DR_STD_ALLOC_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/alloc/include
            -DR_STD_ASYNC_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/async/include
            -DR_STD_C_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/c/include
            -DR_STD_CONVERT_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/convert/include
            -DR_STD_ERROR_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/error/include
            -DR_STD_STRING_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/string/include
            -DR_LIBRARY_ROOT=${PROJECT_SOURCE_DIR}/library
            -DR_STD_ARC_LIBRARY=$<TARGET_FILE:r_std_arc>
            -DR_STD_ALLOC_LIBRARY=$<TARGET_FILE:r_std_alloc>
            -DR_STD_ARRAY_LIBRARY=$<TARGET_FILE:r_std_array>
            -DR_STD_LIST_LIBRARY=$<TARGET_FILE:r_std_list>
            -DR_STD_DICT_LIBRARY=$<TARGET_FILE:r_std_dict>
            -DR_STD_BITS_LIBRARY=$<TARGET_FILE:r_std_bits>
            -DR_STD_BYTES_LIBRARY=$<TARGET_FILE:r_std_bytes>
            -DR_STD_SECRET_LIBRARY=$<TARGET_FILE:r_std_secret>
            -DR_STD_RANDOM_LIBRARY=$<TARGET_FILE:r_std_random>
            -DR_STD_SIGNAL_LIBRARY=$<TARGET_FILE:r_std_signal>
            -DR_STD_TEST_LIBRARY=$<TARGET_FILE:r_std_test>
            "-DSTD_NATIVE_INCLUDE_DIRS=${R_STD_NATIVE_INCLUDE_DIRS}"
            "-DSTD_NATIVE_LIBRARIES=${R_STD_NATIVE_LINK_ARCHIVES}"
            -DR_STD_HASH_LIBRARY=$<TARGET_FILE:r_std_hash>
            -DR_STD_ERROR_LIBRARY=$<TARGET_FILE:r_std_error>
                -DR_STD_FORMAT_LIBRARY=$<TARGET_FILE:r_std_format>
                -DR_STD_JSON_LIBRARY=$<TARGET_FILE:r_std_json>
                -DR_STD_NET_LIBRARY=$<TARGET_FILE:r_std_net>
                -DR_STD_PROCESS_LIBRARY=$<TARGET_FILE:r_std_process>
                -DRUNTIME_DARWIN_SOCKET_LIBRARY=$<TARGET_FILE:r_runtime_darwin_socket>
                -DRUNTIME_DARWIN_DNS_LIBRARY=$<TARGET_FILE:r_runtime_darwin_dns>
                -DR_STD_STRING_LIBRARY=$<TARGET_FILE:r_std_string>
            -DR_STD_UTF8_LIBRARY=$<TARGET_FILE:r_std_utf8>
            -DR_CORE_LIBRARY=$<TARGET_FILE:r_core>
            -DR_STD_ASYNC_LIBRARY=$<TARGET_FILE:r_std_async>
            -DR_STD_C_LIBRARY=$<TARGET_FILE:r_std_c>
            -DR_STD_CONVERT_LIBRARY=$<TARGET_FILE:r_std_convert>
            -DR_STD_FS_LIBRARY=$<TARGET_FILE:r_std_fs>
            -DR_STD_IO_LIBRARY=$<TARGET_FILE:r_std_io>
            -DR_STD_RC_LIBRARY=$<TARGET_FILE:r_std_rc>
            -DR_STD_SYNC_LIBRARY=$<TARGET_FILE:r_std_sync>
            -DR_STD_THREAD_LIBRARY=$<TARGET_FILE:r_std_thread>
            -DR_STD_TIME_LIBRARY=$<TARGET_FILE:r_std_time>
            -DRUNTIME_HOSTED_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/hosted_entry.c
            -DRUNTIME_ALLOCATOR_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/allocator.c
            -DRUNTIME_ARRAY_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/array.c
            -DRUNTIME_LIST_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/list.c
            -DRUNTIME_DICT_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/dict.c
            -DRUNTIME_ARC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/arc.c
            -DRUNTIME_RC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/rc.c
            -DRUNTIME_DROP_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/drop.c
            -DRUNTIME_OWN_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/own.c
            -DRUNTIME_THREAD_ATTACHMENT_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/thread_attachment.c
            -DRUNTIME_TASK_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/task.c
            -DRUNTIME_STACK_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/stack.c
            -DRUNTIME_DARWIN_IO_LIBRARY=$<TARGET_FILE:r_runtime_darwin_io>
            -DRUNTIME_DARWIN_PROCESS_LIBRARY=$<TARGET_FILE:r_runtime_darwin_process>
            -DRUNTIME_DARWIN_EVENT_LIBRARY=$<TARGET_FILE:r_runtime_darwin_event>
            -DRUNTIME_DARWIN_TIMER_LIBRARY=$<TARGET_FILE:r_runtime_darwin_timer>
            -DRUNTIME_FS_LANE_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_lane.c
            -DRUNTIME_FS_LANE_ASYNC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_lane_async.c
            -DRUNTIME_FS_LANE_SYSCALLS_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_lane_syscalls.c
            -DRUNTIME_FS_SERVICE_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_service.c
            -DRUNTIME_STRING_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/string.c
            -DRUNTIME_UTF8_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/utf8.c
            -DRUNTIME_PANIC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/panic_abort.c
            -DEXPECTED_PANIC=
            -DENABLE_SANITIZERS=${R_FRONTEND_ENABLE_SANITIZERS}
            -DENABLE_THREAD_SANITIZER=${R_FRONTEND_ENABLE_THREAD_SANITIZER}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_codegen_program.cmake
    )
    add_test(
        NAME r_frontend_codegen_zip_pure
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
            -DTARGET_TOOLCHAIN_CHECK=${PROJECT_SOURCE_DIR}/tools/check_target_toolchain.py
            -DMODULE_MAP=${PROJECT_SOURCE_DIR}/examples/zip/modules.map
            -DENTRY=example.zip.tests.main::main
            -DPROFILE=hosted-native-async
            -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
            -DOUTPUT_C=${CMAKE_CURRENT_BINARY_DIR}/codegen_zip_pure.c
            -DOUTPUT_EXE=${CMAKE_CURRENT_BINARY_DIR}/codegen_zip_pure
            -DRUNTIME_INCLUDE=${PROJECT_SOURCE_DIR}/runtime/include
            -DRUNTIME_DARWIN_INCLUDE=${PROJECT_SOURCE_DIR}/runtime/darwin/include
            -DR_STD_ALLOC_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/alloc/include
            -DR_STD_ASYNC_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/async/include
            -DR_STD_C_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/c/include
            -DR_STD_CONVERT_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/convert/include
            -DR_STD_ERROR_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/error/include
            -DR_STD_STRING_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/string/include
            -DR_LIBRARY_ROOT=${PROJECT_SOURCE_DIR}/library
            -DR_STD_ARC_LIBRARY=$<TARGET_FILE:r_std_arc>
            -DR_STD_ALLOC_LIBRARY=$<TARGET_FILE:r_std_alloc>
            -DR_STD_ARRAY_LIBRARY=$<TARGET_FILE:r_std_array>
            -DR_STD_LIST_LIBRARY=$<TARGET_FILE:r_std_list>
            -DR_STD_DICT_LIBRARY=$<TARGET_FILE:r_std_dict>
            -DR_STD_BITS_LIBRARY=$<TARGET_FILE:r_std_bits>
            -DR_STD_BYTES_LIBRARY=$<TARGET_FILE:r_std_bytes>
            -DR_STD_SECRET_LIBRARY=$<TARGET_FILE:r_std_secret>
            -DR_STD_RANDOM_LIBRARY=$<TARGET_FILE:r_std_random>
            -DR_STD_SIGNAL_LIBRARY=$<TARGET_FILE:r_std_signal>
            -DR_STD_TEST_LIBRARY=$<TARGET_FILE:r_std_test>
            "-DSTD_NATIVE_INCLUDE_DIRS=${R_STD_NATIVE_INCLUDE_DIRS}"
            "-DSTD_NATIVE_LIBRARIES=${R_STD_NATIVE_LINK_ARCHIVES}"
            -DR_STD_HASH_LIBRARY=$<TARGET_FILE:r_std_hash>
            -DR_STD_ERROR_LIBRARY=$<TARGET_FILE:r_std_error>
                -DR_STD_FORMAT_LIBRARY=$<TARGET_FILE:r_std_format>
                -DR_STD_JSON_LIBRARY=$<TARGET_FILE:r_std_json>
                -DR_STD_NET_LIBRARY=$<TARGET_FILE:r_std_net>
                -DR_STD_PROCESS_LIBRARY=$<TARGET_FILE:r_std_process>
                -DRUNTIME_DARWIN_SOCKET_LIBRARY=$<TARGET_FILE:r_runtime_darwin_socket>
                -DRUNTIME_DARWIN_DNS_LIBRARY=$<TARGET_FILE:r_runtime_darwin_dns>
                -DR_STD_STRING_LIBRARY=$<TARGET_FILE:r_std_string>
            -DR_STD_UTF8_LIBRARY=$<TARGET_FILE:r_std_utf8>
            -DR_CORE_LIBRARY=$<TARGET_FILE:r_core>
            -DR_STD_ASYNC_LIBRARY=$<TARGET_FILE:r_std_async>
            -DR_STD_C_LIBRARY=$<TARGET_FILE:r_std_c>
            -DR_STD_CONVERT_LIBRARY=$<TARGET_FILE:r_std_convert>
            -DR_STD_FS_LIBRARY=$<TARGET_FILE:r_std_fs>
            -DR_STD_IO_LIBRARY=$<TARGET_FILE:r_std_io>
            -DR_STD_RC_LIBRARY=$<TARGET_FILE:r_std_rc>
            -DR_STD_SYNC_LIBRARY=$<TARGET_FILE:r_std_sync>
            -DR_STD_THREAD_LIBRARY=$<TARGET_FILE:r_std_thread>
            -DR_STD_TIME_LIBRARY=$<TARGET_FILE:r_std_time>
            -DRUNTIME_HOSTED_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/hosted_entry.c
            -DRUNTIME_ALLOCATOR_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/allocator.c
            -DRUNTIME_ARRAY_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/array.c
            -DRUNTIME_LIST_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/list.c
            -DRUNTIME_DICT_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/dict.c
            -DRUNTIME_ARC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/arc.c
            -DRUNTIME_RC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/rc.c
            -DRUNTIME_DROP_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/drop.c
            -DRUNTIME_OWN_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/own.c
            -DRUNTIME_THREAD_ATTACHMENT_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/thread_attachment.c
            -DRUNTIME_TASK_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/task.c
            -DRUNTIME_STACK_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/stack.c
            -DRUNTIME_DARWIN_IO_LIBRARY=$<TARGET_FILE:r_runtime_darwin_io>
            -DRUNTIME_DARWIN_PROCESS_LIBRARY=$<TARGET_FILE:r_runtime_darwin_process>
            -DRUNTIME_DARWIN_EVENT_LIBRARY=$<TARGET_FILE:r_runtime_darwin_event>
            -DRUNTIME_DARWIN_TIMER_LIBRARY=$<TARGET_FILE:r_runtime_darwin_timer>
            -DRUNTIME_FS_LANE_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_lane.c
            -DRUNTIME_FS_LANE_ASYNC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_lane_async.c
            -DRUNTIME_FS_LANE_SYSCALLS_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_lane_syscalls.c
            -DRUNTIME_FS_SERVICE_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_service.c
            -DRUNTIME_STRING_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/string.c
            -DRUNTIME_UTF8_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/utf8.c
            -DRUNTIME_PANIC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/panic_abort.c
            -DEXPECTED_PANIC=
            -DMEASURE_GENERATED_STACK_USAGE=TRUE
            -DSTACK_USAGE_MAX_FRAME_SIZE=262144
            -DENABLE_SANITIZERS=${R_FRONTEND_ENABLE_SANITIZERS}
            -DENABLE_THREAD_SANITIZER=${R_FRONTEND_ENABLE_THREAD_SANITIZER}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_codegen_program.cmake
    )
    add_test(
        NAME r_frontend_codegen_zip_full
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
            -DTARGET_TOOLCHAIN_CHECK=${PROJECT_SOURCE_DIR}/tools/check_target_toolchain.py
            -DMODULE_MAP=${PROJECT_SOURCE_DIR}/examples/zip/modules.map
            -DENTRY=example.zip.main::main
            -DPROFILE=hosted-native-async
            -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
            -DOUTPUT_C=${CMAKE_CURRENT_BINARY_DIR}/codegen_zip_full.c
            -DOUTPUT_EXE=${CMAKE_CURRENT_BINARY_DIR}/codegen_zip_full
            -DRUNTIME_INCLUDE=${PROJECT_SOURCE_DIR}/runtime/include
            -DRUNTIME_DARWIN_INCLUDE=${PROJECT_SOURCE_DIR}/runtime/darwin/include
            -DR_STD_ALLOC_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/alloc/include
            -DR_STD_ASYNC_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/async/include
            -DR_STD_C_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/c/include
            -DR_STD_CONVERT_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/convert/include
            -DR_STD_ERROR_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/error/include
            -DR_STD_STRING_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/string/include
            -DR_LIBRARY_ROOT=${PROJECT_SOURCE_DIR}/library
            -DR_STD_ARC_LIBRARY=$<TARGET_FILE:r_std_arc>
            -DR_STD_ALLOC_LIBRARY=$<TARGET_FILE:r_std_alloc>
            -DR_STD_ARRAY_LIBRARY=$<TARGET_FILE:r_std_array>
            -DR_STD_LIST_LIBRARY=$<TARGET_FILE:r_std_list>
            -DR_STD_DICT_LIBRARY=$<TARGET_FILE:r_std_dict>
            -DR_STD_BITS_LIBRARY=$<TARGET_FILE:r_std_bits>
            -DR_STD_BYTES_LIBRARY=$<TARGET_FILE:r_std_bytes>
            -DR_STD_SECRET_LIBRARY=$<TARGET_FILE:r_std_secret>
            -DR_STD_RANDOM_LIBRARY=$<TARGET_FILE:r_std_random>
            -DR_STD_SIGNAL_LIBRARY=$<TARGET_FILE:r_std_signal>
            -DR_STD_TEST_LIBRARY=$<TARGET_FILE:r_std_test>
            "-DSTD_NATIVE_INCLUDE_DIRS=${R_STD_NATIVE_INCLUDE_DIRS}"
            "-DSTD_NATIVE_LIBRARIES=${R_STD_NATIVE_LINK_ARCHIVES}"
            -DR_STD_HASH_LIBRARY=$<TARGET_FILE:r_std_hash>
            -DR_STD_ERROR_LIBRARY=$<TARGET_FILE:r_std_error>
                -DR_STD_FORMAT_LIBRARY=$<TARGET_FILE:r_std_format>
                -DR_STD_JSON_LIBRARY=$<TARGET_FILE:r_std_json>
                -DR_STD_NET_LIBRARY=$<TARGET_FILE:r_std_net>
                -DR_STD_PROCESS_LIBRARY=$<TARGET_FILE:r_std_process>
                -DRUNTIME_DARWIN_SOCKET_LIBRARY=$<TARGET_FILE:r_runtime_darwin_socket>
                -DRUNTIME_DARWIN_DNS_LIBRARY=$<TARGET_FILE:r_runtime_darwin_dns>
                -DR_STD_STRING_LIBRARY=$<TARGET_FILE:r_std_string>
            -DR_STD_UTF8_LIBRARY=$<TARGET_FILE:r_std_utf8>
            -DR_CORE_LIBRARY=$<TARGET_FILE:r_core>
            -DR_STD_ASYNC_LIBRARY=$<TARGET_FILE:r_std_async>
            -DR_STD_C_LIBRARY=$<TARGET_FILE:r_std_c>
            -DR_STD_CONVERT_LIBRARY=$<TARGET_FILE:r_std_convert>
            -DR_STD_FS_LIBRARY=$<TARGET_FILE:r_std_fs>
            -DR_STD_IO_LIBRARY=$<TARGET_FILE:r_std_io>
            -DR_STD_RC_LIBRARY=$<TARGET_FILE:r_std_rc>
            -DR_STD_SYNC_LIBRARY=$<TARGET_FILE:r_std_sync>
            -DR_STD_THREAD_LIBRARY=$<TARGET_FILE:r_std_thread>
            -DR_STD_TIME_LIBRARY=$<TARGET_FILE:r_std_time>
            -DRUNTIME_HOSTED_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/hosted_entry.c
            -DRUNTIME_ALLOCATOR_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/allocator.c
            -DRUNTIME_ARRAY_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/array.c
            -DRUNTIME_LIST_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/list.c
            -DRUNTIME_DICT_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/dict.c
            -DRUNTIME_ARC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/arc.c
            -DRUNTIME_RC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/rc.c
            -DRUNTIME_DROP_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/drop.c
            -DRUNTIME_OWN_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/own.c
            -DRUNTIME_THREAD_ATTACHMENT_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/thread_attachment.c
            -DRUNTIME_TASK_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/task.c
            -DRUNTIME_STACK_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/stack.c
            -DRUNTIME_DARWIN_IO_LIBRARY=$<TARGET_FILE:r_runtime_darwin_io>
            -DRUNTIME_DARWIN_PROCESS_LIBRARY=$<TARGET_FILE:r_runtime_darwin_process>
            -DRUNTIME_DARWIN_EVENT_LIBRARY=$<TARGET_FILE:r_runtime_darwin_event>
            -DRUNTIME_DARWIN_TIMER_LIBRARY=$<TARGET_FILE:r_runtime_darwin_timer>
            -DRUNTIME_FS_LANE_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_lane.c
            -DRUNTIME_FS_LANE_ASYNC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_lane_async.c
            -DRUNTIME_FS_LANE_SYSCALLS_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_lane_syscalls.c
            -DRUNTIME_FS_SERVICE_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_service.c
            -DRUNTIME_STRING_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/string.c
            -DRUNTIME_UTF8_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/utf8.c
            -DRUNTIME_PANIC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/panic_abort.c
            -DEXPECTED_PANIC=
            -DMEASURE_GENERATED_STACK_USAGE=TRUE
            -DSTACK_USAGE_MAX_FRAME_SIZE=262144
            -DCOMPILE_ONLY=TRUE
            -DENABLE_SANITIZERS=${R_FRONTEND_ENABLE_SANITIZERS}
            -DENABLE_THREAD_SANITIZER=${R_FRONTEND_ENABLE_THREAD_SANITIZER}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_codegen_program.cmake
    )
    add_test(
        NAME r_frontend_codegen_unzip_roundtrip
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
            -DTARGET_TOOLCHAIN_CHECK=${PROJECT_SOURCE_DIR}/tools/check_target_toolchain.py
            -DMODULE_MAP=${PROJECT_SOURCE_DIR}/examples/unzip/modules.map
            -DENTRY=example.unzip.main::main
            -DPROFILE=hosted-native-async
            -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
            -DOUTPUT_C=${CMAKE_CURRENT_BINARY_DIR}/codegen_unzip_roundtrip.c
            -DOUTPUT_EXE=${CMAKE_CURRENT_BINARY_DIR}/codegen_unzip_roundtrip
            -DRUNTIME_INCLUDE=${PROJECT_SOURCE_DIR}/runtime/include
            -DRUNTIME_DARWIN_INCLUDE=${PROJECT_SOURCE_DIR}/runtime/darwin/include
            -DR_STD_ALLOC_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/alloc/include
            -DR_STD_ASYNC_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/async/include
            -DR_STD_C_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/c/include
            -DR_STD_CONVERT_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/convert/include
            -DR_STD_ERROR_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/error/include
            -DR_STD_STRING_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/string/include
            -DR_LIBRARY_ROOT=${PROJECT_SOURCE_DIR}/library
            -DR_STD_ARC_LIBRARY=$<TARGET_FILE:r_std_arc>
            -DR_STD_ALLOC_LIBRARY=$<TARGET_FILE:r_std_alloc>
            -DR_STD_ARRAY_LIBRARY=$<TARGET_FILE:r_std_array>
            -DR_STD_LIST_LIBRARY=$<TARGET_FILE:r_std_list>
            -DR_STD_DICT_LIBRARY=$<TARGET_FILE:r_std_dict>
            -DR_STD_BITS_LIBRARY=$<TARGET_FILE:r_std_bits>
            -DR_STD_BYTES_LIBRARY=$<TARGET_FILE:r_std_bytes>
            -DR_STD_SECRET_LIBRARY=$<TARGET_FILE:r_std_secret>
            -DR_STD_RANDOM_LIBRARY=$<TARGET_FILE:r_std_random>
            -DR_STD_SIGNAL_LIBRARY=$<TARGET_FILE:r_std_signal>
            -DR_STD_TEST_LIBRARY=$<TARGET_FILE:r_std_test>
            "-DSTD_NATIVE_INCLUDE_DIRS=${R_STD_NATIVE_INCLUDE_DIRS}"
            "-DSTD_NATIVE_LIBRARIES=${R_STD_NATIVE_LINK_ARCHIVES}"
            -DR_STD_HASH_LIBRARY=$<TARGET_FILE:r_std_hash>
            -DR_STD_ERROR_LIBRARY=$<TARGET_FILE:r_std_error>
                -DR_STD_FORMAT_LIBRARY=$<TARGET_FILE:r_std_format>
                -DR_STD_JSON_LIBRARY=$<TARGET_FILE:r_std_json>
                -DR_STD_NET_LIBRARY=$<TARGET_FILE:r_std_net>
                -DR_STD_PROCESS_LIBRARY=$<TARGET_FILE:r_std_process>
                -DRUNTIME_DARWIN_SOCKET_LIBRARY=$<TARGET_FILE:r_runtime_darwin_socket>
                -DRUNTIME_DARWIN_DNS_LIBRARY=$<TARGET_FILE:r_runtime_darwin_dns>
                -DR_STD_STRING_LIBRARY=$<TARGET_FILE:r_std_string>
            -DR_STD_UTF8_LIBRARY=$<TARGET_FILE:r_std_utf8>
            -DR_CORE_LIBRARY=$<TARGET_FILE:r_core>
            -DR_STD_ASYNC_LIBRARY=$<TARGET_FILE:r_std_async>
            -DR_STD_C_LIBRARY=$<TARGET_FILE:r_std_c>
            -DR_STD_CONVERT_LIBRARY=$<TARGET_FILE:r_std_convert>
            -DR_STD_FS_LIBRARY=$<TARGET_FILE:r_std_fs>
            -DR_STD_IO_LIBRARY=$<TARGET_FILE:r_std_io>
            -DR_STD_RC_LIBRARY=$<TARGET_FILE:r_std_rc>
            -DR_STD_SYNC_LIBRARY=$<TARGET_FILE:r_std_sync>
            -DR_STD_THREAD_LIBRARY=$<TARGET_FILE:r_std_thread>
            -DR_STD_TIME_LIBRARY=$<TARGET_FILE:r_std_time>
            -DRUNTIME_HOSTED_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/hosted_entry.c
            -DRUNTIME_ALLOCATOR_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/allocator.c
            -DRUNTIME_ARRAY_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/array.c
            -DRUNTIME_LIST_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/list.c
            -DRUNTIME_DICT_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/dict.c
            -DRUNTIME_ARC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/arc.c
            -DRUNTIME_RC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/rc.c
            -DRUNTIME_DROP_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/drop.c
            -DRUNTIME_OWN_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/own.c
            -DRUNTIME_THREAD_ATTACHMENT_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/thread_attachment.c
            -DRUNTIME_TASK_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/task.c
            -DRUNTIME_STACK_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/stack.c
            -DRUNTIME_DARWIN_IO_LIBRARY=$<TARGET_FILE:r_runtime_darwin_io>
            -DRUNTIME_DARWIN_PROCESS_LIBRARY=$<TARGET_FILE:r_runtime_darwin_process>
            -DRUNTIME_DARWIN_EVENT_LIBRARY=$<TARGET_FILE:r_runtime_darwin_event>
            -DRUNTIME_DARWIN_TIMER_LIBRARY=$<TARGET_FILE:r_runtime_darwin_timer>
            -DRUNTIME_FS_LANE_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_lane.c
            -DRUNTIME_FS_LANE_ASYNC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_lane_async.c
            -DRUNTIME_FS_LANE_SYSCALLS_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_lane_syscalls.c
            -DRUNTIME_FS_SERVICE_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_service.c
            -DRUNTIME_STRING_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/string.c
            -DRUNTIME_UTF8_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/utf8.c
            -DRUNTIME_PANIC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/panic_abort.c
            -DEXPECTED_PANIC=
            -DMEASURE_GENERATED_STACK_USAGE=TRUE
            -DSTACK_USAGE_MAX_FRAME_SIZE=262144
            -DCOMPILE_ONLY=TRUE
            -DENABLE_SANITIZERS=${R_FRONTEND_ENABLE_SANITIZERS}
            -DENABLE_THREAD_SANITIZER=${R_FRONTEND_ENABLE_THREAD_SANITIZER}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_codegen_program.cmake
    )
    set_tests_properties(
        r_frontend_codegen_zip_full
        PROPERTIES FIXTURES_SETUP r_zip_roundtrip_zip
    )
    set_tests_properties(
        r_frontend_codegen_unzip_roundtrip
        PROPERTIES FIXTURES_SETUP r_zip_roundtrip_unzip
    )
    add_test(
        NAME r_frontend_zip_unzip_roundtrip
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/run_zip_roundtrip.py"
            --zip-executable "${CMAKE_CURRENT_BINARY_DIR}/codegen_zip_full"
            --unzip-executable "${CMAKE_CURRENT_BINARY_DIR}/codegen_unzip_roundtrip"
    )
    set_tests_properties(
        r_frontend_zip_unzip_roundtrip
        PROPERTIES
            DEPENDS "r_frontend_codegen_zip_full;r_frontend_codegen_unzip_roundtrip"
            FIXTURES_REQUIRED "r_zip_roundtrip_zip;r_zip_roundtrip_unzip"
            TIMEOUT 120
    )
    if(R_FRONTEND_ENABLE_SANITIZERS)
        set_property(
            TEST r_frontend_zip_unzip_roundtrip
            PROPERTY ENVIRONMENT
                "ASAN_OPTIONS=abort_on_error=1"
                "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
        )
    elseif(R_FRONTEND_ENABLE_THREAD_SANITIZER)
        set_property(
            TEST r_frontend_zip_unzip_roundtrip
            PROPERTY ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1"
        )
    endif()
    add_test(
        NAME r_frontend_shared_owner_clone_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_shared_owner_clone.r
            "-DSYNC_SOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_shared_owner_clone_sync.r"
            "-DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_shared_owner_clone_artifacts.cmake
    )
    add_test(
        NAME r_frontend_async_cancel_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_cancel.r
            "-DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_async_cancel_artifacts.cmake
    )
    add_test(
        NAME r_frontend_async_stderr_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_stderr.r
            "-DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_async_stderr_artifacts.cmake
    )
    add_test(
        NAME r_frontend_stdout_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSYNC_SOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_stdout.r
            -DASYNC_SOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_stdout.r
            "-DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_stdout_artifacts.cmake
    )
    add_test(
        NAME r_frontend_stdin_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSYNC_SOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_stdin.r
            -DASYNC_SOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_stdin.r
            "-DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_stdin_artifacts.cmake
    )
    add_test(
        NAME r_frontend_async_flush_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_flush.r
            "-DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_async_flush_artifacts.cmake
    )
    add_test(
        NAME r_frontend_async_io_read_codegen_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_io_read.r
            "-DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_async_io_read_codegen_artifacts.cmake
    )
    add_test(
        NAME r_frontend_async_io_read_result_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_io_read_result.r
            "-DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_async_io_read_result_artifacts.cmake
    )
    add_test(
        NAME r_frontend_async_io_write_result_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_io_write_result.r
            "-DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_async_io_write_result_artifacts.cmake
    )
    add_test(
        NAME r_frontend_async_io_write_shared_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_io_write_shared.r
            "-DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_async_io_write_shared_artifacts.cmake
    )
    add_test(
        NAME r_frontend_io_as_error_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_io_as_error.r
            "-DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_io_as_error_artifacts.cmake
    )
    add_test(
        NAME r_frontend_fs_as_error_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_fs_as_error.r
            "-DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_fs_as_error_artifacts.cmake
    )
    add_test(
        NAME r_frontend_fs_path_is_absolute_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_fs_path_is_absolute.r
            "-DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_fs_path_is_absolute_artifacts.cmake
    )
    add_test(
        NAME r_frontend_async_io_write_all_codegen_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_io_write_all.r
            "-DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_async_io_write_all_codegen_artifacts.cmake
    )
    add_test(
        NAME r_frontend_async_io_close_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DINPUT_SOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_io_close_input.r
            -DOUTPUT_SOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_io_close_output.r
            "-DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_async_io_close_artifacts.cmake
    )
    add_test(
        NAME r_frontend_async_fs_read_file_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_fs_read_file.r
            "-DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            -DCASE=read_file
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_async_fs_artifacts.cmake
    )
    add_test(
        NAME r_frontend_async_fs_open_directory_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_fs_open_directory.r
            "-DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            -DCASE=open_directory
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_async_fs_artifacts.cmake
    )
    add_test(
        NAME r_frontend_async_fs_file_metadata_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_fs_file_metadata.r
            "-DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            -DCASE=file_metadata
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_async_fs_artifacts.cmake
    )
    add_test(
        NAME r_frontend_async_fs_create_directory_beneath_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_fs_create_directory_beneath.r
            -DNEGATIVE_SOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_async_fs_create_directory_beneath_negative.r
            "-DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_async_fs_create_directory_beneath_artifacts.cmake
    )
    add_test(
        NAME r_frontend_async_fs_write_file_atomic_no_replace_beneath_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_fs_write_file_atomic_no_replace_beneath.r
            "-DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_async_fs_atomic_write_beneath_artifacts.cmake
    )
    add_test(
        NAME r_frontend_sync_async_start_matrix_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DUSER_SOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_sync_async_start_user.r
            -DSTD_SOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_sync_async_start_std.r
            "-DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_sync_async_start_matrix_artifacts.cmake
    )
    add_test(
        NAME r_frontend_native_result_abi_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_native_result_abi.r
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_native_result_abi_artifacts.cmake
    )
    add_test(
        NAME r_frontend_standard_outcome_codegen_artifacts
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSYNC_SOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_standard_outcomes.r
            -DMAIN_SOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_integer_switch.r
            -DASYNC_SOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_async_standard_outcomes.r
            "-DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_standard_outcome_codegen_artifacts.cmake
    )
    if(R_CODEGEN_CLANG_FORMAT)
        add_test(
            NAME r_frontend_codegen_unzip_pure_format
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DCLANG_FORMAT=${R_CODEGEN_CLANG_FORMAT}
                -DMODULE_MAP=${PROJECT_SOURCE_DIR}/examples/unzip/modules.map
                -DENTRY=example.unzip.tests.main::main
                -DPROFILE=hosted-native-async
                -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
                -DFORMAT_STYLE=${PROJECT_SOURCE_DIR}/.clang-format
                -DOUTPUT_C=${CMAKE_CURRENT_BINARY_DIR}/codegen_unzip_pure_format.c
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_codegen_format.cmake
        )
        add_test(
            NAME r_frontend_codegen_unzip_full_format
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DCLANG_FORMAT=${R_CODEGEN_CLANG_FORMAT}
                -DMODULE_MAP=${PROJECT_SOURCE_DIR}/examples/unzip/modules.map
                -DENTRY=example.unzip.main::main
                -DPROFILE=hosted-native-async
                -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
                -DFORMAT_STYLE=${PROJECT_SOURCE_DIR}/.clang-format
                -DOUTPUT_C=${CMAKE_CURRENT_BINARY_DIR}/codegen_unzip_full_format.c
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_codegen_format.cmake
        )
        add_test(
            NAME r_frontend_codegen_zip_pure_format
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DCLANG_FORMAT=${R_CODEGEN_CLANG_FORMAT}
                -DMODULE_MAP=${PROJECT_SOURCE_DIR}/examples/zip/modules.map
                -DENTRY=example.zip.tests.main::main
                -DPROFILE=hosted-native-async
                -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
                -DFORMAT_STYLE=${PROJECT_SOURCE_DIR}/.clang-format
                -DOUTPUT_C=${CMAKE_CURRENT_BINARY_DIR}/codegen_zip_pure_format.c
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_codegen_format.cmake
        )
        add_test(
            NAME r_frontend_codegen_zip_full_format
            COMMAND "${CMAKE_COMMAND}"
                -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
                -DCLANG_FORMAT=${R_CODEGEN_CLANG_FORMAT}
                -DMODULE_MAP=${PROJECT_SOURCE_DIR}/examples/zip/modules.map
                -DENTRY=example.zip.main::main
                -DPROFILE=hosted-native-async
                -DTARGET_MANIFEST=${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json
                -DFORMAT_STYLE=${PROJECT_SOURCE_DIR}/.clang-format
                -DOUTPUT_C=${CMAKE_CURRENT_BINARY_DIR}/codegen_zip_full_format.c
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_codegen_format.cmake
        )
    endif()
endif()

add_test(
    NAME r_frontend_unzip
    COMMAND "${CMAKE_COMMAND}"
        -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
        -DMODULE_MAP=${PROJECT_SOURCE_DIR}/examples/unzip/modules.map
        -DOUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/unzip.ast
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_unzip.cmake
)

add_test(
    NAME r_frontend_zip
    COMMAND "${CMAKE_COMMAND}"
        -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
        -DMODULE_MAP=${PROJECT_SOURCE_DIR}/examples/zip/modules.map
        -DOUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/zip.ast
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_zip.cmake
)

add_test(
    NAME r_frontend_cli_json_diagnostics
    COMMAND "${CMAKE_COMMAND}"
        -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
        -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/invalid_condition.r
        -P ${CMAKE_CURRENT_SOURCE_DIR}/check_cli_json.cmake
)

foreach(EMIT_KIND IN ITEMS tokens cst ast)
    add_test(
        NAME r_frontend_golden_${EMIT_KIND}
        COMMAND "${CMAKE_COMMAND}"
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/golden.r
            -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/golden/golden.${EMIT_KIND}.sexp
            -DEMIT_KIND=${EMIT_KIND}
            -DACTUAL_FILE=${CMAKE_CURRENT_BINARY_DIR}/golden.${EMIT_KIND}.actual
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_golden.cmake
    )
endforeach()

add_test(
    NAME r_frontend_async_mir_golden
    COMMAND "${CMAKE_COMMAND}"
        -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
        -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/async_mir.r
        -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/golden/async_mir.sexp
        -DEMIT_KIND=mir
        -DACTUAL_FILE=${CMAKE_CURRENT_BINARY_DIR}/async_mir.actual
        -P ${CMAKE_CURRENT_SOURCE_DIR}/check_golden.cmake
)

add_test(
    NAME r_frontend_async_copy_args_mir_golden
    COMMAND "${CMAKE_COMMAND}"
        -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
        -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/async_copy_args.r
        -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/golden/async_copy_args.mir.sexp
        -DEMIT_KIND=mir
        -DACTUAL_FILE=${CMAKE_CURRENT_BINARY_DIR}/async_copy_args.mir.actual
        -P ${CMAKE_CURRENT_SOURCE_DIR}/check_golden.cmake
)

add_test(
    NAME r_frontend_switch_clauses_mir_golden
    COMMAND "${CMAKE_COMMAND}"
        -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
        -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/codegen_switch_clauses.r
        -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/golden/switch_clauses.mir.sexp
        -DEMIT_KIND=mir
        -DACTUAL_FILE=${CMAKE_CURRENT_BINARY_DIR}/switch_clauses.mir.actual
        -P ${CMAKE_CURRENT_SOURCE_DIR}/check_golden.cmake
)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(
        NAME r_frontend_grammar_manifest
        COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tools/check_grammar.py"
            --specification "${PROJECT_SOURCE_DIR}/specification/R_LANGUAGE_SPECIFICATION_0_1.en.adoc"
            --manifest "${PROJECT_SOURCE_DIR}/compiler/grammar/annex_a_manifest.json"
    )
    add_test(
        NAME r_library_inventory_tooling
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tooling/test_library_inventory.py"
    )
    add_test(
        NAME r_format_check_tooling
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tooling/test_format_check.py"
    )
    add_test(
        NAME r_c_style_check_tooling
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tooling/test_c_style_check.py"
    )
    add_test(
        NAME r_no_production_assertions_tooling
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tooling/test_no_production_asserts.py"
    )
    add_test(
        NAME r_no_production_assertions
        COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tools/check_no_production_asserts.py"
            --root "${PROJECT_SOURCE_DIR}"
    )
    add_test(
        NAME r_specification_contract_tooling
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tooling/test_spec_contracts.py"
    )
    add_test(
        NAME r_specification_render_tooling
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tooling/test_spec_render.py"
    )
    add_test(
        NAME r_target_abi_tooling
        COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tools/generate_target_abi.py"
            --root "${PROJECT_SOURCE_DIR}"
            --manifest
            "${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
            --verify
    )
    add_test(
        NAME r_runtime_entry_stack_tooling
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tooling/test_runtime_entry_stack.py"
    )
    add_test(
        NAME r_stack_entries_tooling
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tooling/test_compute_stack_entries.py"
    )
    if(APPLE)
        add_test(
            NAME r_target_toolchain
            COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tools/check_target_toolchain.py"
                --manifest
                "${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json"
                --cc "${CMAKE_C_COMPILER}"
        )
    endif()
    add_test(
        NAME r_named_standard_copy_abi_tooling
        COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tools/generate_named_standard_copy_abi.py"
            --root "${PROJECT_SOURCE_DIR}"
            --verify
    )
    add_test(
        NAME r_named_standard_move_abi_tooling
        COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tools/generate_named_standard_move_abi.py"
            --root "${PROJECT_SOURCE_DIR}"
            --verify
    )
    add_test(
        NAME r_standard_type_registry_tooling
        COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tools/generate_standard_type_registry.py"
            --root "${PROJECT_SOURCE_DIR}"
            --verify
    )
    add_test(
        NAME r_standard_operation_registry_tooling
        COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tools/generate_standard_operation_registry.py"
            --root "${PROJECT_SOURCE_DIR}"
            --verify
    )
endif()

if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    get_property(R_TEST_EXECUTABLES DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(R_TEST_EXECUTABLE IN LISTS R_TEST_EXECUTABLES)
        get_target_property(R_TEST_TARGET_TYPE ${R_TEST_EXECUTABLE} TYPE)
        if(R_TEST_TARGET_TYPE STREQUAL "EXECUTABLE")
            target_compile_options(${R_TEST_EXECUTABLE} PRIVATE
                -fsanitize=thread
                -fno-omit-frame-pointer
            )
            target_link_options(${R_TEST_EXECUTABLE} PRIVATE
                -fsanitize=thread
                -fno-omit-frame-pointer
            )
        endif()
    endforeach()
endif()

# Source-level contracts for typed synchronization outcomes.
foreach(R_SYNC_CASE IN ITEMS
        "wrong_resource|R-DIAG-TYPE-001"
        "wrong_payload|R-DIAG-TYPE-001"
        "missing_move|R-DIAG-MOVE-001"
        "use_after_move|R-DIAG-MOVE-002"
        "borrow_drop|R-DIAG-BORROW-001"
        "wrong_result|R-DIAG-TYPE-001"
        "fieldless_binding|R-DIAG-TYPE-001"
)
    string(REPLACE "|" ";" R_SYNC_PARTS "${R_SYNC_CASE}")
    list(GET R_SYNC_PARTS 0 R_SYNC_NAME)
    list(GET R_SYNC_PARTS 1 R_SYNC_DIAGNOSTIC)
    add_test(NAME r_frontend_semantic_sync_${R_SYNC_NAME}
        COMMAND ${CMAKE_COMMAND}
            -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/semantic_sync_${R_SYNC_NAME}.r
            -DEXPECTATION=reject
            -DREQUIRED_DIAGNOSTIC=${R_SYNC_DIAGNOSTIC}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_semantic_regression.cmake)
endforeach()

if(Python3_Interpreter_FOUND)
    add_test(NAME r_frontend_fs_extended_semantics
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/check_fs_extended_semantics.py"
            "$<TARGET_FILE:r-front>")
endif()

if(Python3_Interpreter_FOUND)
    # M5: R/C benchmark pairs, the `benchmarks` target and the pair-agreement test.
    add_subdirectory(bench)
endif()
