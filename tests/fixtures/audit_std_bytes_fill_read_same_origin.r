module test.audit.std_bytes_fill_read_same_origin;

i32 main() {
    u8[2] value = {17, 23};
    std.bytes::fill(&value, value[0]);
    return 0;
}
