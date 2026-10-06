module example.registry.main;
import std.console;
import std.convert;
import std.sqlite;
import std.json;
import example.registry.store;

/* registry keeps devices in a SQLite database with a transactional outbox: every change of a
   device and the event that announces it are committed together, and deliver relays the
   committed events in order. */
enum Command { init, register, rename, retire, show, outbox, deliver, demo };

error Usage { u32 code; };

protected const str usage_text =
    "registry init DB | register DB ID NAME [WEIGHT] | rename DB ID NAME | retire DB ID NOTE\n"
    "registry show DB | outbox DB | deliver DB [LIMIT] | demo DB\n";

str word(const array<std.string::string>* arguments, usize index) {
    return (*arguments)[index];
}

protected std.string::string owned(const array<std.string::string>* arguments, usize index)
    throws std.alloc::alloc_error {
    return std.string::from_str(word(arguments, index));
}

async void say(std.string::string text) throws std.error::fault {
    await std.console::println(move text);
}

/* Two connections to one file in write-ahead-log mode: a reader keeps seeing the last commit
   while a writer holds a transaction, and a second writer that will not wait is refused. */
async std.string::string snapshots(std.string::string path) throws std.sqlite::sqlite_error, std.error::fault {
    array<std.sqlite::value> none = [];
    std.sqlite::database writer = await std.sqlite::open(path, example.registry.store::writing());
    std.sqlite::database reader =
        await std.sqlite::open(path, std.sqlite::options {.create = false, .busy_timeout_ms = 0u32});
    await writer.begin(std.sqlite::begin_mode::immediate);
    std.sqlite::execution added = await writer.execute(
        "INSERT INTO device(id, name, revision, active) VALUES ('probe-9', 'probe', 1, 1)", move none);
    array<std.sqlite::value> count_values = [];
    std.sqlite::rows before = await reader.query("SELECT count(*) FROM device", move count_values);
    i64 seen = before.items[0usize].integer(0usize);
    bool refused = false;
    std.sqlite::error_code code = std.sqlite::error_code::other;
    try {
        await reader.begin(std.sqlite::begin_mode::immediate);
        await reader.rollback();
    } catch (std.sqlite::sqlite_error failure) {
        refused = true;
        code = failure.code;
    }
    await writer.rollback();
    array<std.sqlite::value> again_values = [];
    std.sqlite::rows after = await reader.query("SELECT count(*) FROM device", move again_values);
    i64 kept = after.items[0usize].integer(0usize);
    u64 changed = added.changes;
    await (move reader).close();
    await (move writer).close();
    return f"writer inserted {changed}, reader saw {seen}, second writer refused: {refused} ({code}), after rollback {kept}";
}

async i32 main() {
    array<std.string::string> arguments = std.env::arguments();
    usize given = len(arguments);
    o<Command> command = o::none;
    if (given >= 2usize) { command = core::enum_from_name::<Command>(arguments[1]); }
    switch (command) {
    case variant o::none:
        await std.console::eprint(std.string::from_str(usage_text));
        if (given == 1usize) { return 0; }
        return 64;
    case variant o::some(selected):
        try {
            throw (given < 3usize) Usage {.code = 2u32};
            std.string::string path = owned(&arguments, 2usize);
            switch (*selected) {
            case Command::init:
                std.string::string journal = await example.registry.store::create(move path);
                await say(f"ready (journal {journal})");
            case Command::register:
                throw (given != 5usize && given != 6usize) Usage {.code = 2u32};
                o<f64> weight = o::none;
                if (given == 6usize) { weight = o::some(std.convert::parse_f64(word(&arguments, 5usize))); }
                i64 event = await example.registry.store::register(
                    move path, owned(&arguments, 3usize), owned(&arguments, 4usize), weight);
                await say(f"registered, event {event}");
            case Command::rename:
                throw (given != 5usize) Usage {.code = 2u32};
                i64 event = await example.registry.store::rename(
                    move path, owned(&arguments, 3usize), owned(&arguments, 4usize));
                await say(f"renamed, event {event}");
            case Command::retire:
                throw (given != 5usize) Usage {.code = 2u32};
                i64 event = await example.registry.store::retire(
                    move path, owned(&arguments, 3usize), owned(&arguments, 4usize));
                await say(f"retired, event {event}");
            case Command::show:
                throw (given != 3usize) Usage {.code = 2u32};
                await std.console::print(await example.registry.store::listing(move path));
            case Command::outbox:
                throw (given != 3usize) Usage {.code = 2u32};
                await std.console::print(await example.registry.store::pending(move path));
            case Command::deliver:
                throw (given != 3usize && given != 4usize) Usage {.code = 2u32};
                i64 limit = 100i64;
                if (given == 4usize) { limit = std.convert::parse_i64(word(&arguments, 3usize), 10u32); }
                await std.console::print(await example.registry.store::deliver(move path, limit));
            case Command::demo:
                throw (given != 3usize) Usage {.code = 2u32};
                std.string::string journal = await example.registry.store::create(std.string::from_str(path));
                await say(f"ready (journal {journal})");
                i64 first = await example.registry.store::register(std.string::from_str(path),
                    std.string::from_str("sensor-1"), std.string::from_str("boiler"), o::some(2.5));
                i64 second = await example.registry.store::register(std.string::from_str(path),
                    std.string::from_str("sensor-2"), std.string::from_str("attic"), o::none);
                await say(f"registered sensor-1 (event {first}) and sensor-2 (event {second})");
                try {
                    i64 twice = await example.registry.store::register(std.string::from_str(path),
                        std.string::from_str("sensor-1"), std.string::from_str("copy"), o::none);
                    twice as void;
                } catch (example.registry.store::RegistryError refused) {
                    example.registry.store::Refusal reason = refused.reason;
                    await say(f"sensor-1 again: {reason}, nothing published");
                }
                i64 renamed = await example.registry.store::rename(std.string::from_str(path),
                    std.string::from_str("sensor-2"), std.string::from_str("loft"));
                i64 retired = await example.registry.store::retire(std.string::from_str(path),
                    std.string::from_str("sensor-1"), std.string::from_str("replaced"));
                await say(f"renamed sensor-2 (event {renamed}), retired sensor-1 (event {retired})");
                await say(await snapshots(std.string::from_str(path)));
                await std.console::print(await example.registry.store::pending(std.string::from_str(path)));
                await std.console::print(await example.registry.store::deliver(std.string::from_str(path), 3i64));
                await std.console::print(await example.registry.store::deliver(std.string::from_str(path), 3i64));
                await std.console::print(await example.registry.store::listing(move path));
            }
        } catch (Usage failure) {
            failure as void;
            await std.console::eprint(std.string::from_str(usage_text));
            return 64;
        } catch (example.registry.store::RegistryError refused) {
            example.registry.store::Refusal reason = refused.reason;
            await std.console::eprintln(f"refused: {reason}");
            return 65;
        } catch (std.sqlite::sqlite_error failure) {
            std.sqlite::error_code code = failure.code;
            i32 native = failure.native_code;
            std.string::string message = std.string::from_str(failure.message);
            await std.console::eprintln(f"sqlite: {code} ({native}) {message}");
            if (code == std.sqlite::error_code::cannot_open) { return 66; }
            return 70;
        } catch (std.convert::parse_error failure) {
            failure as void;
            await std.console::eprint(std.string::from_str(usage_text));
            return 64;
        } catch (std.json::error failure) {
            drop failure;
            return 70;
        } catch (std.error::fault failure) {
            failure as void;
            return 71;
        }
    }
    return 0;
}
