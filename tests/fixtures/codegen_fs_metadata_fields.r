module test.codegen.fs_metadata_fields;

i32 inspect(std.fs::metadata info) {
    if (info.kind != std.fs::file_kind::regular || info.size != 42u64) { return 1; }
    switch (info.created) {
    case variant o::none: break;
    case variant o::some(value): return 2;
    }
    switch (info.modified) {
    case variant o::none: return 3;
    case variant o::some(value):
        if (value->unix_seconds != 123i64 || value->nanoseconds != 456u32) { return 4; }
        break;
    }
    std.fs::metadata copied = info;
    copied.accessed = copied.modified;
    switch (copied.accessed) {
    case variant o::none: return 5;
    case variant o::some(value):
        if (value->unix_seconds != 123i64) { return 6; }
        break;
    }
    return 0;
}

async i32 main() {
    std.time::system_time stamp = { .unix_seconds = 123i64, .nanoseconds = 456u32 };
    std.fs::metadata info = { .kind = std.fs::file_kind::regular, .size = 42u64,
        .created = o::none, .modified = o::some(stamp), .accessed = o::none };
    if (inspect(info) != 0) { return 7; }
    info.created = info.modified;
    switch (info.created) {
    case variant o::none: return 8;
    case variant o::some(value):
        if (value->nanoseconds != 456u32) { return 9; }
        break;
    }
    return 0;
}
