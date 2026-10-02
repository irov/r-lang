module test.audit.std_bytes_fill_read_distinct_origin;

i32 main() {
    u8[2] value = {17, 23};
    u8 source = 29;
    std.bytes::fill(&value, source);
    if (value[0] != 29 || value[1] != 29 || source != 29) {
        return 1;
    }
    return 0;
}
