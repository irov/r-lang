module std.fs;

/* R-SLIB-FS-0017: the R part of std.fs, loaded by `import std.fs;`: ranges of files mapped into
   memory over the native provider std.fs.native. Mapping is unsafe: the language cannot see
   whether the range of the file stays in place, and unchanged by others, while it is mapped. */

@link(name = "std.fs.native", kind = "static")
@header("r_std_fs_native.h")
extern "C" {
    @safety("FS-NATIVE-MAP", "path addresses path_length bytes and every output is writable")
    c_int32 r_std_fs_native_map(raw const c_uint8*? path,
                                c_size path_length,
                                c_uint64 offset,
                                c_uint64 length,
                                c_int32 writable,
                                raw (raw void*?)* base,
                                raw c_size* total,
                                raw (raw c_uint8*?)* start,
                                raw c_int32* native_error);

    @safety("FS-NATIVE-UNMAP", "base is null, or base and total name a mapping of map that is still mapped")
    void r_std_fs_native_unmap(raw void*? base, c_size total);

    @safety("FS-NATIVE-SYNC", "base is null, or base and total name a live mapping of map; native_error is writable")
    c_int32 r_std_fs_native_sync(raw void*? base, c_size total, raw c_int32* native_error);
}

/* R-SLIB-FS-0017: whether a mapping may be written. */
enum map_access { read_only, read_write };

/* The pages of a mapping, shared by the mapping and the syncs that it started and unmapped by
   the last of them. The addresses are atomic fields, so the pages are Send and Sync (Core
   R-MEM-0003). */
protected struct region {
    protected atomic raw void*? base;
    protected atomic raw void*? start;
    protected usize total;
};

drop(region* self) {
    raw void*? base = core::atomic_load(&self->base, core::memory_order::relaxed);
    unsafe { r_std_fs_native_unmap(base, self->total as c_size); }
}

/* R-SLIB-FS-0017: a range of a file mapped into memory. */
struct mapping {
    protected arc region pages;
    protected usize range_length;
    protected bool writable;
};

/* The std.fs::fs_error of a failure class of the provider. */
protected std.fs::fs_error native_failure(i32 class, i32 native) {
    std.fs::error_code code = std.fs::error_code::other;
    switch (class) {
    case 2: code = std.fs::error_code::invalid_operation;
    case 3: code = std.fs::error_code::not_found;
    case 4: code = std.fs::error_code::permission_denied;
    case 5: code = std.fs::error_code::resource_exhausted;
    case 6: code = std.fs::error_code::is_directory;
    case 7: code = std.fs::error_code::read_only;
    case 8: code = std.fs::error_code::name_too_long;
    default: break;
    }
    return std.fs::fs_error {.code = code, .native_code = native as i64};
}

/* The address of the first byte of a view as the provider reads it, null for an empty view. */
protected raw const c_uint8*? bytes_of(const u8[] data) {
    if (len(data) == 0usize) { return null; }
    unsafe {
        raw const u8* first = &data[0usize] as raw const u8*;
        raw const void* erased = first as raw const void*;
        return erased as raw const c_uint8*;
    }
}

/* Maps a range of the file at path, on the blocking call pool. */
protected mapping map_entry(std.string::string path, u64 offset, usize length, bool writable)
    throws std.fs::fs_error {
    const u8[] text = path.as_bytes();
    raw void*? base = null;
    c_size total = 0usize as c_size;
    raw c_uint8*? start = null;
    c_int32 native = 0i32 as c_int32;
    c_int32 writing = 0i32 as c_int32;
    if (writable == true) { writing = 1i32 as c_int32; }
    unsafe {
        raw (raw void*?)* base_out = &base as raw (raw void*?)*;
        raw c_size* total_out = &total as raw c_size*;
        raw (raw c_uint8*?)* start_out = &start as raw (raw c_uint8*?)*;
        raw c_int32* native_out = &native as raw c_int32*;
        c_int32 status = r_std_fs_native_map(bytes_of(text), len(text) as c_size, offset as c_uint64,
                                             length as u64 as c_uint64, writing, base_out, total_out,
                                             start_out, native_out);
        throw (status as i32 != 0i32) native_failure(status as i32, native as i32);
        raw void*? first = start as raw void*?;
        return mapping {.pages = new arc region {.base = base, .start = first, .total = total as usize},
                        .range_length = length, .writable = writable};
    }
}

/* R-SLIB-FS-0017: maps length bytes of the file at path from offset, readable and, for
   read_write, writable with every write reaching the file. The range shall be nonempty and lie
   within a regular file. */
@safety("FS-MAP-FILE", "while the mapping lives, no process truncates the mapped range of the file, and nothing but the mapping writes it")
unsafe task<mapping throws std.fs::fs_error> map_file(const std.fs::path* path, u64 offset, usize length,
                                                     map_access access)
    throws std.fs::path_error, std.async::start_error, std.alloc::alloc_error {
    std.string::string text = std.fs::path_to_utf8(path);
    bool writable = access == map_access::read_write;
    return std.async::blocking(map_entry, move text, offset, length, writable);
}

/* The first mapped byte of the range. */
protected raw void*? first_byte(const mapping* mapped) {
    return core::atomic_load(&mapped->pages->start, core::memory_order::relaxed);
}

/* R-SLIB-FS-0017: the bytes of the mapped range, as a view anchored to the mapping (Core
   R-UNSAFE-0008): the mapping cannot be moved or dropped while the view is live. */
const u8[] mapping::bytes(const mapping* this) {
    raw void*? first = first_byte(this);
    unsafe { return core::slice_from_raw_parts_in(this, first as raw const u8*?, this->range_length); }
}

/* R-SLIB-FS-0017: the bytes of a read_write mapping, as an exclusive view anchored to the
   mapping; a read_only mapping is invalid_operation. */
u8[] mapping::bytes_mut(mapping* this) throws std.fs::fs_error {
    throw (this->writable == false) native_failure(2, 0);
    raw void*? first = first_byte(this);
    unsafe { return core::slice_from_raw_parts_in_mut(this, first as raw u8*?, this->range_length); }
}

/* R-SLIB-FS-0017: the number of mapped bytes. */
usize mapping::size(const mapping* this) {
    return this->range_length;
}

protected void sync_entry(arc region pages) throws std.fs::fs_error {
    raw void*? base = core::atomic_load(&pages->base, core::memory_order::relaxed);
    c_int32 native = 0i32 as c_int32;
    unsafe {
        raw c_int32* native_out = &native as raw c_int32*;
        c_int32 status = r_std_fs_native_sync(base, pages->total as c_size, native_out);
        throw (status as i32 != 0i32) native_failure(status as i32, native as i32);
    }
}

/* R-SLIB-FS-0017: writes the changed pages of the mapping to the file and waits for the write,
   on the blocking call pool; the call keeps the pages mapped until it ends. */
task<void throws std.fs::fs_error> mapping::sync(const mapping* this)
    throws std.async::start_error, std.alloc::alloc_error {
    arc region pages = std.arc::clone(&this->pages);
    return std.async::blocking(sync_entry, move pages);
}
