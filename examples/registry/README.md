# Device registry with a transactional outbox

Keep devices in a SQLite database and announce every change through an outbox table that is
written in the same transaction as the change. A relay delivers the committed events in order and
marks them delivered. `std.sqlite` runs every statement on the blocking call pool, so the program
waits for the database without holding an executor worker (Library R-SLIB-SQLITE-0001..0008).

```sh
ctest --test-dir build/debug -R 'example_registry' --output-on-failure
build/debug/tests/codegen_example_registry demo /tmp/registry.db
build/debug/tests/codegen_example_registry init devices.db
build/debug/tests/codegen_example_registry register devices.db pump-1 cellar 12.5
build/debug/tests/codegen_example_registry outbox devices.db
build/debug/tests/codegen_example_registry deliver devices.db
```

`demo` creates a database in write-ahead-log mode, registers two devices, is refused a second
registration of the first, renames and retires devices, shows how two connections see one file,
lists the outbox and delivers it in two batches:

```text
ready (journal wal)
registered sensor-1 (event 1) and sensor-2 (event 2)
sensor-1 again: already_registered, nothing published
renamed sensor-2 (event 3), retired sensor-1 (event 4)
writer inserted 1, reader saw 2, second writer refused: true (busy), after rollback 2
1 device.registered {"id":"sensor-1","name":"boiler"} (3 columns)
...
deliver 4 device.retired {"id":"sensor-1","name":"boiler"}
delivered 1 (transaction was open: true)
sensor-1 boiler revision 2 active false weight 2.5 kg note replaced fingerprint 75fcce4506e2b49c
sensor-2 loft revision 2 active true weight - note - fingerprint 3fa50f3cdbe45d58
```

[store.r](src/store.r) holds the schema and the changes. `register` binds one value of each
storage class, inserts the device and appends its event inside one `BEGIN IMMEDIATE`
transaction; a repeated identifier fails with `constraint` before any event is written, and the
rollback ends the transaction with nothing published:

```r
await db.begin(std.sqlite::begin_mode::immediate);
try {
    std.sqlite::execution inserted = await db.execute(
        "INSERT INTO device(id, name, revision, active, weight, note, fingerprint) VALUES (?, ?, ?, ?, ?, ?, ?)",
        move device);
    inserted as void;
} catch (std.sqlite::sqlite_error failure) {
    await db.rollback();
    ...
}
std.sqlite::execution event = await publish(&db, "device.registered", id, name);
await db.commit();
```

Every operation copies its text and returns a started task, so `publish` is an ordinary function
that takes the database by reference and returns the task of its insert. `deliver` prepares two
statements, reads a batch of pending events and marks each one delivered with the second
statement in the same transaction. `show` and `outbox` open the file read-only and read each
column with a typed read: `text`, `integer`, `boolean`, and `optional_real`, `optional_text` and
`optional_blob` for the columns that may be NULL.

[main.r](src/main.r) has the commands and the `snapshots` part of the demo: while one connection
holds a write transaction, a second connection still reads the last commit and is refused a
transaction of its own with `busy`, because it does not wait (`busy_timeout_ms = 0`).

The behaviour test checks the database with the `sqlite3` module of Python, which also writes a
device and its event of its own, holds a write lock that `retire` waits for, and holds one past
the timeout of `register`, which then fails without leaving a device behind.
