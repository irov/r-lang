module test.codegen.async_dynamic_loading;

import std.dl;

/* R-FFI-0063, Library R-SLIB-DL-0004 (B9.3): a function obtained from an opened library held in
   an async frame across awaits and called there. */
@link(name = "system.loader", kind = "loadable")
@header("stdlib.h")
extern "C" {
    @safety("LOADER-ABS", "value is not the most negative c_int")
    c_int abs(c_int value);
}

protected async c_int negate(c_int value) {
    return (0i32 as c_int) - value;
}

protected async i32 run() throws std.async::start_error, std.dl::dl_error {
    std.dl::library system = std.dl::open("libSystem.B.dylib");
    /* The library is Send+Sync and lives across the await; a raw function pointer is not. */
    c_int value = await negate(17i32 as c_int);
    raw fn?(c_int) -> c_int absolute = std.dl::resolve(&system, abs);
    unsafe {
        if (absolute == null) {
            value as void;
            return 1;
        }
        if (absolute(value) != 17i32 as c_int) {
            return 2;
        }
    }
    return 0;
}

async i32 main() {
    try {
        return await run();
    } catch (std.async::start_error failure) {
        failure as void;
        return 9;
    } catch (std.dl::dl_error failure) {
        failure as void;
        return 8;
    }
}
