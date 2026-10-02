# Third-party sources are not kept in the repository: configuration downloads each pinned archive
# once, checks its hash and unpacks it into R_THIRD_PARTY_DIR, which every build tree shares. An
# archive already in that directory with the right hash is used without the network, so an
# offline machine can be prepared by copying the archives there.
include_guard(GLOBAL)

set(R_THIRD_PARTY_DIR "${CMAKE_CURRENT_LIST_DIR}/../build/third_party" CACHE PATH
    "Download directory of the pinned third-party archives, shared by all build trees")
get_filename_component(R_THIRD_PARTY_DIR "${R_THIRD_PARTY_DIR}" ABSOLUTE)

# r_third_party_archive(<result> NAME <name> URL <url> FILE <archive file> HASH <ALGO=value>)
# sets <result> to the directory into which the archive is unpacked.
function(r_third_party_archive result)
    cmake_parse_arguments(PARSE_ARGV 1 ARCHIVE "" "NAME;URL;FILE;HASH" "")
    if(NOT ARCHIVE_NAME OR NOT ARCHIVE_URL OR NOT ARCHIVE_FILE OR NOT ARCHIVE_HASH)
        message(FATAL_ERROR "r_third_party_archive needs NAME, URL, FILE and HASH")
    endif()
    string(REPLACE "=" ";" hash_parts "${ARCHIVE_HASH}")
    list(GET hash_parts 0 algorithm)
    list(GET hash_parts 1 expected)
    set(archive "${R_THIRD_PARTY_DIR}/${ARCHIVE_FILE}")
    set(root "${R_THIRD_PARTY_DIR}/${ARCHIVE_NAME}")
    set(stamp "${root}.stamp")

    file(MAKE_DIRECTORY "${R_THIRD_PARTY_DIR}")
    # Build trees configured at the same time unpack one archive once.
    file(LOCK "${R_THIRD_PARTY_DIR}/.lock" GUARD FUNCTION TIMEOUT 600)
    if(EXISTS "${stamp}")
        file(READ "${stamp}" unpacked)
        if(unpacked STREQUAL ARCHIVE_HASH AND IS_DIRECTORY "${root}")
            set(${result} "${root}" PARENT_SCOPE)
            return()
        endif()
    endif()

    set(present FALSE)
    if(EXISTS "${archive}")
        file(${algorithm} "${archive}" actual)
        if(actual STREQUAL expected)
            set(present TRUE)
        else()
            file(REMOVE "${archive}")
        endif()
    endif()
    if(NOT present)
        message(STATUS "Downloading ${ARCHIVE_URL}")
        file(DOWNLOAD "${ARCHIVE_URL}" "${archive}.part"
            EXPECTED_HASH "${ARCHIVE_HASH}" TLS_VERIFY ON STATUS status)
        list(GET status 0 code)
        if(NOT code EQUAL 0)
            file(REMOVE "${archive}.part")
            list(GET status 1 reason)
            message(FATAL_ERROR
                "Cannot download ${ARCHIVE_URL}: ${reason}\n"
                "Place ${ARCHIVE_FILE} (${ARCHIVE_HASH}) into ${R_THIRD_PARTY_DIR} and configure again.")
        endif()
        file(RENAME "${archive}.part" "${archive}")
    endif()

    file(REMOVE_RECURSE "${root}")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${root}")
    file(WRITE "${stamp}" "${ARCHIVE_HASH}")
    set(${result} "${root}" PARENT_SCOPE)
endfunction()
