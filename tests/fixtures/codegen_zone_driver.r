module test.codegen.zone_driver;
import std.console;
import std.time;

/* Runs one time-zone operation of std.time per input line and prints its result or
   `error CODE`:
     local ZONE SECONDS                 Y-M-D h:m:s offset dst abbreviation weekday year_day
     resolve ZONE Y M D h m s CHOICE    the Unix seconds of a local time (earlier/later/reject)
     start ZONE SECONDS                 the Unix seconds of the start of the local day
     days ZONE SECONDS N                the Unix seconds N local days later
     months ZONE SECONDS N              the Unix seconds N months later
   ZONE is an IANA name, or `posix:RULE` for a POSIX TZ string. */

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

protected i64 number(str text) throws std.convert::parse_error {
    return std.convert::parse_i64(text, 10u32);
}

protected std.time::system_time at(str text) throws std.convert::parse_error {
    return std.time::system_time {.unix_seconds = number(text), .nanoseconds = 0u32};
}

protected async std.time::zone zone_named(std.string::string name) throws std.time::zone_error, std.error::fault {
    const u8[] text = name.as_bytes();
    if (len(text) > 6usize && std.bytes::starts_with(text, "posix:") == true) {
        return std.time::posix_zone(core::validate_utf8(text[6usize..len(text)]));
    }
    return await std.time::load_zone(move name);
}

protected std.string::string seconds(std.time::system_time value) throws std.alloc::alloc_error {
    return f"{value.unix_seconds}";
}

protected std.time::ambiguity choice_named(str name) {
    switch (name) {
    case "later": return std.time::ambiguity::later;
    case "reject": return std.time::ambiguity::reject;
    default: return std.time::ambiguity::earlier;
    }
}

protected async std.string::string run(array<std.string::string> arguments)
    throws std.time::zone_error, std.error::fault {
    std.time::zone where = await zone_named(core::replace(&arguments[1usize], std.string::create()));
    switch (arguments[0usize].as_str()) {
    case "local":
        std.time::system_time instant = at(arguments[2usize].as_str());
        std.time::local_time local = where.to_local(instant);
        str name = where.abbreviation(instant);
        i32 offset = where.offset(instant);
        u8 month = local.month;
        u8 day = local.day;
        u8 hour = local.hour;
        u8 minute = local.minute;
        u8 second = local.second;
        return f"{local.year}-{month}-{day} {hour}:{minute}:{second} {offset} {local.dst} {name} {local.weekday} {local.year_day}";
    case "resolve":
        std.time::local_time wall = {.year = number(arguments[2usize].as_str()) as i32,
                                     .month = number(arguments[3usize].as_str()) as u8,
                                     .day = number(arguments[4usize].as_str()) as u8,
                                     .hour = number(arguments[5usize].as_str()) as u8,
                                     .minute = number(arguments[6usize].as_str()) as u8,
                                     .second = number(arguments[7usize].as_str()) as u8, .nanosecond = 0u32,
                                     .weekday = 0u8, .year_day = 0u16, .offset = 0i32, .dst = false};
        return seconds(where.from_local(wall, choice_named(arguments[8usize].as_str())));
    case "start": return seconds(where.start_of_day(at(arguments[2usize].as_str())));
    case "days": return seconds(where.add_days(at(arguments[2usize].as_str()), number(arguments[3usize].as_str())));
    case "months": return seconds(where.add_months(at(arguments[2usize].as_str()), number(arguments[3usize].as_str())));
    default:
        drop where;
        return std.string::from_str("unknown");
    }
}

async i32 main() {
    while (true) {
        o<std.string::string> line = await std.console::read_line();
        switch (move line) {
        case variant o::some(move text):
            try {
                array<std.string::string> arguments = words(text.as_str());
                await std.console::println(await run(move arguments));
            } catch (std.time::zone_error failure) {
                std.time::zone_error_code code = failure.code;
                await std.console::println(f"error {code}");
            } catch (std.time::time_error failure) {
                str code = core::enum_name(failure.code);
                await std.console::println(f"error time {code}");
            } catch (std.convert::parse_error failure) {
                failure as void;
                await std.console::println(std.string::from_str("error number"));
            } catch (std.array::push_error<std.string::string> failure) { return 71; }
        case variant o::none: return 0;
        }
    }
    return 0;
}
