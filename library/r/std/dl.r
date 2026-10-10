module std.dl;

/* R-SLIB-DL-0001: shared libraries the program opens while it runs, over the native provider
   std.dl.native, and the functions of a `loadable` extern "C" block obtained from them (Core
   R-FFI-0063). An opened library stays loaded until the program ends, so every address obtained
   from it stays valid. */

@link(name = "std.dl.native", kind = "static")
@header("r_std_dl_native.h")
extern "C" {
    @safety("DL-NATIVE-OPEN", "path addresses length bytes and handle is writable")
    c_int32 r_std_dl_native_open(raw const c_uint8*? path, c_size length, raw c_uint64* handle);

    @safety("DL-NATIVE-SYMBOL", "handle comes from r_std_dl_native_open and name addresses length bytes")
    raw void*? r_std_dl_native_symbol(c_uint64 handle, raw const c_uint8*? name, c_size length);
}

/* R-SLIB-DL-0001: why a library was not opened. */
enum error_code { not_loaded, invalid_path };

error dl_error { error_code code; };

/* R-SLIB-DL-0002: an opened library; copies name the same library. The provider's token of the
   library is a number: the library is never unloaded and the loader is thread-safe, so the
   struct is Send+Sync (Core R-MEM-0004). */
struct library {
    protected c_uint64 handle;
};

protected raw const c_uint8*? bytes_of(const u8[] data) {
    if (len(data) == 0usize) { return null; }
    unsafe {
        raw const u8* first = &data[0usize] as raw const u8*;
        raw const void* erased = first as raw const void*;
        return erased as raw const c_uint8*;
    }
}

/* R-SLIB-DL-0002: opens the shared library at path, or the one the dynamic loader finds by that
   name when it holds no slash, with every symbol bound now. */
library open(str path) throws dl_error {
    c_uint64 handle = 0u64 as c_uint64;
    unsafe {
        raw c_uint64* handle_out = &handle as raw c_uint64*;
        c_int32 status = r_std_dl_native_open(bytes_of(path), len(path) as c_size, handle_out);
        throw (status as i32 < 0i32) dl_error {.code = error_code::invalid_path};
        throw (status as i32 == 0i32) dl_error {.code = error_code::not_loaded};
    }
    return library {.handle = handle};
}

/* R-SLIB-DL-0003: the address of the symbol name in the opened library, null when it defines
   none. Using the address is the caller's unsafe conversion; std.dl::resolve obtains a function
   of a loadable block with its verified prototype instead (R-SLIB-DL-0004). */
raw void*? symbol(const library* opened, str name) {
    unsafe {
        return r_std_dl_native_symbol(opened->handle, bytes_of(name), len(name) as c_size);
    }
}
