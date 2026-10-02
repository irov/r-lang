add_library(r_library_internal_headers INTERFACE)
target_include_directories(r_library_internal_headers INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/diagnostics/include"
)
