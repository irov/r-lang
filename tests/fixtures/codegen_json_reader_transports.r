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
            const u8[] text = entry.text;
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

// The kind of a reader run named by a program argument: 1 reads the malformed file, 2 the
// well-formed file and 3 a TCP stream whose peer has sent the document and closed its side.
u8 run_kind(str argument) {
    switch (argument) {
    case "malformed": return 1u8;
    case "file": return 2u8;
    case "tcp": return 3u8;
    default: return 0u8;
    }
}

// The outcomes a reader run may have when one allocation fails.
bool allowed(u8 kind, i32 result) {
    if (kind == 1u8) { return result == 98 || result == 99 || result == 97; }
    return result == 0 || result == 99 || result == 97;
}

std.fs::path document_path(u8 kind, str malformed, str document) throws std.fs::path_error {
    if (kind == 1u8) { return std.fs::path_from_utf8(malformed); }
    return std.fs::path_from_utf8(document);
}

// Arguments: the program, a malformed and a well-formed document file, then one argument per
// reader run, "malformed", "file" or "tcp", in the order of the C wrapper's failure plan. The
// arguments are copied before the first suspension (R-BORROW-0024). Main starts no task other
// than the readers: the C wrapper injects one allocation failure from the start of each reader
// to the end of its await. A start failure (97) leaves the source owned here, dropped once.
async i32 main(const str[] args) {
    if (len(args) < 3usize) { return 90; }
    try {
        std.string::string malformed_name = std.string::from_str(args[1]);
        std.string::string document_name = std.string::from_str(args[2]);
        bytes plan = {};
        for (usize index = 3usize; index < len(args); index += 1usize) {
            std.bytes::append_u8(&plan, run_kind(args[index]));
        }
        std.fs::open_file_options reading = std.fs::open_file_options {
            .access = std.fs::access::read, .create = std.fs::create_mode::existing,
            .truncate = false, .append = false, .follow_final_symlink = false };
        std.net::socket_address local = std.net::socket_address {
            .address = std.net::parse_ip("127.0.0.1"), .port = 0u16, .scope_id = 0u32 };
        std.net::listen_options listening = std.net::listen_options {
            .backlog = 1u32, .reuse_address = true, .v6_only = false };
        i32 status = 0;
        for (usize run = 0usize; run < len(plan); run += 1usize) {
            u8 kind = plan[run];
            if (kind == 0u8) { status = 91; break; }
            // 97 stays the outcome when the reader task cannot start.
            i32 result = 97;
            if (kind == 3u8) {
                std.net::tcp_listener listener = await std.net::tcp_listen(local, listening, o::none);
                std.net::socket_address bound = std.net::tcp_listener_local_address(&listener);
                std.net::tcp_stream source = await std.net::tcp_connect(bound, o::none);
                std.net::tcp_connection accepted = await std.net::tcp_accept(&listener, o::none);
                await std.net::tcp_listener_close(move listener, o::none);
                std.string::string text = std.string::from_str(
                    "[{\"id\":\"1\",\"text\":\"first\"},{\"id\":\"2\",\"text\":\"second\"}]");
                bytes document = (move text).into_bytes();
                std.net::tcp_write_all_result written =
                    await std.net::tcp_write_all(&accepted.stream, move document, o::none);
                // A short write leaves the reader a truncated document, which fails the run.
                drop written;
                await std.net::tcp_shutdown(&accepted.stream, std.net::shutdown_direction::write,
                    o::none);
                drop accepted;
                try {
                    result = await ab_tcp(move source);
                } catch (std.async::start_error error) {
                    error as void;
                    drop source;
                }
            } else {
                std.fs::path path = document_path(kind, malformed_name, document_name);
                std.fs::file source = await std.fs::open_file(&path, reading, o::none);
                try {
                    result = await aa_file(move source);
                } catch (std.async::start_error error) {
                    error as void;
                    drop source;
                }
            }
            if (allowed(kind, result) == false) { status = 100 + (kind as i32); break; }
        }
        drop malformed_name;
        drop document_name;
        return status;
    } catch (std.fs::fs_error error) { return 92; }
      catch (std.fs::path_error error) { return 93; }
      catch (std.net::net_error error) { return 94; }
      catch (std.net::address_error error) { return 94; }
      catch (std.alloc::alloc_error error) { return 96; }
      catch (std.async::start_error error) { return 95; }
}
