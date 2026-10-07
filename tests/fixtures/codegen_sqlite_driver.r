module test.codegen.sqlite_driver;
import std.sqlite;
import std.console;
import std.convert;

/* Runs std.sqlite commands, one per input line, against one in-memory database and prints what
   they return: `script SQL`, `query SQL VALUE...`, `execute SQL VALUE...`, `prepare SQL`,
   `run VALUE...` (the prepared statement), `begin MODE`, `commit`, `rollback` and `state`. SQL
   text is hexadecimal; a value is `n` (NULL), `i` with a decimal integer, `r` with a real, `t`
   with hexadecimal UTF-8 text or `b` with a hexadecimal blob (`-` for an empty one). */
protected u8 nibble(u8 digit) {
    if (digit >= 97u8) { return (digit - 87u8) as u8; }
    if (digit >= 65u8) { return (digit - 55u8) as u8; }
    return (digit - 48u8) as u8;
}

protected bytes parse_hex(const u8[] digits) throws std.alloc::alloc_error {
    bytes data = {};
    if (len(digits) == 1usize) { return move data; }
    for (usize index = 0usize; index + 1usize < len(digits); index += 2usize) {
        std.bytes::append_u8(&data, ((nibble(digits[index]) * 16u8) + nibble(digits[index + 1usize])) as u8);
    }
    return move data;
}

protected u8 hex_digit(u8 number) {
    if (number < 10u8) { return (number + 48u8) as u8; }
    return (number + 87u8) as u8;
}

protected std.string::string hex(const u8[] data) throws std.alloc::alloc_error {
    std.string::string text = std.string::create();
    for (usize index = 0usize; index < len(data); index += 1usize) {
        std.string::push_scalar(&text, hex_digit((data[index] >> 4u8) as u8) as char);
        std.string::push_scalar(&text, hex_digit((data[index] & 15u8) as u8) as char);
    }
    if (len(data) == 0usize) { std.string::push_scalar(&text, '-'); }
    return move text;
}

/* The words of a line. */
protected array<std.string::string> words(str line) throws std.alloc::alloc_error, std.array::push_error<std.string::string> {
    array<std.string::string> found = [];
    const u8[] raw_bytes = line;
    usize start = 0usize;
    for (usize index = 0usize; index <= len(raw_bytes); index += 1usize) {
        if (index == len(raw_bytes) || raw_bytes[index] == 32u8) {
            if (index > start) {
                try {
                    found.push(std.string::from_str(core::validate_utf8(raw_bytes[start..index])));
                } catch (core::utf8_error rejected) { rejected as void; }
            }
            start = index + 1usize;
        }
    }
    return move found;
}

/* The text after the first character of an ASCII word. */
protected str rest_of(str word) {
    const u8[] raw_bytes = word;
    try {
        return core::validate_utf8(raw_bytes[1usize..len(raw_bytes)]);
    } catch (core::utf8_error rejected) {
        rejected as void;
    }
    return "";
}

protected std.sqlite::value parse_value(str word)
    throws std.alloc::alloc_error, std.convert::parse_error, std.string::string_error {
    const u8[] raw_bytes = word;
    str rest = rest_of(word);
    if (raw_bytes[0usize] == 105u8) { return std.sqlite::value::integer(std.convert::parse_i64(rest, 10u32)); }
    if (raw_bytes[0usize] == 114u8) { return std.sqlite::value::real(std.convert::parse_f64(rest)); }
    if (raw_bytes[0usize] == 116u8) {
        bytes data = parse_hex(rest);
        return std.sqlite::value::text(std.string::from_utf8(data.as_slice()));
    }
    if (raw_bytes[0usize] == 98u8) { return std.sqlite::value::blob(parse_hex(rest)); }
    rest as void;
    return std.sqlite::value::null_value;
}

/* The values of the words from first on. */
protected array<std.sqlite::value> parameters(const array<std.string::string>* arguments, usize first)
    throws std.alloc::alloc_error, std.convert::parse_error, std.string::string_error,
           std.array::push_error<std.sqlite::value> {
    array<std.sqlite::value> values = [];
    const std.string::string[] at = std.array::as_slice(arguments);
    for (usize index = first; index < len(at); index += 1usize) {
        values.push(parse_value(at[index]));
    }
    return move values;
}

protected std.string::string sql_of(const array<std.string::string>* arguments) throws std.alloc::alloc_error, std.string::string_error {
    const std.string::string[] at = std.array::as_slice(arguments);
    bytes text = parse_hex(at[1usize]);
    return std.string::from_utf8(text.as_slice());
}

protected std.string::string show(const std.sqlite::value* item) throws std.alloc::alloc_error {
    switch (*item) {
    case variant std.sqlite::value::null_value: return std.string::from_str("n");
    case variant std.sqlite::value::integer(number):
        i64 integer = *number;
        return f"i{integer}";
    case variant std.sqlite::value::real(number):
        f64 real = *number;
        return f"r{real}";
    case variant std.sqlite::value::text(data):
        std.string::string digits = hex(*data);
        return f"t{digits}";
    case variant std.sqlite::value::blob(data):
        std.string::string digits = hex(data->as_slice());
        return f"b{digits}";
    }
}

/* The columns, one line per row and `end`. */
protected std.string::string listing(const std.sqlite::rows* found) throws std.alloc::alloc_error {
    std.string::string text = std.string::from_str("columns");
    for (usize column = 0usize; column < len(found->columns); column += 1usize) {
        std.string::string name = hex(found->columns[column]);
        std.string::append_str(&text, " ");
        std.string::append_str(&text, name);
    }
    for (usize index = 0usize; index < len(found->items); index += 1usize) {
        std.string::append_str(&text, "\nrow");
        const std.sqlite::row* row = &found->items[index];
        for (usize column = 0usize; column < row->count(); column += 1usize) {
            std.string::string shown = show(&row->values[column]);
            std.string::append_str(&text, " ");
            std.string::append_str(&text, shown);
        }
    }
    std.string::append_str(&text, "\nend");
    return move text;
}

protected std.sqlite::begin_mode mode_of(str name) {
    switch (name) {
    case "immediate": return std.sqlite::begin_mode::immediate;
    case "exclusive": return std.sqlite::begin_mode::exclusive;
    default: return std.sqlite::begin_mode::deferred;
    }
}

async i32 main() {
    try {
        std.sqlite::database db = await std.sqlite::open(":memory:", std.sqlite::options {});
        o<std.sqlite::statement> prepared = o::none;
        while (true) {
            o<std.string::string> line = await std.console::read_line();
            switch (move line) {
            case variant o::some(move text):
                array<std.string::string> arguments = words(text);
                const std.string::string[] at = std.array::as_slice(&arguments);
                str command = at[0usize];
                try {
                    switch (command) {
                    case "script":
                        std.string::string sql = sql_of(&arguments);
                        await db.execute_script(sql);
                        await std.console::println(f"ok");
                    case "query":
                        std.string::string sql = sql_of(&arguments);
                        std.sqlite::rows found = await db.query(sql, parameters(&arguments, 2usize));
                        await std.console::println(listing(&found));
                    case "execute":
                        std.string::string sql = sql_of(&arguments);
                        std.sqlite::execution done = await db.execute(sql, parameters(&arguments, 2usize));
                        u64 changes = done.changes;
                        i64 last = done.last_row_id;
                        await std.console::println(f"done {changes} {last}");
                    case "prepare":
                        std.string::string sql = sql_of(&arguments);
                        std.sqlite::statement made = await db.prepare(sql);
                        core::replace(&prepared, o::some(move made)) as void;
                        await std.console::println(f"ok");
                    case "run":
                        array<std.sqlite::value> values = parameters(&arguments, 1usize);
                        switch (prepared) {
                        case variant o::some(statement):
                            std.sqlite::rows found = await statement->query(move values);
                            await std.console::println(listing(&found));
                        case variant o::none:
                            drop values;
                            await std.console::println(f"error no_prepared 0");
                        }
                    case "begin":
                        await db.begin(mode_of(at[1usize]));
                        await std.console::println(f"ok");
                    case "commit":
                        await db.commit();
                        await std.console::println(f"ok");
                    case "rollback":
                        await db.rollback();
                        await std.console::println(f"ok");
                    case "state":
                        bool inside = db.in_transaction();
                        await std.console::println(f"{inside}");
                    default:
                        await std.console::println(f"unknown");
                    }
                } catch (std.sqlite::sqlite_error failure) {
                    std.sqlite::error_code code = failure.code;
                    i32 native = failure.native_code;
                    await std.console::println(f"error {code} {native}");
                }
            case variant o::none:
                drop prepared;
                await (move db).close();
                return 0;
            }
        }
    } catch (std.sqlite::sqlite_error failure) {
        drop failure;
        return 70;
    } catch (std.convert::parse_error failure) {
        failure as void;
        return 72;
    } catch (std.string::string_error failure) {
        failure as void;
        return 73;
    } catch (std.array::push_error<std.string::string> failure) {
        drop failure;
        return 74;
    } catch (std.array::push_error<std.sqlite::value> failure) {
        drop failure;
        return 75;
    } catch (std.error::fault failure) {
        failure as void;
        return 71;
    }
    return 0;
}
