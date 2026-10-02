module bench.string_append;

/* Append an eight-byte literal 20 000 000 times, clearing the string every 4096 appends, and
   fold the observed lengths. */
i32 main() {
    std.string::string text = std.string::create();
    usize iterations = 20_000_000usize;
    usize total = 0usize;
    for (usize index = 0usize; index < iterations; index += 1usize) {
        std.string::append_str(&text, "abcdefgh");
        total += std.string::len(&text);
        if ((index & 4095usize) == 4095usize) { std.string::clear(&text); }
    }
    drop text;
    return (total % 109usize) as i32;
}
