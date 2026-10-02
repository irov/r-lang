# The native provider of std.tls (Library R-SLIB-TLS-0001): an engine over Mbed TLS 3.6 that the
# R part of the module imports through the checked C boundary (Core R-FFI-0028..0044). The
# library link manifest library/r/links.json names it std.tls.native and its dependency mbedtls.
find_package(MbedTLS 3.6 CONFIG REQUIRED GLOBAL HINTS /opt/homebrew/opt/mbedtls@3 /usr/local/opt/mbedtls@3)

add_library(r_std_tls_native STATIC source/r_std_tls_native.c)
target_include_directories(r_std_tls_native PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(r_std_tls_native PUBLIC MbedTLS::mbedtls_static)
# The hosted allocator of the runtime; a generated program links the runtime itself.
target_include_directories(r_std_tls_native PRIVATE "${R_LIBRARY_REPOSITORY_ROOT}/runtime/include")
target_compile_features(r_std_tls_native PUBLIC c_std_17)
set_target_properties(r_std_tls_native PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
if(NOT MSVC)
    target_compile_options(r_std_tls_native PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()
if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_std_tls_native PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(r_std_tls_native PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()
if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_std_tls_native PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(r_std_tls_native PUBLIC -fsanitize=thread -fno-omit-frame-pointer)
endif()

# The archives that a program importing std.tls links after its generated code, in link order.
set(R_STD_TLS_NATIVE_LINK_ARCHIVES
    "$<TARGET_FILE:r_std_tls_native>"
    "$<TARGET_FILE:MbedTLS::mbedtls_static>"
    "$<TARGET_FILE:MbedTLS::mbedx509_static>"
    "$<TARGET_FILE:MbedTLS::mbedcrypto_static>"
    "$<TARGET_FILE:MbedTLS::everest>"
    "$<TARGET_FILE:MbedTLS::p256m>"
    CACHE INTERNAL "Archives of the std.tls native provider")
set(R_STD_TLS_NATIVE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include"
    CACHE INTERNAL "Header directory of the std.tls native provider")
