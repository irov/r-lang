module example.journal.main;
import std.console;
import std.convert;
import std.time;
import example.journal.store;

/* journal keeps fixed-size records in a data file with a write-ahead journal: updates lock the
   whole file, reads lock one record, and a program that dies between the journal and the data
   write is repaired by recover. */
enum Command { init, put, get, scan, crash, recover, hold, busy, demo };

error Usage { u32 code; };

protected const str usage_text =
    "journal init DIR RECORDS | put DIR SLOT TEXT [WAIT_MS] | get DIR SLOT | scan DIR\n"
    "journal crash DIR SLOT TEXT | recover DIR | hold DIR MS | busy DIR | demo DIR\n";

str word(const array<std.string::string>* arguments, usize index) {
    return (*arguments)[index].as_str();
}

protected std.string::string owned(const array<std.string::string>* arguments, usize index)
    throws std.alloc::alloc_error {
    return std.string::from_str(word(arguments, index));
}

protected u32 number(const array<std.string::string>* arguments, usize index)
    throws std.convert::parse_error, Usage {
    u64 value = std.convert::parse_u64(word(arguments, index), 10u32);
    throw (value > 4294967295u64) Usage {.code = 3u32};
    return value as u32;
}

protected std.string::string copy_of(const std.string::string* text) throws std.alloc::alloc_error {
    return std.string::from_str(text->as_str());
}

async void say(std.string::string text) throws std.error::fault {
    await std.console::println(move text);
}

/* Every step in one program: updates, a lock of another open file that a probe sees and a
   waiting update outlasts, and an update interrupted after its journal and then recovered. */
async void demo(std.string::string directory) throws example.journal.store::store_error, std.error::fault {
    await example.journal.store::init(copy_of(&directory), 4usize);
    await say(f"store of 4 records");
    await example.journal.store::update(copy_of(&directory), 0u32, std.string::from_str("alpha"), 1000u32, false);
    await example.journal.store::update(copy_of(&directory), 1u32, std.string::from_str("beta"), 1000u32, false);
    std.string::string first = await example.journal.store::get(copy_of(&directory), 0u32);
    std.string::string second = await example.journal.store::get(copy_of(&directory), 1u32);
    await say(f"slot 0: {first}, slot 1: {second}");
    std.fs::path data_file = example.journal.store::data_path(&directory);
    std.fs::file holder = await data_file.open_file(example.journal.store::writing(false));
    await holder.lock(std.fs::lock_kind::shared, 0u64, 64u64);
    bool held = await example.journal.store::busy(copy_of(&directory));
    await say(f"another open file holds a shared lock of slot 0, a probe finds the store busy: {held}");
    task_scope(2) group {
        auto waiting = example.journal.store::update(copy_of(&directory), 2u32, std.string::from_str("delta"),
                                                     5000u32, false);
        await std.time::sleep_for(std.time::duration_from_parts(0i64, 20000000u32));
        await holder.unlock(0u64, 64u64);
        await move waiting;
    }
    await (move holder).close();
    std.string::string third = await example.journal.store::get(copy_of(&directory), 2u32);
    await say(f"an update waited for that lock and then wrote slot 2: {third}");
    await example.journal.store::update(copy_of(&directory), 1u32, std.string::from_str("gamma"), 1000u32, true);
    std.string::string before = await example.journal.store::get(copy_of(&directory), 1u32);
    await say(f"interrupted after the journal of slot 1, which still reads {before}");
    std.string::string replayed = await example.journal.store::recover(copy_of(&directory));
    std.string::string after = await example.journal.store::get(copy_of(&directory), 1u32);
    await say(f"{replayed}, slot 1 now reads {after}");
    std.string::string again = await example.journal.store::recover(copy_of(&directory));
    bool free = await example.journal.store::busy(copy_of(&directory));
    await say(f"a second recovery: {again}; busy: {free}");
}

async i32 main() {
    array<std.string::string> arguments = std.env::arguments();
    usize given = len(arguments);
    o<Command> command = o::none;
    if (given >= 2usize) { command = core::enum_from_name::<Command>(arguments[1].as_str()); }
    switch (command) {
    case variant o::none:
        await std.console::eprint(std.string::from_str(usage_text));
        if (given == 1usize) { return 0; }
        return 64;
    case variant o::some(selected):
        try {
            throw (given < 3usize) Usage {.code = 2u32};
            std.string::string directory = owned(&arguments, 2usize);
            switch (*selected) {
            case Command::init:
                throw (given != 4usize) Usage {.code = 2u32};
                u32 count = number(&arguments, 3usize);
                await example.journal.store::init(move directory, count as usize);
                await say(f"store of {count} records");
            case Command::put:
                throw (given != 5usize && given != 6usize) Usage {.code = 2u32};
                u32 slot = number(&arguments, 3usize);
                std.string::string text = owned(&arguments, 4usize);
                u32 wait_ms = 5000u32;
                if (given == 6usize) { wait_ms = number(&arguments, 5usize); }
                await example.journal.store::update(move directory, slot, copy_of(&text), wait_ms, false);
                await say(f"slot {slot}: {text}");
            case Command::get:
                throw (given != 4usize) Usage {.code = 2u32};
                u32 slot = number(&arguments, 3usize);
                await say(await example.journal.store::get(move directory, slot));
            case Command::scan:
                throw (given != 3usize) Usage {.code = 2u32};
                std.string::string listing = await example.journal.store::scan(move directory);
                await std.console::print(move listing);
            case Command::crash:
                throw (given != 5usize) Usage {.code = 2u32};
                u32 slot = number(&arguments, 3usize);
                await example.journal.store::update(move directory, slot, owned(&arguments, 4usize), 5000u32, true);
                await say(f"journal of slot {slot} written, data not written");
            case Command::recover:
                throw (given != 3usize) Usage {.code = 2u32};
                await say(await example.journal.store::recover(move directory));
            case Command::hold:
                throw (given != 4usize) Usage {.code = 2u32};
                u32 ms = number(&arguments, 3usize);
                await example.journal.store::hold(move directory, ms);
                await say(f"released");
            case Command::busy:
                throw (given != 3usize) Usage {.code = 2u32};
                bool taken = await example.journal.store::busy(move directory);
                if (taken == true) { await say(f"busy"); } else { await say(f"free"); }
            case Command::demo:
                throw (given != 3usize) Usage {.code = 2u32};
                await demo(move directory);
            }
        } catch (Usage failure) {
            failure as void;
            await std.console::eprint(std.string::from_str(usage_text));
            return 64;
        } catch (std.convert::parse_error failure) {
            failure as void;
            await std.console::eprint(std.string::from_str(usage_text));
            return 64;
        } catch (example.journal.store::store_error failure) {
            u32 slot = failure.slot;
            await std.console::eprintln(f"no slot {slot} in the store");
            return 65;
        } catch (std.fs::fs_error failure) {
            std.fs::error_code code = failure.code;
            str name = core::enum_name(code);
            await std.console::eprintln(f"file system: {name}");
            if (code == std.fs::error_code::timed_out) { return 75; }
            return 74;
        } catch (std.error::fault failure) {
            failure as void;
            return 71;
        }
    }
    return 0;
}
