module example.deflate.main;

import std.deflate;

/* Stream DEFLATE, zlib and gzip data between stdin and stdout with std.deflate: chunked
   steps of the resumable coders, one-shot helpers, sync flushes and stream statistics. */

error Usage { constexpr str message; };
error TooLarge { };
error Trailing { usize offset; };

enum Command { deflate, deflate_sync, inflate, pack, unpack, stats };

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

// A single write may make partial progress; the unwritten suffix is retried.
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

std.deflate::format parse_format(str name) throws Usage {
    o<std.deflate::format> selected = core::enum_from_name::<std.deflate::format>(name);
    switch (selected) {
    case variant o::some(value): return *value;
    case variant o::none: throw Usage { .message = "format is rfc1951, rfc1950 or rfc1952" };
    }
}

usize parse_count(str text) throws Usage {
    try {
        usize value = std.convert::parse_usize(text, 10u32);
        throw (value == 0usize) Usage { .message = "counts shall be positive" };
        return value;
    } catch (std.convert::parse_error failure) {
        throw Usage { .message = "counts shall be decimal integers" };
    }
}

u8 parse_level(str text) throws Usage {
    try {
        u8 level = std.convert::parse_u8(text, 10u32);
        return level;
    } catch (std.convert::parse_error failure) {
        throw Usage { .message = "the level shall be a decimal integer from 0 to 9" };
    }
}

void append_produced(bytes* target, const u8[] source, usize count) throws std.alloc::alloc_error {
    std.array::reserve(target, count);
    for (usize index = 0usize; index < count; index += 1usize) {
        std.bytes::append_u8(target, source[index]);
    }
}

/* Feeds the input in chunks, draining the coder into a chunk-sized scratch buffer; sync
   flushes every chunk when requested and finishes on the last one. */
bytes compress_stream(const u8[] input, std.deflate::format form, u8 level, usize window,
                      usize chunk, bool sync)
    throws std.deflate::error, std.alloc::alloc_error {
    std.deflate::deflater packer = std.deflate::deflater::create(form, level, window);
    bytes output = {};
    array<u8> scratch = std.alloc::bytes(chunk, 0u8);
    usize fed = 0usize;
    while (true) {
        usize next = fed + chunk;
        if (next > len(input)) { next = len(input); }
        bool last = next == len(input);
        std.deflate::flush mode = std.deflate::flush::none;
        if (sync == true) { mode = std.deflate::flush::sync; }
        if (last == true) { mode = std.deflate::flush::finish; }
        const u8[] piece = input[fed..next];
        usize taken = 0usize;
        while (true) {
            u8[] out_view = std.array::as_slice_mut(&scratch);
            const u8[] rest = piece[taken..len(piece)];
            std.deflate::progress step = packer.deflate(rest, out_view, mode);
            taken += step.consumed;
            append_produced(&output, out_view, step.produced);
            if (step.state == std.deflate::state::end) { return move output; }
            if (step.state == std.deflate::state::need_input) { break; }
        }
        fed = next;
    }
}

struct Stats {
    usize steps;
    usize consumed;
    usize produced;
};

/* Decodes with chunked input and output; input after the end of the stream is an error. */
bytes decompress_stream(const u8[] input, std.deflate::format form, usize limit, usize window,
                        usize chunk, Stats* stats)
    throws std.deflate::error, std.alloc::alloc_error, Trailing {
    std.deflate::inflater unpacker = std.deflate::inflater::create(form, window, limit);
    bytes result = {};
    array<u8> scratch = std.alloc::bytes(chunk, 0u8);
    usize fed = 0usize;
    while (true) {
        usize next = fed + chunk;
        if (next > len(input)) { next = len(input); }
        const u8[] piece = input[fed..next];
        usize taken = 0usize;
        bool ended = false;
        while (true) {
            u8[] out_view = std.array::as_slice_mut(&scratch);
            const u8[] rest = piece[taken..len(piece)];
            std.deflate::progress step = unpacker.inflate(rest, out_view);
            stats->steps += 1usize;
            taken += step.consumed;
            append_produced(&result, out_view, step.produced);
            if (step.state == std.deflate::state::end) { ended = true; }
            if (step.state == std.deflate::state::need_input || ended == true) { break; }
        }
        fed += taken;
        if (ended == true) {
            throw (fed != len(input)) Trailing { .offset = fed };
            stats->consumed = unpacker.consumed_bytes();
            stats->produced = unpacker.produced_bytes();
            /* A reset coder decodes the same stream again to the same size. */
            unpacker.reset();
            array<u8> replay = std.alloc::bytes(len(result) + 1usize, 0u8);
            u8[] replay_view = std.array::as_slice_mut(&replay);
            std.deflate::progress whole = unpacker.inflate(input, replay_view);
            throw (whole.produced != stats->produced) std.deflate::error {
                .code = std.deflate::error_code::corrupt_stream, .offset = whole.consumed };
            return move result;
        }
        throw (fed >= len(input)) std.deflate::error {
            .code = std.deflate::error_code::corrupt_stream, .offset = fed };
    }
}

struct CommandResponse { std.string::string diagnostic; i32 status; };
struct CommandOptions { u8 level; usize limit; usize window; usize chunk; };

async i32 main(const str[] arguments) {
    CommandResponse response = {.diagnostic = std.string::create(), .status = 0};
    try {
        if (len(arguments) == 1usize) {
            std.string::string help = std.string::from_str(
                "deflate deflate|deflate_sync FORMAT LEVEL [WINDOW] [CHUNK] < plain > packed\n"
                "deflate inflate FORMAT [LIMIT] [WINDOW] [CHUNK] < packed > plain\n"
                "deflate pack FORMAT LEVEL < plain > packed\n"
                "deflate unpack FORMAT [LIMIT] < packed > plain\n"
                "deflate stats FORMAT [LIMIT] [WINDOW] [CHUNK] < packed\n"
                "FORMAT is rfc1951 (raw DEFLATE), rfc1950 (zlib) or rfc1952 (gzip).\n");
            await report(move help);
            return 0;
        }
        o<Command> selected = core::enum_from_name::<Command>(arguments[1]);
        Command command = Command::deflate;
        switch (selected) {
        case variant o::some(value): command = *value; break;
        case variant o::none: throw Usage { .message = "unknown command" };
        }
        throw (len(arguments) < 3usize) Usage { .message = "a format is required" };
        std.deflate::format form = parse_format(arguments[2]);
        /* Every argument is read before the first suspension (Core R-BORROW-0024). */
        bool needs_level = (command == Command::deflate) || (command == Command::deflate_sync) ||
                           (command == Command::pack);
        CommandOptions options = {.level = 0, .limit = INPUT_LIMIT, .window = 32768usize, .chunk = 4096usize};
        if (needs_level == true) {
            throw (len(arguments) < 4usize) Usage { .message = "a compression level is required" };
            options.level = parse_level(arguments[3]);
        }
        if ((needs_level == false) && (len(arguments) > 3usize)) { options.limit = parse_count(arguments[3]); }
        if (len(arguments) > 4usize) { options.window = parse_count(arguments[4]); }
        if (len(arguments) > 5usize) { options.chunk = parse_count(arguments[5]); }
        bytes input = await read_input();
        const u8[] input_view = std.array::as_slice(&input);
        switch (command) {
        case Command::deflate: fallthrough;
        case Command::deflate_sync:
            bytes packed = compress_stream(input_view, form, options.level, options.window, options.chunk,
                                           command == Command::deflate_sync);
            await write_output(move packed);
            break;
        case Command::inflate:
            Stats stats = Stats { .steps = 0usize, .consumed = 0usize, .produced = 0usize };
            bytes plain = decompress_stream(input_view, form, options.limit, options.window, options.chunk, &stats);
            await write_output(move plain);
            break;
        case Command::pack:
            bytes packed = std.deflate::deflate(input_view, form, options.level);
            await write_output(move packed);
            break;
        case Command::unpack:
            bytes plain = std.deflate::inflate(input_view, form, options.limit);
            await write_output(move plain);
            break;
        case Command::stats:
            Stats stats = Stats { .steps = 0usize, .consumed = 0usize, .produced = 0usize };
            bytes plain = decompress_stream(input_view, form, options.limit, options.window, options.chunk, &stats);
            (move plain) as void;
            std.string::string line = f"consumed={stats.consumed} produced={stats.produced} steps={stats.steps}\n";
            bytes line_bytes = (move line).into_bytes();
            await write_output(move line_bytes);
            break;
        }
        return 0;
    } catch (Usage failure) { response.diagnostic = f"{failure.message}\n"; response.status = 64; }
    catch (std.deflate::error failure) {
        constexpr str name = core::enum_name(failure.code);
        response.diagnostic = f"deflate error: {name} at {failure.offset}\n";
        response.status = 65;
    }
    catch (Trailing failure) { response.diagnostic = f"deflate error: trailing input at {failure.offset}\n"; response.status = 65; }
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
