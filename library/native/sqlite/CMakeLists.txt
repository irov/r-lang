# The native provider of std.sqlite (Library R-SLIB-SQLITE-0001): handles over SQLite that the R
# part of the module imports through the checked C boundary (Core R-FFI-0028..0044). The library
# link manifest library/r/links.json names it std.sqlite.native and its dependency sqlite3.

# SQLite itself, built from its amalgamation with the options that README.md of this directory
# explains (Library R-SLIB-IDB-0017). The archive is downloaded at configuration and checked
# against the SHA3-256 of the SQLite download page; it is third-party code, compiled without the
# warning set of the repository.
include("${R_LIBRARY_REPOSITORY_ROOT}/cmake/RThirdParty.cmake")
r_third_party_archive(R_SQLITE3_ROOT
    NAME sqlite-3.53.4
    URL "https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip"
    FILE sqlite-amalgamation-3530400.zip
    HASH "SHA3_256=628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e")
set(R_SQLITE3_SOURCE_DIR "${R_SQLITE3_ROOT}/sqlite-amalgamation-3530400")
set(R_SQLITE3_HEADER "${R_SQLITE3_SOURCE_DIR}/sqlite3.h" CACHE INTERNAL "Header of the built SQLite")
add_library(r_sqlite3 STATIC "${R_SQLITE3_SOURCE_DIR}/sqlite3.c")
target_include_directories(r_sqlite3 PUBLIC "${R_SQLITE3_SOURCE_DIR}")
target_compile_definitions(r_sqlite3 PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_DQS=0
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_OMIT_LOAD_EXTENSION=1
    SQLITE_OMIT_DEPRECATED=1
    SQLITE_OMIT_SHARED_CACHE=1
    SQLITE_ENABLE_MATH_FUNCTIONS=1
    SQLITE_ENABLE_FTS5=1
    SQLITE_ENABLE_API_ARMOR=1
    HAVE_LOCALTIME_R=1
)
set_target_properties(r_sqlite3 PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS YES
)
if(NOT MSVC)
    target_compile_options(r_sqlite3 PRIVATE -w)
endif()
if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_sqlite3 PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(r_sqlite3 PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()
if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_sqlite3 PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(r_sqlite3 PUBLIC -fsanitize=thread -fno-omit-frame-pointer)
endif()

add_library(r_std_sqlite_native STATIC source/r_std_sqlite_native.c)
target_include_directories(r_std_sqlite_native PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(r_std_sqlite_native PUBLIC r_sqlite3)
# The hosted allocator of the runtime; a generated program links the runtime itself.
target_include_directories(r_std_sqlite_native PRIVATE "${R_LIBRARY_REPOSITORY_ROOT}/runtime/include")
target_compile_features(r_std_sqlite_native PUBLIC c_std_17)
set_target_properties(r_std_sqlite_native PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
if(NOT MSVC)
    target_compile_options(r_std_sqlite_native PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
    )
endif()
if((R_FRONTEND_ENABLE_SANITIZERS OR R_FRONTEND_ENABLE_FUZZERS) AND NOT MSVC)
    target_compile_options(r_std_sqlite_native PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(r_std_sqlite_native PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()
if(R_FRONTEND_ENABLE_THREAD_SANITIZER AND NOT MSVC)
    target_compile_options(r_std_sqlite_native PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(r_std_sqlite_native PUBLIC -fsanitize=thread -fno-omit-frame-pointer)
endif()

# The archives that a program importing std.sqlite links after its generated code, in link order.
set(R_STD_SQLITE_NATIVE_LINK_ARCHIVES
    "$<TARGET_FILE:r_std_sqlite_native>"
    "$<TARGET_FILE:r_sqlite3>"
    CACHE INTERNAL "Archives of the std.sqlite native provider")
set(R_STD_SQLITE_NATIVE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include"
    CACHE INTERNAL "Header directory of the std.sqlite native provider")
