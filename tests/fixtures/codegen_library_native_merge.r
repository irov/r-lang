module test.codegen.library_native_merge;
import std.tls;

/* Library R-SLIB-RSRC-0003 (M25): a program with a link manifest of its own also uses std.tls,
   whose native provider comes from the link manifest of the library; the two manifests are
   resolved together, and the verifier and the bridge cover the imports of both. */
@link(name = "system.libc", kind = "system")
@header("unistd.h")
extern "C" {
    @safety("MERGE-USLEEP", "usleep only suspends the calling thread for the given time")
    c_int usleep(c_uint microseconds);
}

i32 main() {
    unsafe {
        c_int slept = usleep(1u32 as c_uint);
        if ((slept as i32) != 0) { return 1; }
    }
    try {
        std.tls::config settings = std.tls::client_config();
        settings.add_authority("not a certificate");
        return 2;
    } catch (std.tls::tls_error failure) {
        if (failure.code != std.tls::error_code::invalid_certificate) { return 3; }
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 4;
    }
    return 0;
}
