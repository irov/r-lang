module example.textkit.main;
import std.console;
import std.regex;
import example.textkit.words;
import example.textkit.codes;
import example.textkit.numbers;
import example.textkit.patterns;

enum Command { fields, lines, scalars, fold, join, replace, encode, decode, edit, insert, pack,
               stats, groups };

error Usage { u32 code; };

protected const str usage_text =
    "textkit fields LINE SEPARATOR | lines TEXT | scalars TEXT | fold TEXT\n"
    "textkit join SEPARATOR PART... | replace TEXT PATTERN REPLACEMENT\n"
    "textkit encode TEXT | decode base64|base64url|hex|percent TEXT\n"
    "textkit edit TEXT START END REPLACEMENT | insert TEXT AT INSERTION\n"
    "textkit pack NUMBER... | stats NUMBER... | groups PATTERN TEXT [OFFSET]\n";

/* The argument at index as a view; the caller checked the count. */
str word(const array<std.string::string>* arguments, usize index) {
    return (*arguments)[index].as_str();
}

/* A byte index argument. */
usize position(const array<std.string::string>* arguments, usize index) throws Usage {
    try {
        return std.convert::parse_usize(word(arguments, index), 10u32);
    } catch (std.convert::parse_error failure) {
        failure as void;
        throw Usage {.code = 1u32};
    }
}

/* A number argument in any radix that parse_u64 recognizes. */
u64 unsigned_at(const array<std.string::string>* arguments, usize index) throws Usage {
    try {
        return std.convert::parse_u64(word(arguments, index), 0u32);
    } catch (std.convert::parse_error failure) {
        failure as void;
        throw Usage {.code = 1u32};
    }
}

/* A decimal number argument. */
i64 signed_at(const array<std.string::string>* arguments, usize index) throws Usage {
    try {
        return std.convert::parse_i64(word(arguments, index), 10u32);
    } catch (std.convert::parse_error failure) {
        failure as void;
        throw Usage {.code = 1u32};
    }
}

/* The output of one command; throws Usage for a wrong argument count or number. */
std.string::string run(Command command, const array<std.string::string>* arguments)
    throws Usage, std.convert::parse_error, std.string::boundary_error, std.bytes::bytes_error,
        std.regex::error, std.alloc::alloc_error {
    usize given = len(*arguments);
    switch (command) {
    case Command::fields:
        throw (given != 4usize) Usage {.code = 2u32};
        return example.textkit.words::split_fields(word(arguments, 2usize), word(arguments, 3usize));
    case Command::lines:
        throw (given != 3usize) Usage {.code = 2u32};
        return example.textkit.words::numbered(word(arguments, 2usize));
    case Command::scalars:
        throw (given != 3usize) Usage {.code = 2u32};
        return example.textkit.words::code_points(word(arguments, 2usize));
    case Command::fold:
        throw (given != 3usize) Usage {.code = 2u32};
        return example.textkit.words::folded(word(arguments, 2usize));
    case Command::join:
        throw (given < 3usize) Usage {.code = 2u32};
        array<str> parts = std.array::create::<str>();
        for (usize index = 3usize; index < given; index += 1usize) {
            try {
                std.array::push(&parts, word(arguments, index));
            } catch (std.array::push_error<str> failure) {
                failure as void;
                throw std.alloc::alloc_error::out_of_memory;
            }
        }
        return example.textkit.words::joined(parts.as_slice(), word(arguments, 2usize));
    case Command::replace:
        throw (given != 5usize) Usage {.code = 2u32};
        return example.textkit.words::replaced(
            word(arguments, 2usize), word(arguments, 3usize), word(arguments, 4usize));
    case Command::encode:
        throw (given != 3usize) Usage {.code = 2u32};
        return example.textkit.codes::encoded(word(arguments, 2usize));
    case Command::decode:
        throw (given != 4usize) Usage {.code = 2u32};
        return example.textkit.codes::decoded(word(arguments, 2usize), word(arguments, 3usize));
    case Command::edit:
        throw (given != 6usize) Usage {.code = 2u32};
        return example.textkit.codes::edited(word(arguments, 2usize), position(arguments, 3usize),
                                             position(arguments, 4usize), word(arguments, 5usize));
    case Command::insert:
        throw (given != 5usize) Usage {.code = 2u32};
        return example.textkit.codes::inserted(word(arguments, 2usize),
                                               position(arguments, 3usize),
                                               word(arguments, 4usize));
    case Command::pack:
        array<u64> values = std.array::create::<u64>();
        for (usize index = 2usize; index < given; index += 1usize) {
            u64 value = unsigned_at(arguments, index);
            try {
                std.array::push(&values, value);
            } catch (std.array::push_error<u64> failure) {
                failure as void;
                throw std.alloc::alloc_error::out_of_memory;
            }
        }
        return example.textkit.codes::packed(values.as_slice());
    case Command::stats:
        array<i64> numbers = std.array::create::<i64>();
        for (usize index = 2usize; index < given; index += 1usize) {
            i64 value = signed_at(arguments, index);
            try {
                std.array::push(&numbers, value);
            } catch (std.array::push_error<i64> failure) {
                failure as void;
                throw std.alloc::alloc_error::out_of_memory;
            }
        }
        return example.textkit.numbers::statistics(numbers.as_slice());
    case Command::groups:
        throw (given != 4usize && given != 5usize) Usage {.code = 2u32};
        usize offset = 0usize;
        if (given == 5usize) { offset = position(arguments, 4usize); }
        return example.textkit.patterns::captured(word(arguments, 2usize),
                                                  word(arguments, 3usize), offset);
    }
    throw Usage {.code = 3u32};
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
            await std.console::eprint(f"textkit: {code} at byte {index}\n");
            return 65;
        } catch (std.string::boundary_error failure) {
            str code = core::enum_name(failure);
            await std.console::eprint(f"textkit: {code}\n");
            return 65;
        } catch (std.bytes::bytes_error failure) {
            failure as void;
            await std.console::eprint(std.string::from_str("textkit: the record holds 32 bytes\n"));
            return 65;
        } catch (std.regex::error failure) {
            usize offset = failure.offset;
            str code = core::enum_name(failure.code);
            await std.console::eprint(f"textkit: {code} at byte {offset}\n");
            return 65;
        }
    }
    return 70;
}
