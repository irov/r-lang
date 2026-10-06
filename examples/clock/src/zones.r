module example.clock.zones;
import std.time;

/* Local time in the zones of the IANA database (the zone files of the system), in zones of a
   fixed offset and of POSIX TZ strings: the local date and time of an instant with its offset
   and abbreviation, the start of its day, the next day and the next month, and the instants of
   a local time that a change of daylight saving time skips or repeats. */

std.string::string shown(const std.time::zone* where, std.time::system_time at)
    throws std.time::time_error, std.alloc::alloc_error {
    std.time::local_time local = where->to_local(at);
    str name = where->name();
    str abbreviation = where->abbreviation(at);
    i32 offset = where->offset(at);
    u8 month = local.month;
    u8 day = local.day;
    u8 hour = local.hour;
    u8 minute = local.minute;
    u8 second = local.second;
    u8 weekday = local.weekday;
    u16 year_day = local.year_day;
    return f"{name}: {local.year}-{month}-{day} {hour}:{minute}:{second} {abbreviation} offset={offset} dst={local.dst} weekday={weekday} day={year_day}\n";
}

/* The local time of an instant and three calendar steps from it. */
std.string::string calendar(const std.time::zone* where, std.time::system_time at)
    throws std.time::zone_error, std.time::time_error, std.alloc::alloc_error {
    std.string::string out = shown(where, at);
    std.time::system_time midnight = where->start_of_day(at);
    std.time::system_time tomorrow = where->add_days(midnight, 1i64);
    std.time::system_time next_month = where->add_months(at, 1i64);
    i64 hours = (tomorrow.unix_seconds - midnight.unix_seconds) / 3600i64;
    std.string::string steps = f"day starts {midnight.unix_seconds} and lasts {hours} hours\nnext month {next_month.unix_seconds}\n";
    out.append(steps);
    return move out;
}

/* One instant in UTC, at +05:30 and in the zone of a POSIX TZ string. */
std.string::string three_zones(std.time::system_time at) throws std.time::zone_error, std.time::time_error,
    std.alloc::alloc_error {
    std.time::zone utc = std.time::utc_zone();
    std.time::zone india = std.time::fixed_zone(19800i32, "IST");
    std.time::zone sydney = std.time::posix_zone("AEST-10AEDT,M10.1.0,M4.1.0/3");
    std.string::string out = shown(&utc, at);
    std.string::string second = shown(&india, at);
    std.string::string third = shown(&sydney, at);
    out.append(second);
    out.append(third);
    return move out;
}

protected std.string::string resolved(const std.time::zone* where, std.time::local_time wall, std.time::ambiguity choice)
    throws std.time::time_error, std.alloc::alloc_error {
    try {
        std.time::system_time instant = where->from_local(wall, choice);
        return f"{instant.unix_seconds}";
    } catch (std.time::zone_error failure) {
        std.time::zone_error_code code = failure.code;
        return f"{code}";
    }
}

/* The instants of a local time with each choice of std.time::ambiguity. */
std.string::string wall_instants(const std.time::zone* where, std.time::local_time wall)
    throws std.time::time_error, std.alloc::alloc_error {
    std.string::string earlier = resolved(where, wall, std.time::ambiguity::earlier);
    std.string::string later = resolved(where, wall, std.time::ambiguity::later);
    std.string::string rejected = resolved(where, wall, std.time::ambiguity::reject);
    return f"earlier={earlier} later={later} reject={rejected}\n";
}

async std.string::string named(std.string::string name, std.time::system_time at)
    throws std.time::zone_error, std.error::fault {
    std.time::zone where = await std.time::load_zone(move name);
    return calendar(&where, at);
}

async std.string::string named_wall(std.string::string name, std.time::local_time wall)
    throws std.time::zone_error, std.error::fault {
    std.time::zone where = await std.time::load_zone(move name);
    return wall_instants(&where, wall);
}

/* The zone of a TZif file anywhere, such as a copy that ships with a program. */
async std.string::string from_file(std.string::string path, std.time::system_time at)
    throws std.time::zone_error, std.error::fault {
    std.fs::path file = std.fs::path_from_utf8(path);
    bytes data = await std.fs::read_file(&file, 1048576usize);
    std.time::zone where = std.time::parse_tzif("file", data.as_slice());
    return shown(&where, at);
}

/* The local time of the process: TZ or /etc/localtime. */
async std.string::string process_zone(std.time::system_time at) throws std.time::zone_error, std.error::fault {
    std.time::zone where = await std.time::local_zone();
    return shown(&where, at);
}
