module example.journal.store;
import std.bytes;
import std.hash;
import std.time;
import std.console;
import std.fs;

/* A store of fixed-size records in a data file, with a journal file that holds one pending
   record write. An update writes the journal first, then the slot of the data file, and clears
   the journal last, so a store interrupted between the two writes is repaired by replaying the
   journal. Every update holds an exclusive lock of the data file; a read holds a shared lock of
   the one record it reads (Library R-SLIB-FS-0014..0016). A listing reads the store through a
   memory mapping of the data file, and recover writes its record through one (R-SLIB-FS-0017). */

const usize record_size = 64usize;
const usize text_limit = 62usize;
const usize header_size = 16usize;

/* The bytes of a record: a used flag, the text length and the text, zero-padded. */
bytes record_of(str text) throws std.alloc::alloc_error {
    const u8[] raw_text = text;
    usize count = len(raw_text);
    if (count > text_limit) { count = text_limit; }
    bytes record = std.alloc::bytes(record_size, 0u8);
    record[0usize] = 1u8;
    record[1usize] = count as u8;
    for (usize index = 0usize; index < count; index += 1usize) {
        record[2usize + index] = raw_text[index];
    }
    return move record;
}

/* The text of a record, or "(empty)" for an unused slot. */
std.string::string text_of(const u8[] record) throws std.alloc::alloc_error {
    std.string::string text = std.string::create();
    if (len(record) < record_size || record[0usize] != 1u8) {
        text.append("(empty)");
        return move text;
    }
    usize count = record[1usize] as usize;
    if (count > text_limit) { count = text_limit; }
    bytes copied = {};
    std.bytes::append(&copied, record[2usize..2usize + count]);
    try {
        text.append(core::validate_utf8(copied.as_slice()));
    } catch (core::utf8_error failure) {
        failure as void;
        text.append("(not text)");
    }
    return move text;
}

/* A journal entry: the magic "RJ1", the slot, the record length and the CRC-32 of the record,
   followed by the record. */
bytes journal_entry(u32 slot, const u8[] record) throws std.alloc::alloc_error {
    bytes entry = {};
    std.bytes::append_u8(&entry, 82u8);
    std.bytes::append_u8(&entry, 74u8);
    std.bytes::append_u8(&entry, 49u8);
    std.bytes::append_u8(&entry, 0u8);
    std.bytes::append_u32_le(&entry, slot);
    std.bytes::append_u32_le(&entry, len(record) as u32);
    std.bytes::append_u32_le(&entry, std.hash::crc32(record));
    std.bytes::append(&entry, record);
    return move entry;
}

/* A journal entry that was completely written. */
struct pending { u32 slot; bytes record; };

/* The entry of a journal whose header and record are intact, or none. */
o<pending> pending_of(const u8[] entry_bytes, usize count) throws std.alloc::alloc_error {
    if (count < header_size + record_size) { return o::none; }
    if (entry_bytes[0usize] != 82u8 || entry_bytes[1usize] != 74u8 || entry_bytes[2usize] != 49u8) { return o::none; }
    std.bytes::cursor at_length = std.bytes::cursor {.position = 8usize};
    std.bytes::cursor at_checksum = std.bytes::cursor {.position = 12usize};
    std.bytes::cursor at_slot = std.bytes::cursor {.position = 4usize};
    try {
        if (at_length.read_u32_le(entry_bytes) as usize != record_size) { return o::none; }
        const u8[] record = entry_bytes[header_size..header_size + record_size];
        if (std.hash::crc32(record) != at_checksum.read_u32_le(entry_bytes)) { return o::none; }
        bytes copied = {};
        std.bytes::append(&copied, record);
        return o::some(pending {.slot = at_slot.read_u32_le(entry_bytes), .record = move copied});
    } catch (std.bytes::bytes_error failure) {
        failure as void;
    }
    return o::none;
}

std.fs::open_file_options writing(bool truncate) {
    return std.fs::open_file_options { .access = std.fs::access::read_write,
        .create = std.fs::create_mode::open_or_create, .truncate = truncate, .append = false,
        .follow_final_symlink = false };
}

std.fs::open_file_options reading() {
    return std.fs::open_file_options { .access = std.fs::access::read,
        .create = std.fs::create_mode::existing, .truncate = false, .append = false,
        .follow_final_symlink = false };
}

std.fs::path data_path(const std.string::string* directory) throws std.fs::path_error, std.alloc::alloc_error {
    str base = *directory;
    std.string::string name = f"{base}/store.dat";
    return std.fs::path_from_utf8(name);
}

std.fs::path journal_path(const std.string::string* directory) throws std.fs::path_error, std.alloc::alloc_error {
    str base = *directory;
    std.string::string name = f"{base}/store.journal";
    return std.fs::path_from_utf8(name);
}

std.time::instant after_milliseconds(u32 ms) throws std.time::time_error, std.time::duration_error {
    std.time::instant now = std.time::monotonic_now();
    return now.add(std.time::duration_from_parts((ms / 1000u32) as i64, (ms % 1000u32) * 1000000u32));
}

/* Creates the data file with count empty records and an empty journal. */
async void init(std.string::string directory, usize count) throws std.error::fault {
    std.fs::path data_file = data_path(&directory);
    std.fs::path journal_file = journal_path(&directory);
    std.fs::file data = await data_file.open_file(writing(true));
    bytes empty = std.alloc::bytes(count * record_size, 0u8);
    std.io::write_all_result written = await data.write_all(move empty);
    switch (move written) {
    case variant std.io::write_all_result::written(move returned): drop returned;
    case variant std.io::write_all_result::failed(move failure): throw failure.error;
    }
    await data.sync(std.fs::sync_level::media);
    await (move data).close();
    std.fs::file log = await journal_file.open_file(writing(true));
    bytes cleared = std.alloc::bytes(header_size, 0u8);
    std.io::write_all_result header = await log.write_all(move cleared);
    switch (move header) {
    case variant std.io::write_all_result::written(move returned): drop returned;
    case variant std.io::write_all_result::failed(move failure): throw failure.error;
    }
    await log.sync(std.fs::sync_level::media);
    await (move log).close();
}

/* A slot outside the data file. */
error store_error { u32 slot; };

protected void written_or_throw(std.io::write_all_result written) throws std.io::io_error {
    switch (move written) {
    case variant std.io::write_all_result::written(move returned): drop returned;
    case variant std.io::write_all_result::failed(move failure): throw failure.error;
    }
}

/* The number of records of an open data file. */
protected async u32 record_count_of(std.fs::path data_file) throws std.error::fault {
    std.fs::metadata info = await data_file.metadata();
    return (info.size / (record_size as u64)) as u32;
}

/* Stores text in slot. The update takes the exclusive lock of the whole data file, waiting at
   most wait_ms for it; writes the journal entry and orders it before the data write with a
   barrier; writes the record at its offset and makes it durable; clears the journal. With
   interrupt it stops after the journal, as a program that dies there, and the lock goes with
   its closed files. */
async void update(std.string::string directory, u32 slot, std.string::string text, u32 wait_ms,
                  bool interrupt) throws store_error, std.error::fault {
    u32 count = await record_count_of(data_path(&directory));
    throw (slot >= count) store_error {.slot = slot};
    std.fs::path data_file = data_path(&directory);
    std.fs::path journal_file = journal_path(&directory);
    std.fs::file data = await data_file.open_file(writing(false));
    std.fs::file log = await journal_file.open_file(writing(false));
    deadline (after_milliseconds(wait_ms)) {
        await data.lock(std.fs::lock_kind::exclusive, 0u64, 0u64);
    }
    bytes record = record_of(text);
    bytes entry = journal_entry(slot, record.as_slice());
    task_scope(1) io {
        await log.write_all_at_from(0u64, entry.as_slice());
    }
    await log.sync(std.fs::sync_level::barrier);
    if (interrupt == true) {
        await (move log).close();
        await (move data).close();
        return;
    }
    u64 offset = (slot as u64) * (record_size as u64);
    written_or_throw(await data.write_all_at(offset, move record));
    await data.sync(std.fs::sync_level::media);
    bytes cleared = std.alloc::bytes(header_size, 0u8);
    task_scope(1) clearing {
        await std.fs::write_all_at_from(&log, 0u64, cleared.as_slice());
    }
    await log.sync(std.fs::sync_level::device);
    await data.unlock(0u64, 0u64);
    await (move log).close();
    await (move data).close();
}

/* The text of slot, read under a shared lock of that one record. */
async std.string::string get(std.string::string directory, u32 slot) throws store_error, std.error::fault {
    u32 count = await record_count_of(data_path(&directory));
    throw (slot >= count) store_error {.slot = slot};
    std.fs::path data_file = data_path(&directory);
    std.fs::file data = await data_file.open_file(reading());
    u64 offset = (slot as u64) * (record_size as u64);
    await std.fs::lock(&data, std.fs::lock_kind::shared, offset, record_size as u64);
    bytes record = std.alloc::bytes(record_size, 0u8);
    usize filled = 0usize;
    task_scope(1) io {
        filled += await data.read_at_into(offset, record.as_slice_mut());
    }
    await data.unlock(offset, record_size as u64);
    await (move data).close();
    throw (filled != record_size) store_error {.slot = slot};
    return text_of(record.as_slice());
}

/* Maps length bytes of the data file from offset. The contract of std.fs::map_file holds
   because every caller keeps a lock of that range while the mapping lives: the journal processes
   take their locks before they write the data file, and none of them truncates it. */
protected task<std.fs::mapping throws std.fs::fs_error> map_range(const std.fs::path* data_file, u64 offset,
                                                                  usize length, std.fs::map_access access)
    throws std.fs::path_error, std.async::start_error, std.alloc::alloc_error {
    unsafe { return std.fs::map_file(data_file, offset, length, access); }
}

/* Every used record, read through a read-only mapping of the whole data file under a shared
   lock of it, so no update writes the store while the view of the mapping is live. */
async std.string::string scan(std.string::string directory) throws std.error::fault {
    std.fs::path data_file = data_path(&directory);
    std.fs::file data = await data_file.open_file(reading());
    await data.lock(std.fs::lock_kind::shared, 0u64, 0u64);
    u32 count = await record_count_of(data_path(&directory));
    std.string::string listing = std.string::create();
    bool found = false;
    if (count > 0u32) {
        std.fs::mapping mapped = await map_range(&data_file, 0u64, (count as usize) * record_size,
                                                 std.fs::map_access::read_only);
        const u8[] all = mapped.bytes();
        for (usize at = 0usize; at + record_size <= len(all); at += record_size) {
            const u8[] record = all[at..at + record_size];
            if (record[0usize] == 1u8) {
                usize slot = at / record_size;
                std.string::string text = text_of(record);
                std.string::string line = f"slot {slot}: {text}\n";
                listing.append(line);
                found = true;
            }
        }
        drop mapped;
    }
    await data.unlock(0u64, 0u64);
    await (move data).close();
    if (found == false) { listing.append("no records\n"); }
    return move listing;
}

/* Replays a complete journal entry into the data file under the exclusive lock, writing the
   record through a writable mapping of its slot, then clears the journal; an empty or torn
   journal leaves the data file as it is. */
async std.string::string recover(std.string::string directory) throws std.error::fault {
    std.fs::path data_file = data_path(&directory);
    std.fs::path journal_file = journal_path(&directory);
    std.fs::file data = await data_file.open_file(writing(false));
    std.fs::file log = await journal_file.open_file(writing(false));
    await data.lock(std.fs::lock_kind::exclusive, 0u64, 0u64);
    bytes buffer = std.alloc::bytes(header_size + record_size, 0u8);
    std.io::read_result read = await log.read_at(0u64, move buffer);
    o<pending> found = o::none;
    switch (move read) {
    case variant std.io::read_result::read(move part):
        const u8[] seen = std.array::as_slice(&part.buffer);
        found = pending_of(seen, part.count);
    case variant std.io::read_result::end(move returned): drop returned;
    case variant std.io::read_result::failed(move failure): throw failure.error;
    }
    bool replayed = false;
    u32 replayed_slot = 0u32;
    switch (move found) {
    case variant o::some(move entry):
        u32 slot = entry.slot;
        bytes none = {};
        pending taken = move entry;
        bytes record = core::replace(&taken.record, move none);
        std.fs::mapping window = await map_range(&data_file, (slot as u64) * (record_size as u64),
                                                 record_size, std.fs::map_access::read_write);
        u8[] target = window.bytes_mut();
        const u8[] source = record.as_slice();
        for (usize index = 0usize; index < len(target); index += 1usize) {
            target[index] = source[index];
        }
        /* The mapping writes its pages to the file; the data file then reaches the medium. */
        await window.sync();
        drop window;
        await data.sync(std.fs::sync_level::media);
        bytes cleared = std.alloc::bytes(header_size, 0u8);
        written_or_throw(await log.write_all_at(0u64, move cleared));
        await log.sync(std.fs::sync_level::device);
        replayed = true;
        replayed_slot = slot;
    case variant o::none: break;
    }
    await data.unlock(0u64, 0u64);
    await (move log).close();
    await (move data).close();
    if (replayed == true) { return f"replayed slot {replayed_slot}"; }
    return f"nothing to replay";
}

/* Holds the exclusive lock of the data file for ms milliseconds, saying when it holds it. */
async void hold(std.string::string directory, u32 ms) throws std.error::fault {
    std.fs::path data_file = data_path(&directory);
    std.fs::file data = await data_file.open_file(writing(false));
    await data.lock(std.fs::lock_kind::exclusive, 0u64, 0u64);
    await std.console::println(f"held");
    await std.time::sleep_for(std.time::duration_from_parts((ms / 1000u32) as i64, (ms % 1000u32) * 1000000u32));
    await data.unlock(0u64, 0u64);
    await (move data).close();
}

/* Whether another open file holds a lock of the data file now. */
async bool busy(std.string::string directory) throws std.error::fault {
    std.fs::path data_file = data_path(&directory);
    std.fs::file data = await data_file.open_file(writing(false));
    bool taken = await std.fs::try_lock(&data, std.fs::lock_kind::exclusive, 0u64, 0u64);
    if (taken == true) {
        await data.unlock(0u64, 0u64);
    }
    await (move data).close();
    return taken == false;
}

