module tests.std.postgres;
import std.test;
import std.fs;
import std.tls;
import std.pool;
import std.text;
import std.postgres;

// The tests of std.postgres (Library R-SLIB-PG-0001..0012) against a PostgreSQL server that
// tests/run_postgres_tests.py starts for them: the environment names the server as libpq reads it
// (PGHOST, PGPORT, PGUSER, PGPASSWORD, PGDATABASE), R_POSTGRES_SOCKET its socket directory and
// R_POSTGRES_TLS_AUTHORITY the authority of its certificate. Without R_POSTGRES_TEST every test
// passes at once, so that the program also runs where no server was started.

protected bool enabled() throws std.alloc::alloc_error {
    try {
        o<std.string::string> flag = std.env::get("R_POSTGRES_TEST");
        switch (move flag) {
        case variant o::some(move text): drop text; return true;
        case variant o::none: break;
        }
    } catch (std.env::env_error unreadable) {
        unreadable as void;
    }
    return false;
}

protected std.string::string setting(str name) throws std.error::fault, std.test::failure {
    o<std.string::string> found = std.env::get(name);
    switch (move found) {
    case variant o::some(move text): return move text;
    case variant o::none: break;
    }
    std.test::check(false, "the driver sets the variable");
    return std.string::create();
}

protected array<std.postgres::value> values() {
    array<std.postgres::value> none = [];
    return move none;
}

protected void add(array<std.postgres::value>* target, std.postgres::value item) throws std.alloc::alloc_error {
    try {
        target->push(move item);
    } catch (std.array::push_error<std.postgres::value> rejected) {
        (move rejected) as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

protected array<std.postgres::value> one(std.postgres::value item) throws std.alloc::alloc_error {
    array<std.postgres::value> listed = values();
    add(&listed, move item);
    return move listed;
}

protected std.postgres::options user(str name, str password) throws std.postgres::pg_error, std.alloc::alloc_error {
    std.postgres::options made = std.postgres::options::from_environment();
    made.user = std.string::from_str(name);
    made.password = std.string::from_str(password);
    return move made;
}

protected bool same(str left, str right) {
    return std.text::equal_ignore_ascii_case(left, right);
}

@test
async void connects_with_scram_md5_and_password()
    throws std.postgres::pg_error, std.error::fault, std.test::failure {
    if (enabled() == false) { return; }
    std.postgres::connection scram = await std.postgres::connect(std.postgres::options::from_environment());
    std.postgres::rows who = await scram.query("SELECT current_user, version()", values());
    std.test::equal_text(who.items[0usize].text(0usize), "postgres");
    std.test::check(std.text::starts_with(who.items[0usize].text(1usize), "PostgreSQL"), "version");
    std.postgres::connection md5 = await std.postgres::connect(user("md5_user", "md5 secret"));
    std.postgres::rows md5_who = await md5.query("SELECT current_user", values());
    std.test::equal_text(md5_who.items[0usize].text(0usize), "md5_user");
    std.postgres::connection plain = await std.postgres::connect(user("plain_user", "plain secret"));
    std.postgres::rows plain_who = await plain.query("SELECT current_user", values());
    std.test::equal_text(plain_who.items[0usize].text(0usize), "plain_user");
    await (move plain).close();
    await (move md5).close();
    await (move scram).close();
}

@test
async void refuses_a_wrong_password_and_a_missing_database()
    throws std.postgres::pg_error, std.error::fault, std.test::failure {
    if (enabled() == false) { return; }
    try {
        std.postgres::connection wrong = await std.postgres::connect(user("postgres", "not the password"));
        drop wrong;
        std.test::check(false, "a wrong password is refused");
    } catch (std.postgres::pg_error refused) {
        std.test::check(refused.code == std.postgres::error_code::server, "server error");
        std.test::equal_text(refused.sqlstate.as_str(), "28P01");
    }
    std.postgres::options missing = std.postgres::options::from_environment();
    missing.database = std.string::from_str("no_such_database");
    try {
        std.postgres::connection absent = await std.postgres::connect(move missing);
        drop absent;
        std.test::check(false, "a missing database is refused");
    } catch (std.postgres::pg_error refused) {
        std.test::equal_text(refused.sqlstate.as_str(), "3D000");
    }
}

@test
async void connects_over_a_unix_domain_socket()
    throws std.postgres::pg_error, std.error::fault, std.test::failure {
    if (enabled() == false) { return; }
    std.string::string directory = setting("R_POSTGRES_SOCKET");
    std.postgres::options local = std.postgres::options::from_environment();
    str folder = directory.as_str();
    u16 port = local.port;
    local.socket = f"{folder}/.s.PGSQL.{port}";
    local.password = std.string::create();
    std.postgres::connection db = await std.postgres::connect(move local);
    std.postgres::rows found = await db.query("SELECT inet_client_addr() IS NULL", values());
    std.test::check(found.items[0usize].boolean(0usize), "a socket connection has no client address");
    await (move db).close();
}

@test
async void stores_and_reads_values()
    throws std.postgres::pg_error, std.error::fault, std.test::failure {
    if (enabled() == false) { return; }
    std.postgres::connection db = await std.postgres::connect(std.postgres::options::from_environment());
    await db.execute_script(
        "DROP TABLE IF EXISTS item;"
        "CREATE TABLE item(id bigserial PRIMARY KEY, name text, score float8, ratio float4, data bytea,"
        " ok boolean, amount numeric, small int2, middle int4, stamp date, doc jsonb)");
    u8[4] blob = {0u8, 1u8, 128u8, 255u8};
    array<std.postgres::value> first = values();
    add(&first, std.postgres::value::of_text("käse ✓"));
    add(&first, std.postgres::value::real(2.5));
    add(&first, std.postgres::value::real(0.125));
    add(&first, std.postgres::value::of_bytes(&blob));
    add(&first, std.postgres::value::boolean(true));
    add(&first, std.postgres::value::of_text("12345678901234567890.25"));
    add(&first, std.postgres::value::integer(-32768i64));
    add(&first, std.postgres::value::integer(2147483647i64));
    add(&first, std.postgres::value::of_text("2026-10-02"));
    add(&first, std.postgres::value::of_text("{\"a\": [1, 2]}"));
    u64 inserted = await db.execute(
        "INSERT INTO item(name, score, ratio, data, ok, amount, small, middle, stamp, doc) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10)", move first);
    std.test::equal(inserted, 1u64);
    array<std.postgres::value> second = values();
    add(&second, std.postgres::value::null_value);
    add(&second, std.postgres::value::real(1.0 / 0.0));
    add(&second, std.postgres::value::real(0.0 / 0.0));
    (await db.execute("INSERT INTO item(name, score, ratio) VALUES ($1, $2, $3)", move second)) as void;
    std.postgres::rows found = await db.query(
        "SELECT id, name, score, ratio, data, ok, amount, small, middle, stamp, doc FROM item ORDER BY id",
        values());
    std.test::equal(len(found.items), 2usize);
    std.test::equal(found.affected, 2u64);
    std.test::equal_text(found.columns[1usize].name.as_str(), "name");
    std.test::equal(found.columns[0usize].type_oid, 20u32);
    const std.postgres::row* first_row = &found.items[0usize];
    std.test::equal(first_row->integer(0usize), 1i64);
    std.test::equal_text(first_row->text(1usize), "käse ✓");
    std.test::check(first_row->real(2usize) == 2.5, "float8");
    std.test::check(first_row->real(3usize) == 0.125, "float4");
    const u8[] data = first_row->bytes(4usize);
    std.test::equal(len(data), 4usize);
    std.test::equal(data[2usize], 128u8);
    std.test::check(first_row->boolean(5usize), "boolean");
    std.test::equal_text(first_row->text(6usize), "12345678901234567890.25");
    std.test::equal(first_row->integer(7usize), -32768i64);
    std.test::equal(first_row->integer(8usize), 2147483647i64);
    std.test::equal_text(first_row->text(9usize), "2026-10-02");
    std.test::equal_text(first_row->text(10usize), "{\"a\": [1, 2]}");
    const std.postgres::row* second_row = &found.items[1usize];
    std.test::check(second_row->is_null(1usize), "NULL text");
    std.test::check(std.math::is_infinite_f64(second_row->real(2usize)), "Infinity");
    std.test::check(std.math::is_nan_f64(second_row->real(3usize)), "NaN");
    try {
        second_row->text(1usize) as void;
        std.test::check(false, "a NULL has no text");
    } catch (std.postgres::pg_error refused) {
        std.test::check(refused.code == std.postgres::error_code::null_value, "null_value");
    }
    try {
        first_row->integer(1usize) as void;
        std.test::check(false, "text is no integer");
    } catch (std.postgres::pg_error refused) {
        std.test::check(refused.code == std.postgres::error_code::invalid_value, "invalid_value");
    }
    try {
        first_row->at(11usize) as void;
        std.test::check(false, "no such column");
    } catch (std.postgres::pg_error refused) {
        std.test::check(refused.code == std.postgres::error_code::column_range, "column_range");
    }
    await (move db).close();
}

@test
async void reports_errors_and_keeps_the_connection()
    throws std.postgres::pg_error, std.error::fault, std.test::failure {
    if (enabled() == false) { return; }
    std.postgres::connection db = await std.postgres::connect(std.postgres::options::from_environment());
    await db.execute_script("DROP TABLE IF EXISTS keyed; CREATE TABLE keyed(id int PRIMARY KEY)");
    (await db.execute("INSERT INTO keyed VALUES (1)", values())) as void;
    try {
        (await db.execute("INSERT INTO keyed VALUES (1)", values())) as void;
        std.test::check(false, "a duplicate key is refused");
    } catch (std.postgres::pg_error refused) {
        std.test::equal_text(refused.sqlstate.as_str(), "23505");
        std.test::check(std.text::contains(refused.message.as_str(), "duplicate key"), "message");
        std.test::check(std.text::contains(refused.detail.as_str(), "(id)=(1)"), "detail");
    }
    try {
        (await db.query("SELEC 1", values())) as void;
        std.test::check(false, "a syntax error is refused");
    } catch (std.postgres::pg_error refused) {
        std.test::equal_text(refused.sqlstate.as_str(), "42601");
    }
    try {
        (await db.query("SELECT $1::int", values())) as void;
        std.test::check(false, "a missing parameter is refused");
    } catch (std.postgres::pg_error refused) {
        std.test::equal_text(refused.sqlstate.as_str(), "08P01");
    }
    std.postgres::rows after = await db.query("SELECT count(*) FROM keyed", values());
    std.test::equal(after.items[0usize].integer(0usize), 1i64);
    await (move db).close();
}

@test
async void runs_transactions() throws std.postgres::pg_error, std.error::fault, std.test::failure {
    if (enabled() == false) { return; }
    std.postgres::connection db = await std.postgres::connect(std.postgres::options::from_environment());
    await db.execute_script("DROP TABLE IF EXISTS ledger; CREATE TABLE ledger(amount int NOT NULL)");
    std.test::check(db.transaction_status() == std.postgres::transaction_status::idle, "idle");
    await db.begin();
    std.test::check(db.transaction_status() == std.postgres::transaction_status::in_transaction, "begun");
    (await db.execute("INSERT INTO ledger VALUES (10)", values())) as void;
    await db.commit();
    await db.begin();
    (await db.execute("INSERT INTO ledger VALUES (20)", values())) as void;
    try {
        (await db.execute("INSERT INTO ledger VALUES (NULL)", values())) as void;
    } catch (std.postgres::pg_error refused) {
        std.test::equal_text(refused.sqlstate.as_str(), "23502");
    }
    std.test::check(db.transaction_status() == std.postgres::transaction_status::failed, "failed");
    await db.rollback();
    std.test::check(db.transaction_status() == std.postgres::transaction_status::idle, "idle again");
    std.postgres::rows total = await db.query("SELECT sum(amount) FROM ledger", values());
    std.test::equal(total.items[0usize].integer(0usize), 10i64);
    await (move db).close();
}

@test
async void runs_prepared_statements_and_scripts()
    throws std.postgres::pg_error, std.error::fault, std.test::failure {
    if (enabled() == false) { return; }
    std.postgres::connection db = await std.postgres::connect(std.postgres::options::from_environment());
    await db.execute_script("DROP TABLE IF EXISTS counter; CREATE TABLE counter(n int NOT NULL);"
                            "INSERT INTO counter SELECT generate_series(1, 5)");
    std.postgres::statement above = await db.prepare("SELECT count(*), sum(n) FROM counter WHERE n > $1");
    std.postgres::rows two = await above.query(one(std.postgres::value::integer(2i64)));
    std.test::equal(two.items[0usize].integer(0usize), 3i64);
    std.test::equal(two.items[0usize].integer(1usize), 12i64);
    std.postgres::rows four = await above.query(one(std.postgres::value::integer(4i64)));
    std.test::equal(four.items[0usize].integer(1usize), 5i64);
    std.postgres::statement remove = await db.prepare("DELETE FROM counter WHERE n % 2 = $1");
    std.test::equal(await remove.execute(one(std.postgres::value::integer(0i64))), 2u64);
    await (move remove).close();
    await (move above).close();
    try {
        await db.execute_script("INSERT INTO counter VALUES (100); SELECT 1/0; INSERT INTO counter VALUES (200)");
        std.test::check(false, "division by zero ends the script");
    } catch (std.postgres::pg_error refused) {
        std.test::equal_text(refused.sqlstate.as_str(), "22012");
    }
    std.postgres::rows left = await db.query("SELECT count(*) FROM counter", values());
    std.test::equal(left.items[0usize].integer(0usize), 3i64);
    await (move db).close();
}

@test
async void listens_and_notifies() throws std.postgres::pg_error, std.error::fault, std.test::failure {
    if (enabled() == false) { return; }
    std.postgres::connection listener = await std.postgres::connect(std.postgres::options::from_environment());
    std.postgres::connection sender = await std.postgres::connect(std.postgres::options::from_environment());
    await listener.listen("job \"queue\"");
    (await sender.notify("job \"queue\"", "first")) as void;
    (await sender.notify("job \"queue\"", "second ✓")) as void;
    std.postgres::notification one_notice = await listener.wait_notification();
    std.postgres::notification two_notice = await listener.wait_notification();
    std.test::equal_text(one_notice.channel.as_str(), "job \"queue\"");
    std.test::equal_text(one_notice.payload.as_str(), "first");
    std.test::equal_text(two_notice.payload.as_str(), "second ✓");
    std.test::check(one_notice.process_id != 0u32, "process");
    await listener.unlisten("job \"queue\"");
    (await sender.notify("job \"queue\"", "unheard")) as void;
    std.postgres::rows ping = await listener.query("SELECT 1", values());
    std.test::equal(ping.items[0usize].integer(0usize), 1i64);
    await (move sender).close();
    await (move listener).close();
}

@test
async void copies_in_and_out() throws std.postgres::pg_error, std.error::fault, std.test::failure {
    if (enabled() == false) { return; }
    std.postgres::connection db = await std.postgres::connect(std.postgres::options::from_environment());
    await db.execute_script("DROP TABLE IF EXISTS copied; CREATE TABLE copied(n int, t text)");
    std.string::string csv = std.string::from_str("1,one\n2,\"two, too\"\n3,three\n");
    u64 loaded = await db.copy_in("COPY copied FROM STDIN WITH (FORMAT csv)", (move csv).into_bytes());
    std.test::equal(loaded, 3u64);
    bytes dumped = await db.copy_out("COPY (SELECT * FROM copied ORDER BY n) TO STDOUT WITH (FORMAT csv)");
    std.string::string text = std.string::from_utf8(dumped.as_slice());
    std.test::equal_text(text.as_str(), "1,one\n2,\"two, too\"\n3,three\n");
    try {
        std.string::string bad = std.string::from_str("x,y\n");
        (await db.copy_in("COPY copied FROM STDIN WITH (FORMAT csv)", (move bad).into_bytes())) as void;
        std.test::check(false, "a malformed row is refused");
    } catch (std.postgres::pg_error refused) {
        std.test::equal_text(refused.sqlstate.as_str(), "22P02");
    }
    try {
        bytes nothing = {};
        (await db.copy_in("COPY nothing FROM STDIN", move nothing)) as void;
        std.test::check(false, "a missing table is refused");
    } catch (std.postgres::pg_error refused) {
        std.test::equal_text(refused.sqlstate.as_str(), "42P01");
    }
    std.postgres::rows count = await db.query("SELECT count(*) FROM copied", values());
    std.test::equal(count.items[0usize].integer(0usize), 3i64);
    // COPY run as an ordinary statement: FROM STDIN has no data and fails, TO STDOUT is counted.
    try {
        (await db.execute("COPY copied FROM STDIN", values())) as void;
        std.test::check(false, "COPY FROM STDIN outside copy_in fails");
    } catch (std.postgres::pg_error refused) {
        std.test::equal_text(refused.sqlstate.as_str(), "57014");
    }
    std.test::equal(await db.execute("COPY copied TO STDOUT", values()), 3u64);
    std.postgres::rows still = await db.query("SELECT count(*) FROM copied", values());
    std.test::equal(still.items[0usize].integer(0usize), 3i64);
    await (move db).close();
}

protected async u32 sleeper(std.postgres::connection db) throws std.postgres::pg_error, std.error::fault {
    try {
        std.postgres::rows slept = await db.query("SELECT pg_sleep(30)", values());
        drop slept;
    } catch (std.postgres::pg_error refused) {
        if (same(refused.sqlstate.as_str(), "57014") == true) { return 1u32; }
        throw move refused;
    }
    return 0u32;
}

@test
async void cancels_a_running_statement() throws std.postgres::pg_error, std.error::fault, std.test::failure {
    if (enabled() == false) { return; }
    std.postgres::connection db = await std.postgres::connect(std.postgres::options::from_environment());
    std.postgres::canceller key = db.canceller();
    u32 cancelled = 0u32;
    task_scope(2) group {
        auto running = sleeper(db.share());
        await std.time::sleep_for(std.time::duration_from_parts(0i64, 300000000u32));
        await std.postgres::cancel(move key);
        cancelled += await move running;
    }
    std.test::equal(cancelled, 1u32);
    std.postgres::rows after = await db.query("SELECT 2", values());
    std.test::equal(after.items[0usize].integer(0usize), 2i64);
    await (move db).close();
}

protected async i64 counted(std.postgres::connection db, i64 limit) throws std.postgres::pg_error, std.error::fault {
    std.postgres::rows found = await db.query("SELECT count(*) FROM generate_series(1, $1)",
                                              one(std.postgres::value::integer(limit)));
    return found.items[0usize].integer(0usize);
}

@test
async void shares_a_connection_between_tasks_and_pools_connections()
    throws std.postgres::pg_error, std.error::fault, std.test::failure {
    if (enabled() == false) { return; }
    std.postgres::connection db = await std.postgres::connect(std.postgres::options::from_environment());
    for (i32 round = 0; round < 10; round += 1) {
        task_scope(2) group {
            auto low = counted(db.share(), 20000i64);
            auto high = counted(db.share(), 30000i64);
            std.test::equal(await move low, 20000i64);
            std.test::equal(await move high, 30000i64);
        }
    }
    await (move db).close();
    std.pool::pool<std.postgres::connection> pool =
        await std.postgres::connect_pool(std.postgres::options::from_environment(), 2usize);
    std.test::equal(pool.available(), 2usize);
    o<std.pool::lease<std.postgres::connection>> taken = pool.acquire();
    switch (move taken) {
    case variant o::some(move lease):
        std.test::equal(pool.available(), 1usize);
        std.postgres::rows answer = await (lease.get())->query("SELECT 41 + 1", values());
        std.test::equal(answer.items[0usize].integer(0usize), 42i64);
        drop lease;
    case variant o::none: std.test::check(false, "a free connection");
    }
    std.test::equal(pool.available(), 2usize);
}

@test
async void connects_over_tls_and_ends_a_closed_connection()
    throws std.postgres::pg_error, std.tls::tls_error, std.error::fault, std.test::failure {
    if (enabled() == false) { return; }
    std.string::string authority_path = setting("R_POSTGRES_TLS_AUTHORITY");
    std.fs::path path = std.fs::path_from_utf8(authority_path.as_str());
    bytes authority = await std.fs::read_file(&path, 65536usize);
    std.tls::config settings = std.tls::client_config();
    settings.add_authority(authority.as_slice());
    arc std.tls::config shared = new arc std.tls::config(move settings);
    std.postgres::options remote = std.postgres::options::from_environment();
    remote.host = std.string::from_str("localhost");
    std.postgres::connection db = await std.postgres::connect_tls(move remote, move shared);
    std.postgres::rows found = await db.query("SELECT ssl FROM pg_stat_ssl WHERE pid = pg_backend_pid()", values());
    std.test::check(found.items[0usize].boolean(0usize), "the session uses TLS");
    std.postgres::connection other = db.share();
    await (move db).close();
    try {
        (await other.query("SELECT 1", values())) as void;
        std.test::check(false, "a closed connection is refused");
    } catch (std.postgres::pg_error refused) {
        std.test::check(refused.code == std.postgres::error_code::closed, "closed");
    }
}

/* Ends the session of session from the server side and waits until its backend is gone. */
protected async void ended_by_server(std.postgres::connection session, std.postgres::connection watcher)
    throws std.postgres::pg_error, std.error::fault, std.test::failure {
    std.postgres::rows identity = await session.query("SELECT pg_backend_pid()", values());
    i64 pid = identity.items[0usize].integer(0usize);
    std.postgres::rows stopped = await watcher.query("SELECT pg_terminate_backend($1::int, 10000)",
                                                     one(std.postgres::value::integer(pid)));
    std.test::check(stopped.items[0usize].boolean(0usize), "the server ends the session");
    await (move session).close();
}

/* close of a session that the server has already ended is no failure, over TCP and over TLS: the
   transport may be reset before the shutdown that follows Terminate (R-SLIB-PG-0005). */
@test
async void closes_sessions_that_the_server_ended()
    throws std.postgres::pg_error, std.tls::tls_error, std.error::fault, std.test::failure {
    if (enabled() == false) { return; }
    std.postgres::connection watcher = await std.postgres::connect(std.postgres::options::from_environment());
    std.string::string authority_path = setting("R_POSTGRES_TLS_AUTHORITY");
    std.fs::path path = std.fs::path_from_utf8(authority_path.as_str());
    bytes authority = await std.fs::read_file(&path, 65536usize);
    for (u32 round = 0u32; round < 6u32; round += 1u32) {
        std.postgres::connection plain = await std.postgres::connect(std.postgres::options::from_environment());
        await ended_by_server(move plain, watcher.share());
        std.tls::config settings = std.tls::client_config();
        settings.add_authority(authority.as_slice());
        arc std.tls::config shared = new arc std.tls::config(move settings);
        std.postgres::options remote = std.postgres::options::from_environment();
        remote.host = std.string::from_str("localhost");
        std.postgres::connection secure = await std.postgres::connect_tls(move remote, move shared);
        await ended_by_server(move secure, watcher.share());
    }
    drop authority;
    await (move watcher).close();
}
