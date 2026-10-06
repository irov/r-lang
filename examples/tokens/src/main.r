module example.tokens.main;
import std.console;
import std.encoding;
import example.tokens.ids;
import example.tokens.codes;
import example.tokens.draws;

enum Command { id, sign, verify, digest, noise, pick, dice, shuffle };

error Usage { u32 code; };

protected const str usage_text =
    "tokens id v4|v7 COUNT | id parse TEXT | id at MILLISECONDS HEX20\n"
    "tokens sign KEY MESSAGE | verify KEY MESSAGE HEX | digest < INPUT\n"
    "tokens noise COUNT | pick LOW HIGH | dice SEED COUNT | shuffle SEED ITEM...\n";

str word(const array<std.string::string>* arguments, usize index) {
    return (*arguments)[index];
}

u64 number(const array<std.string::string>* arguments, usize index) throws Usage {
    try {
        return std.convert::parse_u64(word(arguments, index), 0u32);
    } catch (std.convert::parse_error failure) {
        failure as void;
        throw Usage {.code = 1u32};
    }
}

/* The output of a synchronous command. */
std.string::string run(Command command, const array<std.string::string>* arguments)
    throws Usage, std.convert::parse_error, std.time::time_error, std.alloc::alloc_error {
    usize given = len(*arguments);
    switch (command) {
    case Command::id:
        throw (given < 3usize) Usage {.code = 2u32};
        str form = word(arguments, 2usize);
        if (std.bytes::equal(form, "parse") == true) {
            throw (given != 4usize) Usage {.code = 2u32};
            return example.tokens.ids::described(word(arguments, 3usize));
        }
        if (std.bytes::equal(form, "at") == true) {
            throw (given != 5usize) Usage {.code = 2u32};
            u64 milliseconds = number(arguments, 3usize);
            bytes random = std.encoding::decode_hex(word(arguments, 4usize));
            throw (len(random) != 10usize) Usage {.code = 3u32};
            return example.tokens.ids::built(milliseconds, random.as_slice());
        }
        throw (given != 4usize) Usage {.code = 2u32};
        u64 count = number(arguments, 3usize);
        throw (count > 1000u64) Usage {.code = 3u32};
        if (std.bytes::equal(form, "v7") == true) {
            return example.tokens.ids::fresh(true, count as u32);
        }
        throw (std.bytes::equal(form, "v4") == false) Usage {.code = 2u32};
        return example.tokens.ids::fresh(false, count as u32);
    case Command::sign:
        throw (given != 4usize) Usage {.code = 2u32};
        return example.tokens.codes::signed(word(arguments, 2usize), word(arguments, 3usize));
    case Command::noise:
        throw (given != 3usize) Usage {.code = 2u32};
        u64 count = number(arguments, 2usize);
        throw (count > 4096u64) Usage {.code = 3u32};
        return example.tokens.draws::fresh_bytes(count as usize);
    case Command::pick:
        throw (given != 4usize) Usage {.code = 2u32};
        u64 low = number(arguments, 2usize);
        u64 high = number(arguments, 3usize);
        throw (low >= high) Usage {.code = 3u32};
        return example.tokens.draws::picked(low, high);
    case Command::dice:
        throw (given != 4usize) Usage {.code = 2u32};
        u64 seed = number(arguments, 2usize);
        u64 count = number(arguments, 3usize);
        throw (count > 1000u64) Usage {.code = 3u32};
        return example.tokens.draws::rolled(seed, count as u32);
    case Command::shuffle:
        throw (given < 3usize) Usage {.code = 2u32};
        u64 seed = number(arguments, 2usize);
        array<str> items = std.array::create::<str>();
        for (usize index = 3usize; index < given; index += 1usize) {
            try {
                std.array::push(&items, word(arguments, index));
            } catch (std.array::push_error<str> failure) {
                failure as void;
                throw std.alloc::alloc_error::out_of_memory;
            }
        }
        return example.tokens.draws::shuffled(seed, items.as_slice());
    case Command::verify:
    case Command::digest:
        throw Usage {.code = 4u32};
    }
    throw Usage {.code = 4u32};
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
            if (*selected == Command::digest) {
                throw (given != 2usize) Usage {.code = 2u32};
                std.string::string out = await example.tokens.codes::digested();
                await std.console::print(move out);
                return 0;
            }
            if (*selected == Command::verify) {
                throw (given != 5usize) Usage {.code = 2u32};
                bool valid = example.tokens.codes::verified(
                    word(&arguments, 2usize), word(&arguments, 3usize), word(&arguments, 4usize));
                if (valid == true) {
                    await std.console::println(std.string::from_str("valid"));
                    return 0;
                }
                await std.console::println(std.string::from_str("invalid"));
                return 65;
            }
            std.string::string out = run(*selected, &arguments);
            await std.console::print(move out);
            return 0;
        } catch (Usage failure) {
            failure as void;
            await std.console::eprint(std.string::from_str(usage_text));
            return 64;
        } catch (std.convert::parse_error failure) {
            usize index = failure.index;
            str code = core::enum_name(failure.code);
            await std.console::eprint(f"tokens: {code} at byte {index}\n");
            return 65;
        }
    }
    return 70;
}
