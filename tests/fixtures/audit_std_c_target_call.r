module audit.std_c_target_call;

i32 main() {
    std.c::target_info info = std.c::target();
    if (info.pointer_bits == 0) {
        return 1;
    }
    return 0;
}
