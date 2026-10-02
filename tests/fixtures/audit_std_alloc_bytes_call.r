module audit.std_alloc_bytes_call;

i32 main() {
    try {
        array<u8> value = std.alloc::bytes(4, 0u8);
        drop value;
        return 0;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 1;
    }
}
