module ffi.verify_decorated;

// B9-1 (R-CMAP-0026): the header of realpath binds it to realpath$DARWIN_EXTSN, a symbol an
// import cannot name; the program supplies a shim with the default C ABI instead.
@link(name = "system.libc", kind = "system")
@header("stdlib.h")
extern "C" {
    @safety("REALPATH", "path shall address a NUL-terminated string")
    raw c_char* realpath(raw const c_char* path, raw c_char* resolved);
}

i32 main() {
    return 0;
}
