module example.registry.store;
import std.sqlite;
import std.json;
import std.encoding;
import std.hash;

/* The devices of the registry and the outbox of their events. A change of a device and its event
   are written in one transaction, so a reader of the outbox sees an event exactly when the change
   that it reports was committed (the transactional outbox). */
const str SCHEMA =
    "CREATE TABLE IF NOT EXISTS device(id TEXT PRIMARY KEY, name TEXT NOT NULL, "
    "revision INTEGER NOT NULL, active INTEGER NOT NULL, weight REAL, note TEXT, fingerprint BLOB);"
    "CREATE TABLE IF NOT EXISTS outbox(seq INTEGER PRIMARY KEY AUTOINCREMENT, topic TEXT NOT NULL, "
    "payload TEXT NOT NULL, delivered INTEGER NOT NULL DEFAULT 0)";

/* Why a change of the registry was refused. */
@derive(format)
enum Refusal { already_registered, unknown_device };

error RegistryError { Refusal reason; };

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

/* A connection that writes waits up to two seconds for another writer; the file shall exist. */
std.sqlite::options writing() {
    return std.sqlite::options {.create = false, .busy_timeout_ms = 2000u32};
}

/* The JSON payload of an event about a device. */
protected std.string::string payload(str id, str name) throws std.alloc::alloc_error, std.json::error {
    std.json::value event = std.json::object();
    event.insert("id", std.json::from_string(id));
    event.insert("name", std.json::from_string(name));
    return std.json::stringify(&event);
}

/* Appends an event to the outbox in the transaction of db. */
protected task<std.sqlite::execution throws std.sqlite::sqlite_error, std.alloc::alloc_error>
publish(const std.sqlite::database* db, str topic, str id, str name)
    throws std.async::start_error, std.alloc::alloc_error, std.json::error {
    array<std.sqlite::value> parameters = values();
    add(&parameters, std.sqlite::value::of_text(topic));
    add(&parameters, std.sqlite::value::text(payload(id, name)));
    return db->execute("INSERT INTO outbox(topic, payload) VALUES (?1, ?2)", move parameters);
}

/* Creates the tables in a new database file in write-ahead-log mode and reports the mode. */
async std.string::string create(std.string::string path) throws std.sqlite::sqlite_error, std.error::fault {
    std.sqlite::database db = await std.sqlite::open(path, std.sqlite::options {.wal = true});
    await db.execute_script(SCHEMA);
    std.sqlite::rows mode = await db.query("PRAGMA journal_mode", values());
    std.string::string journal = std.string::from_str(mode.items[0usize].text(0usize));
    await (move db).close();
    return move journal;
}

/* Registers a device and publishes device.registered in one transaction; a known identifier
   rolls both back. Returns the sequence number of the event. */
async i64 register(std.string::string path, std.string::string id, std.string::string name, o<f64> weight)
    throws RegistryError, std.sqlite::sqlite_error, std.json::error, std.error::fault {
    std.sqlite::database db = await std.sqlite::open(path, writing());
    array<std.sqlite::value> device = values();
    add(&device, std.sqlite::value::of_text(id));
    add(&device, std.sqlite::value::of_text(name));
    add(&device, std.sqlite::value::integer(1i64));
    add(&device, std.sqlite::value::of_bool(true));
    switch (weight) {
    case variant o::some(kilograms): add(&device, std.sqlite::value::real(*kilograms));
    case variant o::none: add(&device, std.sqlite::value::null_value);
    }
    add(&device, std.sqlite::value::null_value);
    // The first 8 bytes of the SHA-256 of the identifier.
    std.hash::sha256_digest digest = std.hash::sha256(id.as_bytes());
    add(&device, std.sqlite::value::of_blob(digest.bytes[0usize..8usize]));
    await db.begin(std.sqlite::begin_mode::immediate);
    try {
        std.sqlite::execution inserted = await db.execute(
            "INSERT INTO device(id, name, revision, active, weight, note, fingerprint) VALUES (?, ?, ?, ?, ?, ?, ?)",
            move device);
        inserted as void;
    } catch (std.sqlite::sqlite_error failure) {
        await db.rollback();
        if (failure.code == std.sqlite::error_code::constraint) {
            throw RegistryError {.reason = Refusal::already_registered};
        }
        throw move failure;
    }
    std.sqlite::execution event = await publish(&db, "device.registered", id, name);
    await db.commit();
    await (move db).close();
    return event.last_row_id;
}

/* Renames a device and publishes device.renamed in one transaction; an unknown identifier rolls
   back. */
async i64 rename(std.string::string path, std.string::string id, std.string::string name)
    throws RegistryError, std.sqlite::sqlite_error, std.json::error, std.error::fault {
    std.sqlite::database db = await std.sqlite::open(path, writing());
    array<std.sqlite::value> change = values();
    add(&change, std.sqlite::value::of_text(name));
    add(&change, std.sqlite::value::of_text(id));
    await db.begin(std.sqlite::begin_mode::immediate);
    std.sqlite::execution updated =
        await db.execute("UPDATE device SET name = ?1, revision = revision + 1 WHERE id = ?2", move change);
    if (updated.changes == 0u64) {
        await db.rollback();
        throw RegistryError {.reason = Refusal::unknown_device};
    }
    std.sqlite::execution event = await publish(&db, "device.renamed", id, name);
    await db.commit();
    await (move db).close();
    return event.last_row_id;
}

protected array<std.sqlite::value> one_text(str text) throws std.alloc::alloc_error {
    array<std.sqlite::value> listed = values();
    add(&listed, std.sqlite::value::of_text(text));
    return move listed;
}

/* Marks a device inactive with a note and publishes device.retired, in an exclusive
   transaction. */
async i64 retire(std.string::string path, std.string::string id, std.string::string note)
    throws RegistryError, std.sqlite::sqlite_error, std.json::error, std.error::fault {
    std.sqlite::database db = await std.sqlite::open(path, writing());
    await db.begin(std.sqlite::begin_mode::exclusive);
    array<std.sqlite::value> change = values();
    add(&change, std.sqlite::value::of_bool(false));
    add(&change, std.sqlite::value::of_text(note));
    add(&change, std.sqlite::value::of_text(id));
    std.sqlite::execution updated =
        await db.execute("UPDATE device SET active = ?1, note = ?2, revision = revision + 1 WHERE id = ?3",
                         move change);
    if (updated.changes == 0u64) {
        await db.rollback();
        throw RegistryError {.reason = Refusal::unknown_device};
    }
    std.sqlite::rows found = await db.query("SELECT name FROM device WHERE id = ?", one_text(id));
    std.sqlite::execution event =
        await publish(&db, "device.retired", id, found.items[0usize].text(0usize));
    await db.commit();
    await (move db).close();
    return event.last_row_id;
}

protected std.string::string weight_text(o<f64> weight) throws std.alloc::alloc_error {
    switch (weight) {
    case variant o::some(kilograms):
        f64 kilos = *kilograms;
        return f"{kilos} kg";
    case variant o::none: break;
    }
    return std.string::from_str("-");
}

protected std.string::string note_text(o<str> note) throws std.alloc::alloc_error {
    switch (note) {
    case variant o::some(text): return std.string::from_str(*text);
    case variant o::none: break;
    }
    return std.string::from_str("-");
}

protected std.string::string fingerprint_text(o<const u8[]> fingerprint) throws std.alloc::alloc_error {
    switch (fingerprint) {
    case variant o::some(data): return std.encoding::encode_hex(*data);
    case variant o::none: break;
    }
    return std.string::from_str("-");
}

/* One line for a device: typed reads of each column, NULL shown as -. */
protected std.string::string device_line(const std.sqlite::row* device)
    throws std.sqlite::sqlite_error, std.alloc::alloc_error {
    str id = device->text(0usize);
    str name = device->text(1usize);
    i64 revision = device->integer(2usize);
    bool active = device->boolean(3usize);
    std.string::string weight = weight_text(device->optional_real(4usize));
    std.string::string note = note_text(device->optional_text(5usize));
    std.string::string fingerprint = fingerprint_text(device->optional_blob(6usize));
    return f"{id} {name} revision {revision} active {active} weight {weight} note {note} fingerprint {fingerprint}";
}

/* The devices in the order of their identifiers. */
async std.string::string listing(std.string::string path) throws std.sqlite::sqlite_error, std.error::fault {
    std.sqlite::database db = await std.sqlite::open(path, std.sqlite::options {.read_only = true});
    std.sqlite::rows found =
        await db.query("SELECT id, name, revision, active, weight, note, fingerprint FROM device ORDER BY id",
                       values());
    std.string::string text = std.string::create();
    for (usize index = 0usize; index < len(found.items); index += 1usize) {
        std.string::string line = device_line(&found.items[index]);
        std.string::append_str(&text, line);
        std.string::append_str(&text, "\n");
    }
    await (move db).close();
    return move text;
}

/* A value with its storage class, as the outbox listing shows it. */
protected std.string::string shown(const std.sqlite::value* item) throws std.alloc::alloc_error {
    switch (*item) {
    case variant std.sqlite::value::null_value: return std.string::from_str("null");
    case variant std.sqlite::value::integer(number):
        i64 integer = *number;
        return f"{integer}";
    case variant std.sqlite::value::real(number):
        f64 real = *number;
        return f"{real}";
    case variant std.sqlite::value::text(data): return std.string::from_str(data->as_str());
    case variant std.sqlite::value::blob(data): return std.encoding::encode_hex(data->as_slice());
    }
}

/* The events not yet delivered, in order, as columns found by name. */
async std.string::string pending(std.string::string path) throws std.sqlite::sqlite_error, std.error::fault {
    std.sqlite::database db = await std.sqlite::open(path, std.sqlite::options {.read_only = true});
    std.sqlite::rows found =
        await db.query("SELECT seq, topic, payload FROM outbox WHERE delivered = 0 ORDER BY seq", values());
    std.string::string text = std.string::create();
    usize topic = 1usize;
    switch (found.column("topic")) {
    case variant o::some(index): topic = *index;
    case variant o::none: break;
    }
    for (usize index = 0usize; index < len(found.items); index += 1usize) {
        const std.sqlite::row* event = &found.items[index];
        std.string::string seq = shown(event->at(0usize));
        std.string::string kind = shown(event->at(topic));
        std.string::string body = shown(event->at(2usize));
        usize count = event->count();
        std.string::string line = f"{seq} {kind} {body} ({count} columns)\n";
        std.string::append_str(&text, line);
    }
    await (move db).close();
    return move text;
}

/* Delivers pending events in order: each is printed, as a relay would send it to a broker, and
   marked delivered in the same transaction that read it. */
async std.string::string deliver(std.string::string path, i64 limit) throws std.sqlite::sqlite_error, std.error::fault {
    std.sqlite::database db = await std.sqlite::open(path, writing());
    std.sqlite::statement next =
        await db.prepare("SELECT seq, topic, payload FROM outbox WHERE delivered = 0 ORDER BY seq LIMIT ?");
    std.sqlite::statement mark = await db.prepare("UPDATE outbox SET delivered = 1 WHERE seq = ?");
    await db.begin(std.sqlite::begin_mode::immediate);
    array<std.sqlite::value> bound = values();
    add(&bound, std.sqlite::value::integer(limit));
    std.sqlite::rows batch = await next.query(move bound);
    std.string::string text = std.string::create();
    u64 delivered = 0u64;
    for (usize index = 0usize; index < len(batch.items); index += 1usize) {
        const std.sqlite::row* event = &batch.items[index];
        i64 seq = event->integer(0usize);
        str topic = event->text(1usize);
        str body = event->text(2usize);
        std.string::string line = f"deliver {seq} {topic} {body}\n";
        std.string::append_str(&text, line);
        array<std.sqlite::value> which = values();
        add(&which, std.sqlite::value::integer(seq));
        std.sqlite::execution marked = await mark.execute(move which);
        delivered += marked.changes;
    }
    bool open = db.in_transaction();
    await db.commit();
    std.string::string summary = f"delivered {delivered} (transaction was open: {open})\n";
    std.string::append_str(&text, summary);
    drop next;
    drop mark;
    await (move db).close();
    return move text;
}
