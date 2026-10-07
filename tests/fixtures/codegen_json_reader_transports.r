module test.codegen.json_reader_transports;
struct Entry { @json(string) u64 id; std.string::string text; };
async i32 aa_file(std.fs::file source) {
    i32 trace = 0;
    try {
      try {
        std.json::reader<std.fs::file> reader = std.json::new_reader(move source);
        o<Entry> decoded = await std.json::read_next(&reader, o::none);
        switch (move decoded) {
        case variant o::some(move entry):
            if (entry.id != 18446744073709551615) { return 1; }
            // The C wrapper names generated functions by number; the explicit call keeps them.
            const u8[] text = std.string::as_bytes(&entry.text);
            if (len(text) != 5) { return 1; }
            break;
        case variant o::none: return 2;
        }
        o<Entry> end = await std.json::read_next(&reader, o::none);
        switch (move end) {
        case variant o::some(move entry): return 3;
        case variant o::none: return 0;
        }
      } finally { trace += 1; }
    } catch (std.json::error error) { if (trace != 1) { return 95; } return 98; }
      catch (std.alloc::alloc_error error) { if (trace != 1) { return 95; } return 99; }
      catch (std.io::io_error error) { if (trace != 1) { return 95; } return 96; }
      catch (std.async::start_error error) { if (trace != 1) { return 95; } return 97; }
}
async i32 ab_tcp(std.net::tcp_stream source) {
    try {
        std.json::options options = { .max_depth = 256, .max_value_bytes = 1048576, .indent = 0,
            .reject_unknown_fields = false, .ignore_case = false, .mode = std.json::mode::array_elements };
        std.json::reader<std.net::tcp_stream> reader = std.json::new_reader(move source, options);
        u64 count = 0;
        while (true) {
            o<Entry> next = await std.json::read_next(&reader, o::none);
            switch (move next) {
            case variant o::some(move entry):
                count += 1;
                if (entry.id != count) { return 4; }
                break;
            case variant o::none:
                if (count != 2) { return 5; }
                return 0;
            }
        }
    } catch (std.json::error error) { return 98; }
      catch (std.alloc::alloc_error error) { return 99; }
      catch (std.net::net_error error) { return 96; }
      catch (std.async::start_error error) { return 97; }
}
i32 main() { return 0; }
