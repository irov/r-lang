module test.audit.std_bytes_append_distinct_source;

i32 main() {
    try {
        bytes value = {};
        u8[1] source = {17};
        std.bytes::append(&value, &source);
        if (len(value) != 1 || source[0] != 17) {
            return 1;
        }
        return 0;
    } catch (std.alloc::alloc_error failure) {
        return 2;
    }
}
