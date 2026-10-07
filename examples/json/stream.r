module example.json.stream;

struct Entry { u64 id; std.string::string name; };

async i32 example() throws std.json::error, std.alloc::alloc_error {
    std.json::options options = {
        .max_depth = 256, .max_value_bytes = 1024, .indent = 0,
        .reject_unknown_fields = false, .ignore_case = false,
        .mode = std.json::mode::array_elements,
    };
    std.json::decoder<Entry> decoder = std.json::new_decoder(options);
    std.string::string input = std.string::from_str(
        "[{\"id\":1,\"name\":\"Madrid\"},{\"id\":2,\"name\":\"\\uD83D\\uDE00\"}]");
    const u8[] source = input;
    usize offset = 0;
    u64 sum = 0;
    while (true) {
        usize end = offset + 3;
        if (end > len(source)) { end = len(source); }
        bool final = end == len(source);
        // The decoder retains no borrow of this fragment, even inside an escape.
        std.json::feed_result progress = decoder.feed(source[offset..end], final);
        offset += progress.consumed;
        if (progress.state == std.json::feed_state::value_ready) {
            Entry entry = decoder.take();
            sum += entry.id;
            // The entry is destroyed here; the entire root array is never retained.
        } else {
            if (progress.state == std.json::feed_state::end) { break; }
        }
    }
    if (sum != 3 || offset != len(source)) { return 1; }
    return 0;
}

async i32 main() {
    try { i32 result = await example(); return result; }
    catch (std.json::error failure) { return 98; }
}
