module example.runner.pipes;
import example.calculator.common::{Usage};

// Both collectors run concurrently; neither pipe must wait for the other to drain.
protected async std.string::string read_pipe(o<std.io::input> endpoint)
    throws Usage, std.io::io_error, std.alloc::alloc_error, std.array::push_error<u8>,
           std.string::string_error, std.async::start_error {
    switch (move endpoint) {
    case variant o::none:
        return std.string::create();
    case variant o::some(move stream):
        bytes collected = std.array::create::<u8>();
        bool complete = false;
        while (complete == false) {
            bytes chunk = std.alloc::bytes(4096usize, 0u8);
            std.io::read_result result = await stream.read(move chunk);
            switch (move result) {
            case variant std.io::read_result::read(move part):
                usize count = part.count;
                usize total = len(collected) + count;
                throw (total > 1048576usize) Usage { .message = "captured output exceeds 1 MiB" };
                const u8[] chunk_view = std.array::as_slice(&part.buffer);
                for (usize index = 0usize; index < count; index += 1usize) {
                    u8 byte = chunk_view[index];
                    collected.push(byte);
                }
                break;
            case variant std.io::read_result::end(move returned):
                complete = true; break;
            case variant std.io::read_result::failed(move failure):
                throw failure.error;
            }
        }
        await (move stream).close();
        std.string::string text = std.string::from_utf8(collected);
        return move text;
    }
}

async void close_stdin(o<std.io::output> endpoint)
    throws std.io::io_error, std.async::start_error {
    switch (move endpoint) {
    case variant o::none: return;
    case variant o::some(move stream):
        await (move stream).close();
        return;
    }
}

struct Capture {
    std.string::string text;
    i32 failure;
};

// A completion record lets the caller join both readers before reporting either failure.
async Capture collect(o<std.io::input> endpoint) {
    try {
        std.string::string text = await read_pipe(move endpoint);
        return Capture { .text = move text, .failure = 0 };
    } catch (Usage failure) { return Capture { .text = std.string::create(), .failure = 64 }; }
    catch (std.io::io_error failure) { return Capture { .text = std.string::create(), .failure = 74 }; }
    catch (std.alloc::alloc_error failure) { return Capture { .text = std.string::create(), .failure = 71 }; }
    catch (std.array::push_error<u8> failure) { return Capture { .text = std.string::create(), .failure = 71 }; }
    catch (std.string::string_error failure) { return Capture { .text = std.string::create(), .failure = 65 }; }
    catch (std.async::start_error failure) { return Capture { .text = std.string::create(), .failure = 75 }; }
}
