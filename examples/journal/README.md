# Records with a write-ahead journal

Keep fixed-size records in a data file with a write-ahead journal that several processes use in
turn: an update locks the whole data file, a read locks the one record it reads, records are
written at their offsets, `scan` reads them through a memory mapping of the data file, and an
update that dies between the journal and the data write is repaired by `recover` (Library
R-SLIB-FS-0014..0017).

```sh
ctest --test-dir build/debug -R 'example_journal' --output-on-failure
build/debug/tests/codegen_example_journal demo /tmp/store
build/debug/tests/codegen_example_journal init /tmp/store 8
build/debug/tests/codegen_example_journal put /tmp/store 3 hello
build/debug/tests/codegen_example_journal get /tmp/store 3
build/debug/tests/codegen_example_journal scan /tmp/store
```

`demo` runs every step in one program:

```text
store of 4 records
slot 0: alpha, slot 1: beta
another open file holds a shared lock of slot 0, a probe finds the store busy: true
an update waited for that lock and then wrote slot 2: delta
interrupted after the journal of slot 1, which still reads beta
replayed slot 1, slot 1 now reads gamma
a second recovery: nothing to replay; busy: false
```

[store.r](src/store.r) holds the store. A record is 64 bytes: a used flag, the text length and
the text. An update takes the exclusive lock of the whole data file, waiting at most the given
time inside a `deadline` block, then writes the journal entry (a header with the slot, the length
and the CRC-32 of the record, then the record) and orders it before the data write:

```r
await data.lock(std.fs::lock_kind::exclusive, 0u64, 0u64);
task_scope(1) io {
    await log.write_all_at_from(0u64, entry.as_slice());
}
await log.sync(std.fs::sync_level::barrier);
```

It then writes the record at its offset with `write_all_at`, makes it durable with
`sync(std.fs::sync_level::media)`, clears the journal, syncs it to the device and unlocks. None
of these writes moves the shared file position. A read takes a shared lock of its record only and
reads it with `read_at_into` into a buffer of its own.

`scan` takes a shared lock of the whole data file and maps it read-only with
`std.fs::map_file`; `bytes()` of the mapping is a view anchored to it, so the records are read in
place and the mapping cannot be dropped while the view is in use. The lock is what makes the
unsafe contract of `map_file` hold: no journal process writes or truncates the file while the
view lives.

```r
std.fs::mapping mapped = await map_range(&data_file, 0u64, (count as usize) * record_size,
                                         std.fs::map_access::read_only);
const u8[] all = mapped.bytes();
```

`crash` stops an update right after its journal, as a program that dies there; its locks go with
its closed files. `recover` replays a journal entry whose checksum matches and clears it: under
the exclusive lock it maps the record of the slot read-write, copies the record into
`bytes_mut()`, writes the page to the file with the mapping's `sync()` and then makes the data file
durable with `sync(std.fs::sync_level::media)`. A torn entry is left alone. `hold` keeps the exclusive lock for some milliseconds and `busy` asks with
`try_lock` whether another open file holds a lock, so several processes can be watched taking
turns.

The behaviour test checks the files byte by byte from Python, sees the lock of a `hold` process
with `fcntl.lockf`, lets an update wait for it and another one time out, scans a record that
Python wrote into the file, replays an interrupted update and refuses a torn journal.
