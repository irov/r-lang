module ffi.object_reserved_no_header;

/* R-FFI-0057: a reserved C identifier is declared only by its header, which the accessor bridge
   includes. */
@link(name = "probe", kind = "static")
@abi("probe-abi")
extern "C" {
    c_int total_count;
}

i32 main() {
    return 0;
}
