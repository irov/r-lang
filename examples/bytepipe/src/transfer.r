module example.bytepipe.transfer;

error InvalidUtf8 { usize offset; };
error TooLarge { };

async bytes read_input()
    throws std.io::io_error, std.alloc::alloc_error, std.async::start_error, TooLarge {
    std.io::input input = std.io::stdin();
    bytes data = {};
    bytes buffer = std.alloc::bytes(4096usize, 0u8);
    // One loaned buffer serves every read; each consuming await returns it for the next one.
    task_scope(1) reader {
        while (true) {
            usize count = await input.read_into(buffer.as_slice_mut());
            if (count == 0usize) { break; }
            throw (len(data) + count > 1048576usize) TooLarge { };
            const u8[] view = buffer.as_slice();
            std.bytes::append(&data, view[0usize..count]);
        }
    }
    await (move input).close();
    return move data;
}

bytes validate(bytes source) throws InvalidUtf8 {
    std.string::from_bytes_result result = std.string::from_bytes(move source);
    switch (move result) {
    case variant std.string::from_bytes_result::valid(move value):
        bytes output = (move value).into_bytes();
        return move output;
    case variant std.string::from_bytes_result::invalid(move failure):
        throw InvalidUtf8 { .offset = failure.index };
    }
}

// A single write may make partial progress. Advance an offset over the loaned view.
async void write_bytes(bytes data)
    throws std.io::io_error, std.alloc::alloc_error, std.async::start_error {
    std.io::output output = std.io::stdout();
    usize offset = 0usize;
    task_scope(1) writer {
        while (offset < len(data)) {
            const u8[] view = data.as_slice();
            usize written = await output.write_from(view[offset..len(view)]);
            offset += written;
        }
    }
    await output.flush();
    await (move output).close();
}

async void write_shared(bytes data) throws std.io::io_error, std.async::start_error {
    std.io::output output = std.io::stdout();
    usize length = len(data);
    arc (bytes) shared = new arc (bytes)(move data);
    std.io::shared_write_result result = await output.write_shared(move shared, 0usize, length);
    switch (move result) {
    case variant std.io::shared_write_result::written(move returned): break;
    case variant std.io::shared_write_result::failed(move failure): throw failure.error;
    }
    await output.flush();
    await (move output).close();
}

async void diagnostic(std.string::string message) throws std.io::io_error, std.async::start_error {
    std.io::output output = std.io::stderr();
    bytes data = (move message).into_bytes();
    task_scope(1) writer {
        await std.io::write_all_from(&output, data.as_slice());
    }
}
