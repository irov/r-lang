module example.xml.main;

import std.xml;

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string diagnostic; i32 status; };
struct CommandOptions { usize chunk; std.string::string pattern; };

/* Stream XML from stdin with std.xml: print the event stream, select elements with a
   streaming path pattern, or rewrite the document through the escaping writer. */

error Usage { constexpr str message; };
error TooLarge { };

enum Command { events, events_all, select, compact, pretty };

const usize INPUT_LIMIT = 16777216usize;

async bytes read_input()
    throws std.io::io_error, std.alloc::alloc_error, std.async::start_error, TooLarge {
    std.io::input input = std.io::stdin();
    bytes data = {};
    while (true) {
        bytes buffer = std.alloc::bytes(65536usize, 0u8);
        std.io::read_result result = await input.read(move buffer);
        switch (move result) {
        case variant std.io::read_result::read(move part):
            throw (len(data) + part.count > INPUT_LIMIT) TooLarge { };
            const u8[] view = std.array::as_slice(&part.buffer);
            std.array::reserve(&data, part.count);
            for (usize index = 0usize; index < part.count; index += 1usize) {
                std.bytes::append_u8(&data, view[index]);
            }
            break;
        case variant std.io::read_result::end(move returned):
            await (move input).close();
            return move data;
        case variant std.io::read_result::failed(move failure): throw failure.error;
        }
    }
}

async void write_output(bytes data)
    throws std.io::io_error, std.alloc::alloc_error, std.async::start_error {
    std.io::output output = std.io::stdout();
    while (len(data) != 0usize) {
        std.io::write_result result = await output.write(move data);
        switch (move result) {
        case variant std.io::write_result::written(move part):
            bytes remaining = {};
            const u8[] view = std.array::as_slice(&part.buffer);
            for (usize index = part.count; index < len(view); index += 1usize) {
                std.bytes::append_u8(&remaining, view[index]);
            }
            data = move remaining;
            break;
        case variant std.io::write_result::failed(move failure): throw failure.error;
        }
    }
    await output.flush();
    await (move output).close();
}

async void report(std.string::string message) throws std.io::io_error, std.async::start_error {
    std.io::output output = std.io::stderr();
    bytes data = (move message).into_bytes();
    std.io::write_all_result result = await output.write_all(move data);
    switch (move result) {
    case variant std.io::write_all_result::written(move returned): return;
    case variant std.io::write_all_result::failed(move failure): throw failure.error;
    }
}

usize parse_count(str text) throws Usage {
    try {
        usize value = std.convert::parse_usize(text, 10u32);
        throw (value == 0usize) Usage { .message = "the chunk size shall be positive" };
        return value;
    } catch (std.convert::parse_error failure) {
        throw Usage { .message = "the chunk size shall be a decimal integer" };
    }
}

/* Appends text with tab, LF, CR and backslash spelled as escapes so that one event is one line. */
void append_escaped(bytes* target, str value) throws std.alloc::alloc_error {
    const u8[] source = value;
    for (usize index = 0usize; index < len(source); index += 1usize) {
        u8 byte = source[index];
        u8 escaped = 0;
        switch (byte) {
        case 9: escaped = 116; break;
        case 10: escaped = 110; break;
        case 13: escaped = 114; break;
        case 92: escaped = 92; break;
        default: break;
        }
        if (escaped != 0) {
            std.bytes::append_u8(target, 92);
            std.bytes::append_u8(target, escaped);
        } else {
            std.bytes::append_u8(target, byte);
        }
    }
}

void append_str(bytes* target, str value) throws std.alloc::alloc_error {
    const u8[] source = value;
    std.array::reserve(target, len(source));
    for (usize index = 0usize; index < len(source); index += 1usize) { std.bytes::append_u8(target, source[index]); }
}

/* Appends the attributes of the current event as tab-separated name=value pairs. */
void append_attributes(bytes* target, const std.xml::reader* parser) throws std.alloc::alloc_error {
    for (usize index = 0usize; index < parser->attribute_count(); index += 1usize) {
        std.bytes::append_u8(target, 9);
        append_escaped(target, parser->attribute_name(index));
        std.bytes::append_u8(target, 61);
        append_escaped(target, parser->attribute_value(index));
    }
}

/* Feeds the document in chunks until the next event; none at the end of the document. */
std.xml::event_kind next_event(std.xml::reader* parser, const u8[] document, usize* fed, usize chunk)
    throws std.xml::error, std.alloc::alloc_error {
    while (true) {
        usize stop = *fed + chunk;
        if (stop > len(document)) { stop = len(document); }
        const u8[] piece = document[*fed..stop];
        std.xml::progress step = parser->feed(piece, stop == len(document));
        *fed += step.consumed;
        if (step.state == std.xml::state::event_ready) { return parser->kind(); }
        if (step.state == std.xml::state::end) { return std.xml::event_kind::none; }
    }
}

/* One line per event: kind, depth, name, text and attributes separated by tabs. */
bytes list_events(const u8[] document, usize chunk, bool all)
    throws std.xml::error, std.alloc::alloc_error {
    std.xml::options settings = std.xml::default_options();
    settings.skip_whitespace = all == false;
    std.xml::reader parser = std.xml::reader::create(settings);
    bytes output = {};
    usize fed = 0usize;
    while (true) {
        std.xml::event_kind kind = next_event(&parser, document, &fed, chunk);
        if (kind == std.xml::event_kind::none) { return move output; }
        append_str(&output, core::enum_name(kind));
        std.bytes::append_u8(&output, 9);
        usize depth = parser.depth();
        std.string::string depth_text = f"{depth}";
        append_str(&output, depth_text);
        std.bytes::append_u8(&output, 9);
        append_escaped(&output, parser.name());
        std.bytes::append_u8(&output, 9);
        if (kind != std.xml::event_kind::declaration) { append_escaped(&output, parser.text()); }
        append_attributes(&output, &parser);
        std.bytes::append_u8(&output, 10);
    }
}

/* One line for a selected element: ordinal, depth, path, local name, namespace, attributes. */
void append_selected(bytes* target, const std.xml::reader* parser, usize ordinal) throws std.alloc::alloc_error {
    usize depth = parser->depth();
    std.string::string line = f"{ordinal}\t{depth}\t";
    append_str(target, line);
    for (usize level = 0usize; level < depth; level += 1usize) {
        std.bytes::append_u8(target, 47);
        append_escaped(target, parser->path_name(level));
    }
    std.bytes::append_u8(target, 9);
    append_escaped(target, parser->local_name());
    std.bytes::append_u8(target, 9);
    append_escaped(target, parser->namespace());
    append_attributes(target, parser);
    std.bytes::append_u8(target, 10);
}

/* One line per selected element. */
bytes select_elements(const u8[] document, str pattern, usize chunk)
    throws std.xml::error, std.alloc::alloc_error {
    std.xml::selector chosen = std.xml::selector::compile(pattern);
    const std.xml::selector* selector = &chosen;
    std.xml::reader parser = std.xml::reader::create(std.xml::default_options());
    bytes output = {};
    usize fed = 0usize;
    usize ordinal = 0usize;
    while (true) {
        std.xml::event_kind kind = next_event(&parser, document, &fed, chunk);
        if (kind == std.xml::event_kind::none) { return move output; }
        if (kind == std.xml::event_kind::start_element) { ordinal += 1usize; }
        if (selector->matches(&parser) == true) { append_selected(&output, &parser, ordinal); }
    }
}

/* Rewrites the document through the writer, dropping the declaration and whitespace. */
bytes rewrite(const u8[] document, bool pretty) throws std.xml::error, std.alloc::alloc_error {
    std.xml::reader parser = std.xml::reader::create(std.xml::default_options());
    std.xml::writer out = std.xml::writer::create(pretty);
    out.declaration();
    usize fed = 0usize;
    while (true) {
        std.xml::event_kind kind = next_event(&parser, document, &fed, 4096usize);
        switch (kind) {
        case std.xml::event_kind::none:
            array<u8> produced = (move out).finish();
            return move produced;
        case std.xml::event_kind::start_element:
            out.start(parser.name());
            for (usize index = 0usize; index < parser.attribute_count(); index += 1usize) {
                out.attribute(parser.attribute_name(index), parser.attribute_value(index));
            }
            break;
        case std.xml::event_kind::end_element: out.end(); break;
        case std.xml::event_kind::text: out.text(parser.text()); break;
        case std.xml::event_kind::cdata: out.cdata(parser.text()); break;
        case std.xml::event_kind::comment: out.comment(parser.text()); break;
        case std.xml::event_kind::instruction: out.instruction(parser.name(), parser.text()); break;
        case std.xml::event_kind::declaration: break;
        }
    }
}

async i32 main(const str[] arguments) {
    CommandResponse response = {.diagnostic = std.string::create(), .status = 0};

    try {
        if (len(arguments) == 1usize) {
            std.string::string help = std.string::from_str(
                "xml events [CHUNK] < document        one event per line, whitespace text dropped\n"
                "xml events_all [CHUNK] < document    the same with whitespace text\n"
                "xml select PATTERN [CHUNK] < document   elements on the path pattern\n"
                "xml compact < document               rewrite on one line\n"
                "xml pretty < document                rewrite indented\n");
            await report(move help);
            return 0;
        }
        o<Command> selected = core::enum_from_name::<Command>(arguments[1]);
        Command command = Command::events;
        switch (selected) {
        case variant o::some(value): command = *value; break;
        case variant o::none: throw Usage { .message = "unknown command" };
        }
        /* Every argument is read before the first suspension (Core R-BORROW-0024). */
        CommandOptions options = {.chunk = 4096usize, .pattern = std.string::create()};
        if (command == Command::select) {
            throw (len(arguments) < 3usize) Usage { .message = "select needs a pattern" };
            options.pattern = std.string::from_str(arguments[2]);
            if (len(arguments) > 3usize) { options.chunk = parse_count(arguments[3]); }
        } else {
            if (len(arguments) > 2usize) { options.chunk = parse_count(arguments[2]); }
        }
        bytes input = await read_input();
        const u8[] input_view = std.array::as_slice(&input);
        switch (command) {
        case Command::events:
            bytes listed = list_events(input_view, options.chunk, false);
            await write_output(move listed);
            break;
        case Command::events_all:
            bytes listed = list_events(input_view, options.chunk, true);
            await write_output(move listed);
            break;
        case Command::select:
            bytes chosen = select_elements(input_view, options.pattern, options.chunk);
            await write_output(move chosen);
            break;
        case Command::compact:
            bytes flat = rewrite(input_view, false);
            await write_output(move flat);
            break;
        case Command::pretty:
            bytes shaped = rewrite(input_view, true);
            await write_output(move shaped);
            break;
        }
        return 0;
    } catch (Usage failure) { response.diagnostic = f"{failure.message}\n"; response.status = 64; }
    catch (std.xml::error failure) {
        constexpr str name = core::enum_name(failure.code);
        response.diagnostic = f"xml error: {name} at {failure.offset}\n";
        response.status = 65;
    }
    catch (TooLarge failure) { response.diagnostic = std.string::from_str("input exceeds 16 MiB\n"); response.status = 65; }
    catch (std.io::io_error failure) {
        std.io::error_code code = failure.code;
        if (code == std.io::error_code::broken_pipe) { return 74; }
        std.error::error error = failure.as_error();
        response.diagnostic = error.diagnostic();
        response.status = 74;
    }
    await report(core::replace(&response.diagnostic, std.string::create()));
    return response.status;
}

