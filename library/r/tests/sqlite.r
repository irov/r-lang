module tests.std.sqlite;
import std.test;
import std.fs;
import std.sqlite;

// The tests of std.sqlite (Library R-SLIB-SQLITE-0001..0008): values of every storage class
// through parameters and typed reads, prepared statements run again, refusals of the module and
// failures of SQLite with their codes and messages, transactions, and a database file in
// write-ahead-log mode shared by two connections, one of which waits for a lock and one of which
// only reads. Run in test mode (Core R-FUNC-0025) with the native provider of std.sqlite linked.

protected array<std.sqlite::value> values() {
    array<std.sqlite::value> none = [];
    return move none;
}

protected void add(array<std.sqlite::value>* target, std.sqlite::value item) throws std.alloc::alloc_error {
    try {
        target->push(move item);
    } catch (std.array::push_error<std.sqlite::value> rejected) {
        (move rejected) as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

protected array<std.sqlite::value> one(std.sqlite::value item) throws std.alloc::alloc_error {
    array<std.sqlite::value> listed = values();
    add(&listed, move item);
    return move listed;
}

@test
async void stores_and_reads_every_storage_class()
    throws std.sqlite::sqlite_error, std.error::fault, std.test::failure {
    std.sqlite::database db = await std.sqlite::open(":memory:", std.sqlite::options {});
    await db.execute_script(
        "CREATE TABLE item(id INTEGER PRIMARY KEY, name TEXT NOT NULL, score REAL, data BLOB, ok INTEGER);"
        "CREATE INDEX item_name ON item(name);");
    array<std.sqlite::value> first = values();
    add(&first, std.sqlite::value::of_text("käse"));
    add(&first, std.sqlite::value::real(2.5));
    add(&first, std.sqlite::value::of_blob("\x00\x01\x02"));
    add(&first, std.sqlite::value::of_bool(true));
    std.sqlite::execution done = await db.execute(
        "INSERT INTO item(name, score, data, ok) VALUES (?1, ?2, ?3, ?4)", move first);
    std.test::equal(done.changes, 1u64);
    std.test::equal(done.last_row_id, 1i64);
    array<std.sqlite::value> second = values();
    add(&second, std.sqlite::value::of_text(""));
    add(&second, std.sqlite::value::null_value);
    add(&second, std.sqlite::value::of_blob(""));
    add(&second, std.sqlite::value::integer(-9223372036854775807i64 - 1i64));
    std.sqlite::execution again = await db.execute(
        "INSERT INTO item(name, score, data, ok) VALUES (?, ?, ?, ?)", move second);
    std.test::equal(again.last_row_id, 2i64);

    std.sqlite::rows found = await db.query(
        "SELECT id, name, score, data, ok, typeof(data) FROM item ORDER BY id", values());
    std.test::equal(len(found.columns), 6usize);
    std.test::equal_text(found.columns[5usize].as_str(), "typeof(data)");
    std.test::equal(len(found.items), 2usize);
    const std.sqlite::row* row = &found.items[0usize];
    std.test::equal(row->count(), 6usize);
    std.test::equal(row->integer(0usize), 1i64);
    std.test::equal_text(row->text(1usize), "käse");
    std.test::check(row->real(2usize) == 2.5, "the real value");
    const u8[] data = row->blob(3usize);
    std.test::equal(len(data), 3usize);
    std.test::equal(data[2usize], 2u8);
    std.test::check(row->boolean(4usize) == true, "the truth value");
    std.test::check(row->real(0usize) == 1.0, "an integer read as a real");
    const std.sqlite::row* empty = &found.items[1usize];
    std.test::equal_text(empty->text(1usize), "");
    std.test::check(empty->is_null(2usize) == true, "NULL score");
    const u8[] none = empty->blob(3usize);
    std.test::equal(len(none), 0usize);
    std.test::equal_text(empty->text(5usize), "blob");
    std.test::equal(empty->integer(4usize), -9223372036854775807i64 - 1i64);
    switch (empty->optional_real(2usize)) {
    case variant o::none: break;
    default: std.test::check(false, "a NULL real is none");
    }
    switch (row->optional_text(1usize)) {
    case variant o::some(text): std.test::equal_text(*text, "käse");
    default: std.test::check(false, "a text is some");
    }
    switch (found.column("score")) {
    case variant o::some(index): std.test::equal(*index, 2usize);
    default: std.test::check(false, "the column is found");
    }
    switch (found.column("missing")) {
    case variant o::none: break;
    default: std.test::check(false, "no such column");
    }
    await (move db).close();
}

@test
async void runs_prepared_statements_again()
    throws std.sqlite::sqlite_error, std.error::fault, std.test::failure {
    std.sqlite::database db = await std.sqlite::open(":memory:", std.sqlite::options {});
    await db.execute_script("CREATE TABLE counter(n INTEGER NOT NULL)");
    std.sqlite::statement insert = await db.prepare("INSERT INTO counter(n) VALUES (?)");
    std.sqlite::execution first = await insert.execute(one(std.sqlite::value::integer(1i64)));
    std.test::equal(first.changes, 1u64);
    for (i64 n = 2i64; n <= 5i64; n += 1i64) {
        std.sqlite::execution done = await insert.execute(one(std.sqlite::value::integer(n)));
        std.test::equal(done.last_row_id, n);
    }
    std.sqlite::statement sum = await db.prepare("SELECT sum(n), count(*) FROM counter WHERE n > ?");
    std.sqlite::rows above_two = await sum.query(one(std.sqlite::value::integer(2i64)));
    std.test::equal(above_two.items[0usize].integer(0usize), 12i64);
    std.test::equal(above_two.items[0usize].integer(1usize), 3i64);
    std.sqlite::rows above_four = await sum.query(one(std.sqlite::value::integer(4i64)));
    std.test::equal(above_four.items[0usize].integer(0usize), 5i64);
    std.sqlite::execution removed = await db.execute("DELETE FROM counter WHERE n % 2 = 0", values());
    std.test::equal(removed.changes, 2u64);
    try {
        std.sqlite::rows wrong = await sum.query(values());
        drop wrong;
        std.test::check(false, "a missing value is refused");
    } catch (std.sqlite::sqlite_error failure) {
        std.test::check(failure.code == std.sqlite::error_code::parameter_count, "parameter_count");
        std.test::equal(failure.native_code, 0i32);
    }
    std.sqlite::rows again = await sum.query(one(std.sqlite::value::integer(0i64)));
    std.test::equal(again.items[0usize].integer(1usize), 3i64);
}

// One run of the series statement, as a child of the caller's task group: the count of the
// numbers up to limit.
@scoped
protected async i64 count_up_to(const std.sqlite::statement* series, i64 limit)
    throws std.sqlite::sqlite_error, std.error::fault {
    std.sqlite::rows found = await series->query(one(std.sqlite::value::integer(limit)));
    return found.items[0usize].integer(0usize);
}

// Two runs of one statement from concurrent tasks: each sees its own parameter, because the
// runs of a statement take turns (R-SLIB-SQLITE-0007).
@test
async void runs_one_statement_from_concurrent_tasks()
    throws std.sqlite::sqlite_error, std.error::fault, std.test::failure {
    std.sqlite::database db = await std.sqlite::open(":memory:", std.sqlite::options {});
    std.sqlite::statement series = await db.prepare(
        "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM c WHERE x < ?1) "
        "SELECT count(*) FROM c");
    for (i32 round = 0; round < 40; round += 1) {
        task_scope(2) group {
            auto first = count_up_to(&series, 20000i64);
            auto second = count_up_to(&series, 30000i64);
            i64 low = await move first;
            i64 high = await move second;
            std.test::equal(low, 20000i64);
            std.test::equal(high, 30000i64);
        }
    }
    drop series;
}

@test
async void reports_refusals_and_failures()
    throws std.sqlite::sqlite_error, std.error::fault, std.test::failure {
    std.sqlite::database db = await std.sqlite::open(":memory:", std.sqlite::options {});
    await db.execute_script("CREATE TABLE person(name TEXT PRIMARY KEY, age INTEGER CHECK (age >= 0))");
    try {
        std.sqlite::rows found = await db.query("SELEC 1", values());
        drop found;
        std.test::check(false, "syntax");
    } catch (std.sqlite::sqlite_error failure) {
        std.test::check(failure.code == std.sqlite::error_code::sql, "syntax");
    }
    try {
        std.sqlite::rows found = await db.query("SELECT * FROM nowhere", values());
        drop found;
        std.test::check(false, "unknown table");
    } catch (std.sqlite::sqlite_error failure) {
        std.test::check(failure.code == std.sqlite::error_code::sql, "unknown table");
    }
    try {
        std.sqlite::rows found = await db.query("SELECT 1; SELECT 2", values());
        drop found;
        std.test::check(false, "two statements");
    } catch (std.sqlite::sqlite_error failure) {
        std.test::check(failure.code == std.sqlite::error_code::several_statements, "two statements");
    }
    try {
        std.sqlite::rows found = await db.query("  -- nothing", values());
        drop found;
        std.test::check(false, "no statement");
    } catch (std.sqlite::sqlite_error failure) {
        std.test::check(failure.code == std.sqlite::error_code::no_statement, "no statement");
    }
    try {
        std.sqlite::rows found = await db.query("SELECT 1\x00", values());
        drop found;
        std.test::check(false, "a zero byte");
    } catch (std.sqlite::sqlite_error failure) {
        std.test::check(failure.code == std.sqlite::error_code::invalid_text, "a zero byte");
    }
    try {
        std.sqlite::rows found = await db.query("SELECT CAST(x'ff' AS TEXT)", values());
        drop found;
        std.test::check(false, "text that is not UTF-8");
    } catch (std.sqlite::sqlite_error failure) {
        std.test::check(failure.code == std.sqlite::error_code::invalid_text, "text that is not UTF-8");
    }
    (await db.execute("INSERT INTO person VALUES ('ada', 36)", values())) as void;
    try {
        (await db.execute("INSERT INTO person VALUES ('ada', 37)", values())) as void;
        std.test::check(false, "a repeated key is refused");
    } catch (std.sqlite::sqlite_error failure) {
        std.test::check(failure.code == std.sqlite::error_code::constraint, "constraint");
        std.test::equal(failure.native_code, 1555i32);
        std.test::equal_text(failure.message.as_str(), "UNIQUE constraint failed: person.name");
    }
    try {
        (await db.execute("INSERT INTO person VALUES ('bob', -1)", values())) as void;
        std.test::check(false, "a failed check is refused");
    } catch (std.sqlite::sqlite_error failure) {
        std.test::equal(failure.native_code, 275i32);
    }
    std.sqlite::rows found = await db.query("SELECT name, age, NULL FROM person", values());
    const std.sqlite::row* first = &found.items[0usize];
    try {
        first->integer(0usize) as void;
        std.test::check(false, "text read as an integer");
    } catch (std.sqlite::sqlite_error failure) {
        std.test::check(failure.code == std.sqlite::error_code::type_mismatch, "type_mismatch");
    }
    try {
        first->text(2usize) as void;
        std.test::check(false, "NULL read as text");
    } catch (std.sqlite::sqlite_error failure) {
        std.test::check(failure.code == std.sqlite::error_code::null_value, "null_value");
    }
    try {
        first->at(3usize) as void;
        std.test::check(false, "a column past the row");
    } catch (std.sqlite::sqlite_error failure) {
        std.test::check(failure.code == std.sqlite::error_code::column_range, "column_range");
    }
    try {
        first->boolean(1usize) as void;
        std.test::check(false, "36 is not a truth value");
    } catch (std.sqlite::sqlite_error failure) {
        std.test::check(failure.code == std.sqlite::error_code::type_mismatch, "not 0 or 1");
    }
}

@test
async void commits_and_rolls_back() throws std.sqlite::sqlite_error, std.error::fault, std.test::failure {
    std.sqlite::database db = await std.sqlite::open(":memory:", std.sqlite::options {});
    await db.execute_script("CREATE TABLE entry(text TEXT)");
    std.test::check(db.in_transaction() == false, "outside a transaction");
    await db.begin(std.sqlite::begin_mode::immediate);
    std.test::check(db.in_transaction() == true, "inside a transaction");
    (await db.execute("INSERT INTO entry VALUES ('kept')", values())) as void;
    await db.commit();
    await db.begin(std.sqlite::begin_mode::deferred);
    (await db.execute("INSERT INTO entry VALUES ('discarded')", values())) as void;
    await db.rollback();
    std.test::check(db.in_transaction() == false, "after rollback");
    std.sqlite::rows found = await db.query("SELECT text FROM entry", values());
    std.test::equal(len(found.items), 1usize);
    std.test::equal_text(found.items[0usize].text(0usize), "kept");
    try {
        await db.commit();
        std.test::check(false, "commit without a transaction");
    } catch (std.sqlite::sqlite_error failure) {
        std.test::check(failure.code == std.sqlite::error_code::sql, "no transaction is active");
    }
}

/* Removes a file of the current directory. */
@scoped
protected async void remove_here(str name) throws std.error::fault {
    std.fs::path here = std.fs::path_from_utf8(".");
    std.fs::path entry = std.fs::path_from_utf8(name);
    std.fs::directory root = await std.fs::open_directory(&here);
    await root.remove_file_beneath(&entry);
    await (move root).close();
}

@test
async void shares_a_file_in_write_ahead_log_mode()
    throws std.sqlite::sqlite_error, std.error::fault, std.test::failure {
    std.sqlite::database writer =
        await std.sqlite::open("tests_std_sqlite.db", std.sqlite::options {.wal = true});
    await writer.execute_script(
        "DROP TABLE IF EXISTS event; CREATE TABLE event(id INTEGER PRIMARY KEY, body TEXT NOT NULL)");
    std.sqlite::rows mode = await writer.query("PRAGMA journal_mode", values());
    std.test::equal_text(mode.items[0usize].text(0usize), "wal");
    (await writer.execute("INSERT INTO event(body) VALUES ('first')", values())) as void;

    std.sqlite::database other =
        await std.sqlite::open("tests_std_sqlite.db", std.sqlite::options {.busy_timeout_ms = 0u32});
    await writer.begin(std.sqlite::begin_mode::immediate);
    (await writer.execute("INSERT INTO event(body) VALUES ('second')", values())) as void;
    // A reader sees the last commit while the writer holds its transaction.
    std.sqlite::rows seen = await other.query("SELECT count(*) FROM event", values());
    std.test::equal(seen.items[0usize].integer(0usize), 1i64);
    try {
        await other.begin(std.sqlite::begin_mode::immediate);
        std.test::check(false, "a second writer waits for the lock");
    } catch (std.sqlite::sqlite_error failure) {
        std.test::check(failure.code == std.sqlite::error_code::busy, "busy");
    }
    await writer.commit();
    std.sqlite::rows committed = await other.query("SELECT count(*) FROM event", values());
    std.test::equal(committed.items[0usize].integer(0usize), 2i64);

    std.sqlite::database reader =
        await std.sqlite::open("tests_std_sqlite.db", std.sqlite::options {.read_only = true});
    try {
        (await reader.execute("INSERT INTO event(body) VALUES ('third')", values())) as void;
        std.test::check(false, "a read-only connection refuses writes");
    } catch (std.sqlite::sqlite_error failure) {
        std.test::check(failure.code == std.sqlite::error_code::read_only, "read_only");
    }
    await (move reader).close();
    await (move other).close();
    await (move writer).close();
    // The emptied write-ahead log and its index stay next to the file (R-SLIB-SQLITE-0006).
    task_scope(1) cleanup {
        await remove_here("tests_std_sqlite.db");
        await remove_here("tests_std_sqlite.db-wal");
        await remove_here("tests_std_sqlite.db-shm");
    }
}

@test
async void refuses_a_missing_file() throws std.error::fault, std.test::failure {
    try {
        std.sqlite::database db = await std.sqlite::open(
            "/nonexistent-directory-of-r-tests/data.db", std.sqlite::options {.create = false});
        drop db;
        std.test::check(false, "a missing file is refused");
    } catch (std.sqlite::sqlite_error failure) {
        std.test::check(failure.code == std.sqlite::error_code::cannot_open, "cannot_open");
        std.test::equal(failure.native_code, 14i32);
    }
}
