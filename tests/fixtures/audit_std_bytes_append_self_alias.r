module test.audit.std_bytes_append_self_alias;

i32 main() {
    try {
        bytes value = std.alloc::bytes(1048576, 17);
        std.bytes::append(&value, value);
        if (len(value) != 2097152) {
            return 1;
        }
        return 0;
    } catch (std.alloc::alloc_error failure) {
        return 2;
    }
}
