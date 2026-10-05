# The native provider of std.crypto (Library R-SLIB-CRYPTO-0001): libsodium primitives and the
# public-key and block-cipher primitives of Mbed TLS 3.6 (PSA Crypto) that the R part of the
# module imports through the checked C boundary (Core R-FFI-0028..0044). The library link
# manifest library/r/links.json names it std.crypto.native and its dependencies sodium and
# mbedtls; the Mbed TLS package is the one native/tls found (GLOBAL).
find_path(R_SODIUM_INCLUDE_DIR sodium.h
    HINTS /opt/homebrew/opt/libsodium/include /usr/local/opt/libsodium/include REQUIRED)
find_library(R_SODIUM_STATIC_LIBRARY NAMES libsodium.a
    HINTS /opt/homebrew/opt/libsodium/lib /usr/local/opt/libsodium/lib REQUIRED)

add_library(r_std_crypto_native STATIC source/r_std_crypto_native.c)
target_include_directories(r_std_crypto_native PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(r_std_crypto_native PRIVATE "${R_SODIUM_INCLUDE_DIR}")
target_link_libraries(r_std_crypto_native PUBLIC "${R_SODIUM_STATIC_LIBRARY}" MbedTLS::mbedcrypto_static)
target_compile_features(r_std_crypto_native PUBLIC c_std_17)
set_target_properties(r_std_crypto_native PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
if(NOT MSVC)
    target_compile_options(r_std_crypto_native PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()
if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_std_crypto_native PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(r_std_crypto_native PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()
if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_std_crypto_native PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(r_std_crypto_native PUBLIC -fsanitize=thread -fno-omit-frame-pointer)
endif()

# The archives that a program importing std.crypto links after its generated code, in link order.
set(R_STD_CRYPTO_NATIVE_LINK_ARCHIVES
    "$<TARGET_FILE:r_std_crypto_native>"
    "${R_SODIUM_STATIC_LIBRARY}"
    "$<TARGET_FILE:MbedTLS::mbedcrypto_static>"
    "$<TARGET_FILE:MbedTLS::everest>"
    "$<TARGET_FILE:MbedTLS::p256m>"
    CACHE INTERNAL "Archives of the std.crypto native provider")
set(R_STD_CRYPTO_NATIVE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include"
    CACHE INTERNAL "Header directory of the std.crypto native provider")
