module example.json_tool.stream;
import example.json_tool.tree;

struct CommandStorage1 { std.string::string value; };
struct CommandStorage2 { std.string::string value; };

// Decode array elements from fixed-size fragments; no full input tree is built.
std.string::string fragments(str input, usize chunk) throws std.json::error, std.alloc::alloc_error {
    std.json::options options = example.json_tool.tree::options(0u32, false, std.json::mode::array_elements);
    std.json::decoder<std.json::value> decoder = std.json::new_decoder(options);
    const u8[] source = input;
    usize offset = 0usize;
    CommandStorage2 state_report = {.value = std.string::create()};
    bool ended = false;
    while (true) {
        usize end = offset + chunk;
        if (end > len(source)) { end = len(source); }
        bool final = end == len(source);
        std.json::feed_result progress = decoder.feed(source[offset..end], final);
        offset += progress.consumed;
        if (progress.state == std.json::feed_state::value_ready) {
            std.json::value item = decoder.take();
            std.string::string line = item.stringify();
            str text = line;
            state_report.value.append(text);
            state_report.value.append("\n");
        } else { ended = progress.state == std.json::feed_state::end; }
        if (ended == true) { break; }
    }
    return core::replace(&state_report.value, std.string::create());
}

// Read one JSON document from stdin, then preserve all unread bytes for the next consumer.
async std.string::string first() throws std.json::error, std.io::io_error, std.alloc::alloc_error, std.async::start_error {
    std.io::input input = std.io::stdin();
    std.json::options options = example.json_tool.tree::options(0u32, false, std.json::mode::sequence);
    std.json::reader<std.io::input> reader = std.json::new_reader(move input, options);
    CommandStorage1 state_report = {.value = std.string::create()};
    o<std.json::value> next = await reader.read_next();
    switch (move next) {
    case variant o::some(move item): state_report.value = item.stringify(); break;
    case variant o::none: state_report.value = std.string::from_str("end"); break;
    }
    state_report.value.append("\n");
    std.json::detached<std.io::input> detached = (move reader).detach();
    std.io::input remaining_input = detached.take_handle();
    bytes pending = detached.take_bytes();
    bool finished = false;
    while (finished == false) {
        bytes buffer = std.alloc::bytes(4096usize, 0u8);
        std.io::read_result result = await remaining_input.read(move buffer);
        switch (move result) {
        case variant std.io::read_result::read(move part):
            const u8[] source = std.array::as_slice(&part.buffer);
            for (usize index = 0usize; index < part.count; index += 1usize) {
                std.bytes::append_u8(&pending, source[index]);
            }
            break;
        case variant std.io::read_result::end(move returned): finished = true; break;
        case variant std.io::read_result::failed(move failure): throw failure.error;
        }
    }
    // The suffix is binary, so summarize it without requiring UTF-8 after the first document.
    usize suffix_size = len(pending);
    u32 checksum = std.hash::crc32(pending);
    std.string::string suffix = f"remaining={suffix_size} crc32={checksum}\n";
    str text = suffix;
    state_report.value.append(text);
    await (move remaining_input).close();
    return core::replace(&state_report.value, std.string::create());
}
