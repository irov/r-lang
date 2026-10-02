module example.workspace.files;
import example.workspace.report;

std.fs::open_file_options settings(std.fs::access access, std.fs::create_mode create) {
    return std.fs::open_file_options { .access = access, .create = create,
        .truncate = false, .append = false, .follow_final_symlink = false };
}

// Create a journal record: one version byte followed by the caller's UTF-8 message.
async std.string::string record(std.fs::file file, std.string::string message)
    throws std.fs::fs_error, std.io::io_error, std.async::start_error, std.alloc::alloc_error {
    bytes version = std.alloc::bytes(1usize, 1u8);
    std.io::write_result first = await file.write(move version);
    switch (move first) {
    case variant std.io::write_result::written(move part): break;
    case variant std.io::write_result::failed(move failure): throw failure.error;
    }
    bytes body = (move message).into_bytes();
    std.io::write_all_result written = await file.write_all(move body);
    switch (move written) {
    case variant std.io::write_all_result::written(move returned): break;
    case variant std.io::write_all_result::failed(move failure): throw failure.error;
    }
    await file.flush();
    u64 position = await file.seek(std.fs::seek_origin::current, 0i64);
    std.fs::metadata info = await file.metadata();
    await (move file).close();
    return f"written={position} size={info.size}\n";
}

// Tail a regular file without loading the prefix into memory.
async std.string::string tail(std.fs::path path, u32 count)
    throws std.fs::fs_error, std.io::io_error, std.async::start_error, std.alloc::alloc_error {
    std.fs::open_file_options options = settings(std.fs::access::read, std.fs::create_mode::existing);
    std.fs::file file = await path.open_file(options);
    std.fs::metadata info = await file.metadata();
    u64 desired = count as u64;
    if (desired > info.size) { desired = info.size; }
    i64 offset = desired as i64;
    u64 position = await file.seek(std.fs::seek_origin::end, -offset);
    usize size = desired as usize;
    bytes selected = {};
    while (len(selected) < size) {
        usize missing = size - len(selected);
        bytes buffer = std.alloc::bytes(missing, 0u8);
        std.io::read_result result = await file.read(move buffer);
        switch (move result) {
        case variant std.io::read_result::read(move part):
            const u8[] source = std.array::as_slice(&part.buffer);
            for (usize index = 0usize; index < part.count; index += 1usize) {
                std.bytes::append_u8(&selected, source[index]);
            }
            break;
        case variant std.io::read_result::end(move returned):
            await (move file).close();
            u32 checksum = std.hash::crc32(selected);
            usize length = len(selected);
            return f"offset={position} bytes={length} crc32={checksum}\n";
        case variant std.io::read_result::failed(move failure): throw failure.error;
        }
    }
    await (move file).close();
    usize length = len(selected);
    u32 checksum = std.hash::crc32(selected);
    return f"offset={position} bytes={length} crc32={checksum}\n";
}

async void publish(std.fs::path destination, bytes contents)
    throws std.fs::fs_error, std.async::start_error {
    std.fs::write_file_result result = await destination.write_file_atomic_no_replace(move contents);
    switch (move result) {
    case variant std.fs::write_file_result::committed(move returned): return;
    case variant std.fs::write_file_result::failed(move failure): throw failure.error;
    }
}
