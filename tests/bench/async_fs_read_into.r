module bench.async_fs_read_into;

/* One scoped 4 KiB read per iteration from a regular file through the filesystem lane; the
   shared position is rewound at end of file. */
async i32 main() {
    std.fs::path path = std.fs::path_from_utf8("/usr/share/dict/words");
    std.fs::open_file_options reading = std.fs::open_file_options { .access = std.fs::access::read,
        .create = std.fs::create_mode::existing, .truncate = false, .append = false,
        .follow_final_symlink = true };
    std.fs::file file = await path.open_file(reading, o::none);
    bytes buffer = std.alloc::bytes(4096usize, 0u8);
    usize iterations = 20_000usize;
    usize total = 0usize;
    task_scope(1) reader {
        for (usize index = 0usize; index < iterations; index += 1usize) {
            usize count = await std.fs::read_into(&file, buffer.as_slice_mut(), o::none);
            if (count == 0usize) {
                u64 rewound = await file.seek(std.fs::seek_origin::start, 0i64, o::none);
                rewound as void;
            }
            total += count;
        }
    }
    drop buffer;
    await (move file).close(o::none);
    return (total % 109usize) as i32;
}
