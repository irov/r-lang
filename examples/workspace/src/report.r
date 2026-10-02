module example.workspace.report;

std.string::string timestamp(o<std.time::system_time> value) throws std.alloc::alloc_error {
    switch (value) {
    case variant o::none: return std.string::from_str("unavailable");
    case variant o::some(stamp):
        std.string::string text = f"{stamp->unix_seconds} {stamp->nanoseconds}";
        return move text;
    }
}

constexpr str kind_name(std.fs::file_kind kind) {
    if (kind == std.fs::file_kind::regular) { return "regular"; }
    if (kind == std.fs::file_kind::directory) { return "directory"; }
    if (kind == std.fs::file_kind::symlink) { return "symlink"; }
    if (kind == std.fs::file_kind::character_device) { return "character_device"; }
    if (kind == std.fs::file_kind::block_device) { return "block_device"; }
    if (kind == std.fs::file_kind::fifo) { return "fifo"; }
    if (kind == std.fs::file_kind::socket) { return "socket"; }
    return "other";
}

std.string::string describe(std.fs::metadata info) throws std.alloc::alloc_error {
    constexpr str kind = kind_name(info.kind);
    std.string::string created = timestamp(info.created);
    std.string::string modified = timestamp(info.modified);
    std.string::string accessed = timestamp(info.accessed);
    return f"kind={kind} size={info.size}\ncreated={created}\nmodified={modified}\naccessed={accessed}\n";
}

async std.string::string first(std.fs::directory_iter iterator)
    throws std.fs::fs_error, std.async::start_error, std.fs::path_error, std.alloc::alloc_error {
    std.fs::directory_next_result result = await (move iterator).next();
    switch (move result) {
    case variant std.fs::directory_next_result::entry(move item):
        std.string::string name = std.fs::path_to_utf8(&item.entry.name);
        return f"first={name}\n";
    case variant std.fs::directory_next_result::end: return std.string::from_str("empty\n");
    case variant std.fs::directory_next_result::failed(move failure): throw failure.error;
    }
}
