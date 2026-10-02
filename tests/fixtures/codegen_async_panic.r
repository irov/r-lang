module test.codegen.async_panic;

/* R-ERR-0004 inside an async frame: the explicit panic begins from the frame body. */
async i32 main() {
    i32 value = 1;
    if (value == 1) {
        panic("async stop");
    }
    return 0;
}
