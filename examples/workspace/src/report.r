module example.workspace.report;
import std.slice;

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

/* Every entry of the directory as a `name kind` line, sorted by name, or `empty`. Each entry hands
   back the iterator for the next one, and the declaration takes it out of the entry (Core
   R-STMT-0022). */
async std.string::string entries(std.fs::directory_iter iterator)
    throws std.fs::fs_error, std.async::start_error, std.fs::path_error, std.alloc::alloc_error {
    array<std.string::string> lines = [];
    walk: while (true) {
        std.fs::directory_next_result result = await (move iterator).next();
        switch (move result) {
        case variant std.fs::directory_next_result::entry(move item):
            auto {.iterator = rest, .entry} = move item;
            iterator = move rest;
            std.string::string name = std.fs::path_to_utf8(&entry.name);
            constexpr str kind = kind_name(entry.kind);
            std.string::string line = f"{name} {kind}\n";
            try {
                lines.push(move line);
            } catch (std.array::push_error<std.string::string> refused) {
                (move refused) as void;
                throw std.alloc::alloc_error::out_of_memory;
            }
        case variant std.fs::directory_next_result::end: break walk;
        case variant std.fs::directory_next_result::failed(move failure):
            auto {.error, .iterator = rest} = move failure;
            drop rest;
            throw error;
        }
    }
    if (len(lines) == 0usize) { return std.string::from_str("empty\n"); }
    {
        std.string::string[] ordered = lines.as_slice_mut();
        std.slice::sort(ordered);
    }
    std.string::string output = std.string::create();
    for (usize index = 0usize; index < len(lines); index += 1usize) { output.append(lines[index]); }
    return move output;
}
