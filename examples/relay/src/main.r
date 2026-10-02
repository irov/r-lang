module example.relay.main;
import std.console;
import std.stream;
import example.relay.lines;
import example.relay.channel;

enum Command { number, fields, count, echo, pipe, ask };

/* The words after the command, owned. */
array<std.string::string> words(array<std.string::string> arguments)
    throws std.array::push_error<std.string::string> {
    array<std.string::string> selected = std.array::create::<std.string::string>();
    usize count = len(arguments);
    for (usize index = 2usize; index < count; index += 1usize) {
        std.array::push(&selected, core::replace(&arguments[index], std.string::create()));
    }
    return move selected;
}

/* The source of count: the file that the argument names, or standard input. */
async own dyn(std.stream::Reader)* source(array<std.string::string> arguments)
    throws std.error::fault {
    if (len(arguments) == 3usize) {
        std.fs::path path = std.fs::path_from_utf8(arguments[2].as_str());
        std.fs::open_file_options reading = std.fs::open_file_options {.access = std.fs::access::read,
            .create = std.fs::create_mode::existing, .truncate = false, .append = false,
            .follow_final_symlink = false};
        std.fs::file file = await path.open_file(reading);
        return new std.fs::file(move file);
    }
    return new std.io::input(std.io::stdin());
}

async i32 ask() throws std.error::fault {
    usize limit = std.console::max_line;
    await std.console::print(f"name (up to {limit} bytes)? ");
    o<std.string::string> answer = await std.console::read_line();
    i32 status = 0;
    switch (move answer) {
    case variant o::some(move name):
        await std.console::println(f"hello, {name}");
    case variant o::none:
        await std.console::eprintln(std.string::from_str("relay: no name given"));
        status = 65;
    }
    return status;
}

async i32 main() {
    array<std.string::string> arguments = std.env::arguments();
    usize given = len(arguments);
    o<Command> command = o::none;
    if (given >= 2usize) { command = core::enum_from_name::<Command>(arguments[1].as_str()); }
    switch (command) {
    case variant o::none:
        drop arguments;
        await std.console::eprint(std.string::from_str(
            "relay number|ask\nrelay fields FILE\nrelay count [FILE]\nrelay echo|pipe WORD...\n"));
        if (given == 1usize) { return 0; }
        return 64;
    case variant o::some(chosen):
        switch (*chosen) {
        case Command::number:
            drop arguments;
            u32 lines = await example.relay.lines::number_lines();
            if (lines == 0u32) { return 65; }
            return 0;
        case Command::ask:
            drop arguments;
            return await ask();
        case Command::fields:
            if (given != 3usize) {
                drop arguments;
                return 64;
            }
            std.fs::path path = std.fs::path_from_utf8(arguments[2].as_str());
            drop arguments;
            std.fs::open_file_options reading = std.fs::open_file_options {.access = std.fs::access::read,
                .create = std.fs::create_mode::existing, .truncate = false, .append = false,
                .follow_final_symlink = false};
            std.fs::file file = await path.open_file(reading);
            o<std.string::string> text = await example.relay.lines::read_fields(move file);
            i32 status = 0;
            switch (move text) {
            case variant o::some(move value): await std.console::print(move value);
            case variant o::none:
                await std.console::eprintln(std.string::from_str("relay: not a record file"));
                status = 65;
            }
            return status;
        case Command::count:
            if (given > 3usize) {
                drop arguments;
                return 64;
            }
            own dyn(std.stream::Reader)* input = await source(move arguments);
            await std.console::print(await example.relay.lines::measure(move input));
            return 0;
        case Command::echo:
            if (given < 3usize) {
                drop arguments;
                return 64;
            }
            try {
                array<std.string::string> selected = words(move arguments);
                await std.console::print(await example.relay.channel::echo_words(move selected));
            } catch (std.array::push_error<std.string::string> failure) {
                return 71;
            }
            return 0;
        case Command::pipe:
            if (given < 3usize) {
                drop arguments;
                return 64;
            }
            try {
                array<std.string::string> selected = words(move arguments);
                await std.console::print(await example.relay.channel::pipe_words(move selected));
            } catch (std.array::push_error<std.string::string> failure) {
                return 71;
            }
            return 0;
        }
    }
    return 70;
}
