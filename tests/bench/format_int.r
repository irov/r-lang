module bench.format_int;

/* Render 5 000 000 integers through a formatted literal and fold the rendered lengths. */
i32 main() {
    usize iterations = 5_000_000usize;
    usize total = 0usize;
    for (usize index = 0usize; index < iterations; index += 1usize) {
        std.string::string text = f"{index}";
        const u8[] rendered = text;
        total += len(rendered);
    }
    return (total % 109usize) as i32;
}
