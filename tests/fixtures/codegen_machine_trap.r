module test.codegen.machine_trap;

/* Library R-LIB-0028 (B11): core::trap ends the process at once with the trap instruction of the
   target (SIGTRAP on arm64-apple-darwin): no panic report, no drops, no exit status. */

u32 checked(u32 value) {
    if (value != 7u32) {
        core::trap();
    }
    return value;
}

i32 main(const str[] args) {
    if (checked(len(args) as u32) == 7u32) {
        return 0;
    }
    return 1;
}
