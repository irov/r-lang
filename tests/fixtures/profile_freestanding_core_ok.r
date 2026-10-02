module profile.freestanding_core_ok;

i32 main() {
    u32 wrapped = core::wrapping_add_u32(4294967295u32, 1u32);
    i32[2] values = {41, 1};
    const i32[] view = &values;
    i32 sum = view[0] + view[1];
    if (wrapped != 0u32) {
        return 1;
    }
    return sum - 42;
}
