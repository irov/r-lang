module std.time;

/* R-SLIB-TIME-0006: the R part of std.time, loaded by `import std.time;`: the text forms of RFC
   3339 and of HTTP dates (RFC 9110 section 5.6.7), and in hosted-native-async the periodic
   interval of R-SLIB-TIME-0009. */

protected const str DAY_NAMES = "MonTueWedThuFriSatSun";
protected const str LONG_DAY_NAMES = "Monday,Tuesday,Wednesday,Thursday,Friday,Saturday,Sunday,";
protected const str MONTH_NAMES = "JanFebMarAprMayJunJulAugSepOctNovDec";

protected std.time::time_error failure(std.time::error_code code) {
    return std.time::time_error {.code = code, .native_code = 0i64};
}

/* The decimal digits of value, right-aligned in target[at..at + width]. */
protected void put_number(u8[] target, usize at, u64 value, usize width) {
    u64 rest = value;
    usize index = width;
    while (index > 0usize) {
        index -= 1usize;
        target[at + index] = ((rest % 10u64) + 48u64) as u8;
        rest /= 10u64;
    }
}

/* Monday is 0: 1970-01-01 was a Thursday. */
protected usize weekday_of(i64 unix_seconds) {
    i64 days = unix_seconds / 86400i64;
    if (unix_seconds % 86400i64 < 0i64) { days -= 1i64; }
    i64 shifted = (days + 3i64) % 7i64;
    if (shifted < 0i64) { return (shifted + 7i64) as usize; }
    return shifted as usize;
}

/* The calendar fields of a time that RFC 3339 and HTTP dates can write: years 0000..9999. */
protected std.time::utc_datetime writable(std.time::system_time value) throws std.time::time_error {
    std.time::utc_datetime utc = std.time::to_utc(value);
    throw (utc.year < 0 || utc.year > 9999) failure(std.time::error_code::overflow);
    return utc;
}

/* R-SLIB-TIME-0006: YYYY-MM-DDTHH:MM:SS, a fraction of digits digits (0 to 9, truncated) and Z. */
std.string::string format_rfc3339(std.time::system_time value, u32 digits)
    throws std.time::time_error, std.alloc::alloc_error {
    throw (digits > 9u32) failure(std.time::error_code::invalid_value);
    std.time::utc_datetime utc = writable(value);
    u8[30] text = {};
    put_number(&text, 0usize, utc.year as u64, 4usize);
    text[4] = 45u8;
    put_number(&text, 5usize, utc.month as u64, 2usize);
    text[7] = 45u8;
    put_number(&text, 8usize, utc.day as u64, 2usize);
    text[10] = 84u8;
    put_number(&text, 11usize, utc.hour as u64, 2usize);
    text[13] = 58u8;
    put_number(&text, 14usize, utc.minute as u64, 2usize);
    text[16] = 58u8;
    put_number(&text, 17usize, utc.second as u64, 2usize);
    usize end = 19usize;
    if (digits > 0u32) {
        u64 fraction = utc.nanosecond as u64;
        for (u32 cut = digits; cut < 9u32; cut += 1u32) { fraction /= 10u64; }
        text[19] = 46u8;
        put_number(&text, 20usize, fraction, digits as usize);
        end = 20usize + digits as usize;
    }
    text[end] = 90u8;
    std.string::string result = std.string::with_capacity(end + 1usize);
    try {
        std.string::append_str(&result, core::validate_utf8(text[0usize..end + 1usize]));
    } catch (core::utf8_error error) {
        error as void;
    }
    return move result;
}

/* R-SLIB-TIME-0008: IMF-fixdate, `Sun, 06 Nov 1994 08:49:37 GMT`; the fraction of a second is
   dropped. */
std.string::string format_http_date(std.time::system_time value)
    throws std.time::time_error, std.alloc::alloc_error {
    std.time::utc_datetime utc = writable(value);
    const u8[] days = DAY_NAMES;
    const u8[] months = MONTH_NAMES;
    usize day_name = weekday_of(value.unix_seconds) * 3usize;
    usize month_name = ((utc.month as usize) - 1usize) * 3usize;
    u8[29] text = {};
    text[0] = days[day_name];
    text[1] = days[day_name + 1usize];
    text[2] = days[day_name + 2usize];
    text[8] = months[month_name];
    text[9] = months[month_name + 1usize];
    text[10] = months[month_name + 2usize];
    text[3] = 44u8;
    text[4] = 32u8;
    put_number(&text, 5usize, utc.day as u64, 2usize);
    text[7] = 32u8;
    text[11] = 32u8;
    put_number(&text, 12usize, utc.year as u64, 4usize);
    text[16] = 32u8;
    put_number(&text, 17usize, utc.hour as u64, 2usize);
    text[19] = 58u8;
    put_number(&text, 20usize, utc.minute as u64, 2usize);
    text[22] = 58u8;
    put_number(&text, 23usize, utc.second as u64, 2usize);
    text[25] = 32u8;
    text[26] = 71u8;
    text[27] = 77u8;
    text[28] = 84u8;
    std.string::string result = std.string::with_capacity(29usize);
    try {
        std.string::append_str(&result, core::validate_utf8(text));
    } catch (core::utf8_error error) {
        error as void;
    }
    return move result;
}

/* A parse error at a byte index. */
protected std.convert::parse_error bad(std.convert::parse_error_code code, usize index) {
    return std.convert::parse_error {.code = code, .index = index};
}

protected bool is_digit(u8 symbol) { return symbol >= 48u8 && symbol <= 57u8; }

/* The value of width decimal digits at source[at..]; a missing byte reports invalid_digit at the
   length of the text and a byte that is not a digit invalid_digit at that byte. */
protected u32 read_number(const u8[] source, usize at, usize width) throws std.convert::parse_error {
    u32 value = 0u32;
    for (usize index = 0usize; index < width; index += 1usize) {
        usize position = at + index;
        throw (position >= len(source))
            bad(std.convert::parse_error_code::invalid_digit, len(source));
        u8 symbol = source[position];
        throw (is_digit(symbol) == false) bad(std.convert::parse_error_code::invalid_digit, position);
        value = value * 10u32 + ((symbol - 48u8) as u32);
    }
    return value;
}

/* The byte expected at source[at]. */
protected void expect_byte(const u8[] source, usize at, u8 expected) throws std.convert::parse_error {
    throw (at >= len(source)) bad(std.convert::parse_error_code::invalid_digit, len(source));
    throw (source[at] != expected) bad(std.convert::parse_error_code::invalid_digit, at);
}

/* The index of the three-letter name at source[at..at + 3] among count names: the first byte
   that no name admits reports invalid_digit there, and a text that ends inside the name
   invalid_digit at its length. */
protected usize read_name(const u8[] source, usize at, const u8[] names, usize count)
    throws std.convert::parse_error {
    usize found = count;
    for (usize width = 1usize; width <= 3usize; width += 1usize) {
        usize position = at + width - 1usize;
        throw (position >= len(source))
            bad(std.convert::parse_error_code::invalid_digit, len(source));
        found = count;
        for (usize index = 0usize; index < count; index += 1usize) {
            bool same = true;
            for (usize offset = 0usize; offset < width; offset += 1usize) {
                if (source[at + offset] != names[index * 3usize + offset]) { same = false; }
            }
            if (same == true && found == count) { found = index; }
        }
        throw (found == count) bad(std.convert::parse_error_code::invalid_digit, position);
    }
    return found;
}

protected u32 days_in_month(u32 year, u32 month) {
    if (month == 2u32) {
        if ((year % 4u32 == 0u32 && year % 100u32 != 0u32) || year % 400u32 == 0u32) {
            return 29u32;
        }
        return 28u32;
    }
    if (month == 4u32 || month == 6u32 || month == 9u32 || month == 11u32) { return 30u32; }
    return 31u32;
}

/* The limits of a field: below_minimum or above_maximum at its first digit. */
protected void check_field(u32 value, u32 minimum, u32 maximum, usize at)
    throws std.convert::parse_error {
    throw (value < minimum) bad(std.convert::parse_error_code::below_minimum, at);
    throw (value > maximum) bad(std.convert::parse_error_code::above_maximum, at);
}

/* The positions of the fields of a date and time in a text. */
protected struct field_positions {
    usize month;
    usize day;
    usize hour;
    usize minute;
    usize second;
};

/* The calendar fields as a system time, checked in text order: month, day, hour, minute and
   second (R-SLIB-TIME-0007). */
protected std.time::system_time from_fields(u32 year, u32 month, u32 day, u32 hour, u32 minute,
    u32 second, u32 nanosecond, field_positions at) throws std.convert::parse_error {
    check_field(month, 1u32, 12u32, at.month);
    check_field(day, 1u32, days_in_month(year, month), at.day);
    check_field(hour, 0u32, 23u32, at.hour);
    check_field(minute, 0u32, 59u32, at.minute);
    check_field(second, 0u32, 59u32, at.second);
    std.time::utc_datetime fields = std.time::utc_datetime {
        .year = year as i32, .month = month as u8, .day = day as u8, .hour = hour as u8,
        .minute = minute as u8, .second = second as u8, .nanosecond = nanosecond};
    try {
        return std.time::from_utc(fields);
    } catch (std.time::time_error error) {
        /* Years 0000..9999 with valid fields are always representable. */
        error as void;
        throw bad(std.convert::parse_error_code::above_maximum, 0usize);
    }
}

/* The zone of an RFC 3339 time: `Z` and `z` have sign zero, `+HH:MM` sign 1 and `-HH:MM`
   sign -1; end is the index after it. */
protected struct zone_offset {
    i64 sign;
    u32 hour;
    u32 minute;
    usize end;
};

protected zone_offset read_offset(const u8[] source, usize at) throws std.convert::parse_error {
    throw (at >= len(source)) bad(std.convert::parse_error_code::invalid_digit, len(source));
    u8 zone = source[at];
    if (zone == 90u8 || zone == 122u8) {
        return zone_offset {.sign = 0i64, .hour = 0u32, .minute = 0u32, .end = at + 1usize};
    }
    throw (zone != 43u8 && zone != 45u8) bad(std.convert::parse_error_code::invalid_digit, at);
    u32 hour = read_number(source, at + 1usize, 2usize);
    expect_byte(source, at + 3usize, 58u8);
    u32 minute = read_number(source, at + 4usize, 2usize);
    i64 sign = 1i64;
    if (zone == 45u8) { sign = -1i64; }
    return zone_offset {.sign = sign, .hour = hour, .minute = minute, .end = at + 6usize};
}

/* R-SLIB-TIME-0007: a date-time of RFC 3339 section 5.6. */
std.time::system_time parse_rfc3339(str text) throws std.convert::parse_error {
    const u8[] source = text;
    usize total = len(source);
    throw (total == 0usize) bad(std.convert::parse_error_code::empty, 0usize);
    u32 year = read_number(source, 0usize, 4usize);
    expect_byte(source, 4usize, 45u8);
    u32 month = read_number(source, 5usize, 2usize);
    expect_byte(source, 7usize, 45u8);
    u32 day = read_number(source, 8usize, 2usize);
    throw (total <= 10usize) bad(std.convert::parse_error_code::invalid_digit, total);
    u8 separator = source[10];
    throw (separator != 84u8 && separator != 116u8 && separator != 32u8)
        bad(std.convert::parse_error_code::invalid_digit, 10usize);
    u32 hour = read_number(source, 11usize, 2usize);
    expect_byte(source, 13usize, 58u8);
    u32 minute = read_number(source, 14usize, 2usize);
    expect_byte(source, 16usize, 58u8);
    u32 second = read_number(source, 17usize, 2usize);
    usize at = 19usize;
    u32 nanosecond = 0u32;
    if (at < total && source[at] == 46u8) {
        at += 1usize;
        usize first = at;
        u32 scale = 100000000u32;
        while (at < total && is_digit(source[at]) == true) {
            nanosecond += ((source[at] - 48u8) as u32) * scale;
            scale /= 10u32;
            at += 1usize;
        }
        throw (at == first) bad(std.convert::parse_error_code::invalid_digit, at);
    }
    zone_offset zone = read_offset(source, at);
    throw (zone.end < total) bad(std.convert::parse_error_code::trailing_character, zone.end);
    field_positions positions = field_positions {
        .month = 5usize, .day = 8usize, .hour = 11usize, .minute = 14usize, .second = 17usize};
    std.time::system_time local =
        from_fields(year, month, day, hour, minute, second, nanosecond, positions);
    check_field(zone.hour, 0u32, 23u32, at + 1usize);
    check_field(zone.minute, 0u32, 59u32, at + 4usize);
    i64 offset = zone.sign * ((zone.hour as i64) * 3600i64 + (zone.minute as i64) * 60i64);
    return std.time::system_time {.unix_seconds = local.unix_seconds - offset,
                                  .nanoseconds = local.nanoseconds};
}

/* The fields of an HTTP date, their positions and the index after the date. */
protected struct clock_time {
    u32 hour;
    u32 minute;
    u32 second;
};

protected struct http_fields {
    u32 year;
    u32 month;
    u32 day;
    clock_time clock;
    field_positions at;
    usize end;
};

/* HH:MM:SS at source[at..]. */
protected clock_time read_clock(const u8[] source, usize at) throws std.convert::parse_error {
    u32 hour = read_number(source, at, 2usize);
    expect_byte(source, at + 2usize, 58u8);
    u32 minute = read_number(source, at + 3usize, 2usize);
    expect_byte(source, at + 5usize, 58u8);
    u32 second = read_number(source, at + 6usize, 2usize);
    return clock_time {.hour = hour, .minute = minute, .second = second};
}

/* ` GMT` at source[at..]. */
protected void expect_gmt(const u8[] source, usize at) throws std.convert::parse_error {
    expect_byte(source, at, 32u8);
    expect_byte(source, at + 1usize, 71u8);
    expect_byte(source, at + 2usize, 77u8);
    expect_byte(source, at + 3usize, 84u8);
}

protected field_positions clock_positions(usize month, usize day, usize clock_at) {
    return field_positions {.month = month, .day = day, .hour = clock_at,
                            .minute = clock_at + 3usize, .second = clock_at + 6usize};
}

/* IMF-fixdate after its day name: `, 06 Nov 1994 08:49:37 GMT`. */
protected http_fields read_fixdate(const u8[] source, const u8[] month_names)
    throws std.convert::parse_error {
    expect_byte(source, 4usize, 32u8);
    u32 day = read_number(source, 5usize, 2usize);
    expect_byte(source, 7usize, 32u8);
    u32 month = (read_name(source, 8usize, month_names, 12usize) + 1usize) as u32;
    expect_byte(source, 11usize, 32u8);
    u32 year = read_number(source, 12usize, 4usize);
    expect_byte(source, 16usize, 32u8);
    clock_time clock = read_clock(source, 17usize);
    expect_gmt(source, 25usize);
    return http_fields {.year = year, .month = month, .day = day, .clock = clock,
                        .at = clock_positions(8usize, 5usize, 17usize), .end = 29usize};
}

/* asctime after its day name: ` Nov  6 08:49:37 1994`, a day below 10 after a space. */
protected http_fields read_asctime(const u8[] source, const u8[] month_names)
    throws std.convert::parse_error {
    u32 month = (read_name(source, 4usize, month_names, 12usize) + 1usize) as u32;
    expect_byte(source, 7usize, 32u8);
    throw (len(source) <= 8usize) bad(std.convert::parse_error_code::invalid_digit, len(source));
    usize day_at = 8usize;
    usize width = 2usize;
    if (source[8] == 32u8) {
        day_at = 9usize;
        width = 1usize;
    }
    u32 day = read_number(source, day_at, width);
    expect_byte(source, 10usize, 32u8);
    clock_time clock = read_clock(source, 11usize);
    expect_byte(source, 19usize, 32u8);
    u32 year = read_number(source, 20usize, 4usize);
    return http_fields {.year = year, .month = month, .day = day, .clock = clock,
                        .at = clock_positions(4usize, day_at, 11usize), .end = 24usize};
}

/* The obsolete RFC 850 form: the full day name, `, 06-Nov-94 08:49:37 GMT`; a two-digit year
   below 70 is in the 2000s, otherwise in the 1900s. */
protected http_fields read_rfc850(const u8[] source, usize weekday, const u8[] month_names)
    throws std.convert::parse_error {
    const u8[] names = LONG_DAY_NAMES;
    usize start = 0usize;
    for (usize index = 0usize; index < weekday; index += 1usize) {
        while (names[start] != 44u8) { start += 1usize; }
        start += 1usize;
    }
    usize name_end = 3usize;
    while (names[start + name_end] != 44u8) {
        throw (name_end >= len(source))
            bad(std.convert::parse_error_code::invalid_digit, len(source));
        throw (source[name_end] != names[start + name_end])
            bad(std.convert::parse_error_code::invalid_digit, name_end);
        name_end += 1usize;
    }
    expect_byte(source, name_end, 44u8);
    expect_byte(source, name_end + 1usize, 32u8);
    usize day_at = name_end + 2usize;
    u32 day = read_number(source, day_at, 2usize);
    expect_byte(source, name_end + 4usize, 45u8);
    usize month_at = name_end + 5usize;
    u32 month = (read_name(source, month_at, month_names, 12usize) + 1usize) as u32;
    expect_byte(source, name_end + 8usize, 45u8);
    u32 short_year = read_number(source, name_end + 9usize, 2usize);
    u32 year = 1900u32 + short_year;
    if (short_year < 70u32) { year = 2000u32 + short_year; }
    expect_byte(source, name_end + 11usize, 32u8);
    usize time_at = name_end + 12usize;
    clock_time clock = read_clock(source, time_at);
    expect_gmt(source, time_at + 8usize);
    return http_fields {.year = year, .month = month, .day = day, .clock = clock,
                        .at = clock_positions(month_at, day_at, time_at),
                        .end = time_at + 12usize};
}

/* R-SLIB-TIME-0008: the three forms of RFC 9110 section 5.6.7, told apart by the byte after
   the three-letter day name: a comma for IMF-fixdate, a space for asctime and a letter for the
   RFC 850 form. The day name must be the weekday of the date. */
std.time::system_time parse_http_date(str text) throws std.convert::parse_error {
    const u8[] source = text;
    usize total = len(source);
    throw (total == 0usize) bad(std.convert::parse_error_code::empty, 0usize);
    const u8[] day_names = DAY_NAMES;
    const u8[] month_names = MONTH_NAMES;
    usize weekday = read_name(source, 0usize, day_names, 7usize);
    throw (total <= 3usize) bad(std.convert::parse_error_code::invalid_digit, total);
    http_fields fields = {};
    if (source[3] == 44u8) {
        fields = read_fixdate(source, month_names);
    } else {
        if (source[3] == 32u8) {
            fields = read_asctime(source, month_names);
        } else {
            fields = read_rfc850(source, weekday, month_names);
        }
    }
    throw (total > fields.end) bad(std.convert::parse_error_code::trailing_character, fields.end);
    std.time::system_time value = from_fields(fields.year, fields.month, fields.day,
        fields.clock.hour, fields.clock.minute, fields.clock.second, 0u32, fields.at);
    throw (weekday_of(value.unix_seconds) != weekday)
        bad(std.convert::parse_error_code::invalid_digit, 0usize);
    return value;
}

/* R-SLIB-TIME-0009: periodic ticks on the monotonic clock. */
@if (core::profile is hosted-native-async) {
    struct interval {
        protected std.time::instant start;
        protected i64 period;
        protected u64 ticks;
    };

    /* The nanoseconds of a nonnegative duration below 2^63 nanoseconds, or -1. */
    protected i64 whole_nanoseconds(std.time::duration value) {
        i64 seconds = std.time::duration_seconds(value);
        u32 fraction = std.time::duration_nanoseconds(value);
        /* 2^63 nanoseconds are 9223372036 seconds and 854775808 nanoseconds. */
        bool below_limit = fraction < 854775808u32;
        if (seconds < 0i64 || seconds > 9223372036i64) { return -1i64; }
        if (seconds == 9223372036i64 && below_limit == false) { return -1i64; }
        return seconds * 1000000000i64 + fraction as i64;
    }

    interval interval::every(std.time::duration period) throws std.time::time_error {
        i64 nanoseconds = whole_nanoseconds(period);
        throw (nanoseconds <= 0i64) failure(std.time::error_code::invalid_value);
        std.time::instant start = std.time::monotonic_now();
        return interval {.start = start, .period = nanoseconds, .ticks = 0u64};
    }

    /* Tick n completes no earlier than start + n periods. A call before the next tick is due
       waits for it; a call after it is due completes at once the latest due tick and skips the
       ticks between, so ticks keep to the grid of the start and a delay gives one late tick,
       not a burst. */
    @scoped
    async u64 interval::tick(interval* this)
        throws std.time::time_error, std.async::start_error {
        std.time::instant now = std.time::monotonic_now();
        i64 elapsed = whole_nanoseconds(std.time::instant_duration(now, this->start));
        throw (elapsed < 0i64) failure(std.time::error_code::overflow);
        u64 due = (elapsed / this->period) as u64;
        u64 next = this->ticks + 1u64;
        if (due >= next) {
            this->ticks = due;
            return due;
        }
        throw (next > (9223372036854775807i64 / this->period) as u64)
            failure(std.time::error_code::overflow);
        i64 offset = (next as i64) * this->period;
        try {
            std.time::duration delay = std.time::duration_from_parts(
                offset / 1000000000i64, (offset % 1000000000i64) as u32);
            await std.time::sleep_until(std.time::instant_add(this->start, delay));
        } catch (std.time::duration_error error) {
            error as void;
            throw failure(std.time::error_code::overflow);
        }
        this->ticks = next;
        return next;
    }
}

/* ---- Time zones (R-SLIB-TIME-0010..0013) ---- */

/* R-SLIB-TIME-0010: why a zone was not read or a local time not resolved. */
@derive(format)
enum zone_error_code { invalid_name, unknown_zone, malformed, nonexistent_time, ambiguous_time };

error zone_error { zone_error_code code; };

protected zone_error zone_failure(zone_error_code code) { return zone_error {.code = code}; }

/* A local time type: its offset east of UTC in seconds, whether it is daylight saving time, and
   the bounds of its abbreviation in the abbreviations of the zone. */
protected struct zone_type { i32 offset; bool dst; usize name_start; usize name_end; };

/* The date of a rule of a POSIX TZ string: kind 0 is Jn (1..365, February 29 never counted),
   1 is n (0..365) and 2 is Mm.w.d; time is the local time of day of the change in seconds. */
protected struct rule_date { u8 kind; i32 day; i32 month; i32 week; i64 time; };

/* The rule of a POSIX TZ string (the footer of TZif, RFC 8536 section 3.3). */
protected struct posix_rule {
    zone_type standard;
    bool has_dst;
    zone_type daylight;
    rule_date start;
    rule_date end;
};

/* R-SLIB-TIME-0010: a time zone: its name, its transitions and local time types, and the rule
   for the times after its last transition. */
struct zone {
    protected std.string::string zone_name;
    protected array<i64> times;
    protected array<u8> indices;
    protected array<zone_type> types;
    protected bytes names;
    protected bool has_rule;
    protected posix_rule rule;
};

/* R-SLIB-TIME-0011: the local time of an instant in a zone. weekday is 1 for Monday to 7 for
   Sunday (ISO 8601) and year_day 1 for January 1. */
struct local_time {
    i32 year;
    u8 month;
    u8 day;
    u8 hour;
    u8 minute;
    u8 second;
    u32 nanosecond;
    u8 weekday;
    u16 year_day;
    i32 offset;
    bool dst;
};

/* R-SLIB-TIME-0012: which instant a local time that occurs twice, or never, resolves to. */
enum ambiguity { earlier, later, reject };

@generic<T>
protected void put(array<T>* target, T value) throws std.alloc::alloc_error {
    try {
        target->push(move value);
    } catch (std.array::push_error<T> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

/* ---- The proleptic Gregorian calendar ---- */

/* The days from 1970-01-01 to a date (H. Hinnant's days_from_civil). */
protected i64 days_from_civil(i64 year, i64 month, i64 day) {
    i64 y = year;
    if (month <= 2i64) { y -= 1i64; }
    i64 era = y / 400i64;
    if (y < 0i64 && y % 400i64 != 0i64) { era -= 1i64; }
    i64 yoe = y - era * 400i64;
    i64 shifted = month + 9i64;
    if (month > 2i64) { shifted = month - 3i64; }
    i64 doy = (153i64 * shifted + 2i64) / 5i64 + day - 1i64;
    i64 doe = yoe * 365i64 + yoe / 4i64 - yoe / 100i64 + doy;
    return era * 146097i64 + doe - 719468i64;
}

protected struct civil { i64 year; i64 month; i64 day; };

/* The date of a count of days from 1970-01-01 (H. Hinnant's civil_from_days). */
protected civil civil_from_days(i64 days) {
    i64 z = days + 719468i64;
    i64 era = z / 146097i64;
    if (z < 0i64 && z % 146097i64 != 0i64) { era -= 1i64; }
    i64 doe = z - era * 146097i64;
    i64 yoe = (doe - doe / 1460i64 + doe / 36524i64 - doe / 146096i64) / 365i64;
    i64 doy = doe - (365i64 * yoe + yoe / 4i64 - yoe / 100i64);
    i64 mp = (5i64 * doy + 2i64) / 153i64;
    i64 day = doy - (153i64 * mp + 2i64) / 5i64 + 1i64;
    i64 month = mp + 3i64;
    if (mp >= 10i64) { month = mp - 9i64; }
    i64 year = yoe + era * 400i64;
    if (month <= 2i64) { year += 1i64; }
    return civil {.year = year, .month = month, .day = day};
}

protected bool leap_year(i64 year) {
    return (year % 4i64 == 0i64 && year % 100i64 != 0i64) || year % 400i64 == 0i64;
}

protected i64 days_in_month(i64 year, i64 month) {
    if (month == 2i64) {
        if (leap_year(year) == true) { return 29i64; }
        return 28i64;
    }
    if (month == 4i64 || month == 6i64 || month == 9i64 || month == 11i64) { return 30i64; }
    return 31i64;
}

/* The whole days and the second of the day of a count of seconds, the second in 0..86399. */
protected i64 floor_days(i64 seconds) {
    i64 days = seconds / 86400i64;
    if (seconds % 86400i64 < 0i64) { days -= 1i64; }
    return days;
}

/* ---- Rules of POSIX TZ strings ---- */

/* The local second, counted from 1970-01-01 00:00 local, at which a rule date falls in a year. */
protected i64 rule_local_seconds(const rule_date* date, i64 year) {
    i64 days = 0i64;
    if (date->kind == 2u8) {
        i64 month = date->month as i64;
        i64 first = days_from_civil(year, month, 1i64);
        /* 1970-01-01 was a Thursday, weekday 4 counted from Sunday. */
        i64 first_weekday = (first + 4i64) % 7i64;
        if (first_weekday < 0i64) { first_weekday += 7i64; }
        i64 day = 1i64 + (date->day as i64 - first_weekday + 7i64) % 7i64 + 7i64 * (date->week as i64 - 1i64);
        i64 last = days_in_month(year, month);
        while (day > last) { day -= 7i64; }
        days = first + day - 1i64;
    } else {
        i64 january = days_from_civil(year, 1i64, 1i64);
        i64 day = date->day as i64;
        if (date->kind == 0u8) {
            if (leap_year(year) == true && day >= 60i64) { day += 1i64; }
            days = january + day - 1i64;
        } else {
            days = january + day;
        }
    }
    return days * 86400i64 + date->time;
}

/* The local time type of a rule at an instant. */
protected zone_type rule_type(const posix_rule* rule, i64 instant) {
    if (rule->has_dst == false) { return rule->standard; }
    i64 local = instant + rule->standard.offset as i64;
    i64 year = civil_from_days(floor_days(local)).year;
    i64 start = rule_local_seconds(&rule->start, year) - rule->standard.offset as i64;
    i64 end = rule_local_seconds(&rule->end, year) - rule->daylight.offset as i64;
    if (start < end) {
        if (instant >= start && instant < end) { return rule->daylight; }
        return rule->standard;
    }
    if (instant >= end && instant < start) { return rule->standard; }
    return rule->daylight;
}

/* A reader of the text of a POSIX TZ string. */
protected struct rule_text { usize at; };

protected bool is_letter(u8 c) { return (c >= 65u8 && c <= 90u8) || (c >= 97u8 && c <= 122u8); }

/* Reads a zone abbreviation, alphabetic or quoted in angle brackets, into the names. */
protected zone_type read_abbreviation(const u8[] text, rule_text* cursor, bytes* names)
    throws zone_error, std.alloc::alloc_error {
    usize start = cursor->at;
    usize stop = start;
    if (start < len(text) && text[start] == 60u8) {
        usize at = start + 1usize;
        while (at < len(text) && text[at] != 62u8) { at += 1usize; }
        throw (at >= len(text) || at - start - 1usize < 3usize) zone_failure(zone_error_code::malformed);
        start += 1usize;
        stop = at;
        cursor->at = at + 1usize;
    } else {
        usize at = start;
        while (at < len(text) && is_letter(text[at]) == true) { at += 1usize; }
        throw (at - start < 3usize) zone_failure(zone_error_code::malformed);
        stop = at;
        cursor->at = at;
    }
    usize name_start = len(*names);
    std.bytes::append(names, text[start..stop]);
    return zone_type {.offset = 0i32, .dst = false, .name_start = name_start, .name_end = len(*names)};
}

/* Reads a number of at most `digits` digits. */
protected i64 read_number(const u8[] text, rule_text* cursor, usize digits) throws zone_error {
    i64 value = 0i64;
    usize count = 0usize;
    while (cursor->at < len(text) && count < digits && is_digit(text[cursor->at]) == true) {
        value = value * 10i64 + (text[cursor->at] as i64 - 48i64);
        cursor->at += 1usize;
        count += 1usize;
    }
    throw (count == 0usize) zone_failure(zone_error_code::malformed);
    return value;
}

/* Reads [+-]hh[:mm[:ss]] in seconds, the hours up to `max_hours`. */
protected i64 read_clock(const u8[] text, rule_text* cursor, i64 max_hours) throws zone_error {
    i64 sign = 1i64;
    if (cursor->at < len(text) && (text[cursor->at] == 43u8 || text[cursor->at] == 45u8)) {
        if (text[cursor->at] == 45u8) { sign = -1i64; }
        cursor->at += 1usize;
    }
    i64 hours = read_number(text, cursor, 3usize);
    throw (hours > max_hours) zone_failure(zone_error_code::malformed);
    i64 total = hours * 3600i64;
    if (cursor->at < len(text) && text[cursor->at] == 58u8) {
        cursor->at += 1usize;
        i64 minutes = read_number(text, cursor, 2usize);
        throw (minutes > 59i64) zone_failure(zone_error_code::malformed);
        total += minutes * 60i64;
        if (cursor->at < len(text) && text[cursor->at] == 58u8) {
            cursor->at += 1usize;
            i64 seconds = read_number(text, cursor, 2usize);
            throw (seconds > 59i64) zone_failure(zone_error_code::malformed);
            total += seconds;
        }
    }
    return sign * total;
}

/* Reads one date of a rule with its optional /time (02:00:00 by default). */
protected rule_date read_rule_date(const u8[] text, rule_text* cursor) throws zone_error {
    throw (cursor->at >= len(text)) zone_failure(zone_error_code::malformed);
    rule_date date = rule_date {.kind = 1u8, .day = 0i32, .month = 0i32, .week = 0i32, .time = 7200i64};
    if (text[cursor->at] == 74u8) {
        cursor->at += 1usize;
        i64 day = read_number(text, cursor, 3usize);
        throw (day < 1i64 || day > 365i64) zone_failure(zone_error_code::malformed);
        date.kind = 0u8;
        date.day = day as i32;
    } else {
        if (text[cursor->at] == 77u8) {
            cursor->at += 1usize;
            i64 month = read_number(text, cursor, 2usize);
            throw (cursor->at >= len(text) || text[cursor->at] != 46u8) zone_failure(zone_error_code::malformed);
            cursor->at += 1usize;
            i64 week = read_number(text, cursor, 1usize);
            throw (cursor->at >= len(text) || text[cursor->at] != 46u8) zone_failure(zone_error_code::malformed);
            cursor->at += 1usize;
            i64 day = read_number(text, cursor, 1usize);
            throw (month < 1i64 || month > 12i64 || week < 1i64 || week > 5i64 || day > 6i64)
                zone_failure(zone_error_code::malformed);
            date.kind = 2u8;
            date.month = month as i32;
            date.week = week as i32;
            date.day = day as i32;
        } else {
            i64 day = read_number(text, cursor, 3usize);
            throw (day > 365i64) zone_failure(zone_error_code::malformed);
            date.day = day as i32;
        }
    }
    if (cursor->at < len(text) && text[cursor->at] == 47u8) {
        cursor->at += 1usize;
        date.time = read_clock(text, cursor, 167i64);
    }
    return date;
}

/* The rule of a POSIX TZ string such as EST5EDT,M3.2.0,M11.1.0; offsets are written west of
   UTC and kept east of it. Daylight time without dates follows the rule of the United States. */
protected posix_rule read_rule(const u8[] text, bytes* names) throws zone_error, std.alloc::alloc_error {
    rule_text cursor = rule_text {.at = 0usize};
    zone_type standard = read_abbreviation(text, &cursor, names);
    standard.offset = (0i64 - read_clock(text, &cursor, 24i64)) as i32;
    rule_date default_start = rule_date {.kind = 2u8, .day = 0i32, .month = 3i32, .week = 2i32, .time = 7200i64};
    rule_date default_end = rule_date {.kind = 2u8, .day = 0i32, .month = 11i32, .week = 1i32, .time = 7200i64};
    if (cursor.at >= len(text)) {
        return posix_rule {.standard = standard, .has_dst = false, .daylight = standard,
                           .start = default_start, .end = default_end};
    }
    zone_type daylight = read_abbreviation(text, &cursor, names);
    daylight.dst = true;
    daylight.offset = standard.offset + 3600i32;
    if (cursor.at < len(text) && text[cursor.at] != 44u8) {
        daylight.offset = (0i64 - read_clock(text, &cursor, 24i64)) as i32;
    }
    rule_date start = default_start;
    rule_date end = default_end;
    if (cursor.at < len(text)) {
        throw (text[cursor.at] != 44u8) zone_failure(zone_error_code::malformed);
        cursor.at += 1usize;
        start = read_rule_date(text, &cursor);
        throw (cursor.at >= len(text) || text[cursor.at] != 44u8) zone_failure(zone_error_code::malformed);
        cursor.at += 1usize;
        end = read_rule_date(text, &cursor);
    }
    throw (cursor.at != len(text)) zone_failure(zone_error_code::malformed);
    return posix_rule {.standard = standard, .has_dst = true, .daylight = daylight, .start = start, .end = end};
}

/* ---- Zones ---- */

protected zone empty_zone(str name) throws std.alloc::alloc_error {
    zone_type utc = zone_type {.offset = 0i32, .dst = false, .name_start = 0usize, .name_end = 0usize};
    rule_date none = rule_date {.kind = 1u8, .day = 0i32, .month = 0i32, .week = 0i32, .time = 0i64};
    return zone {.zone_name = std.string::from_str(name), .times = std.array::create::<i64>(),
                 .indices = std.array::create::<u8>(), .types = std.array::create::<zone_type>(),
                 .names = std.bytes::with_capacity(16usize), .has_rule = false,
                 .rule = posix_rule {.standard = utc, .has_dst = false, .daylight = utc, .start = none, .end = none}};
}

/* R-SLIB-TIME-0010: UTC, with the abbreviation UTC. */
zone utc_zone() throws std.alloc::alloc_error {
    zone made = empty_zone("UTC");
    std.bytes::append(&made.names, "UTC");
    put(&made.types, zone_type {.offset = 0i32, .dst = false, .name_start = 0usize, .name_end = 3usize});
    return move made;
}

/* R-SLIB-TIME-0010: a zone with one offset east of UTC, of at most 25 hours, named `name`. */
zone fixed_zone(i32 offset, str name) throws zone_error, std.alloc::alloc_error {
    throw (offset > 90000i32 || offset < -90000i32) zone_failure(zone_error_code::malformed);
    zone made = empty_zone(name);
    std.bytes::append(&made.names, name);
    put(&made.types, zone_type {.offset = offset, .dst = false, .name_start = 0usize, .name_end = len(made.names)});
    return move made;
}

/* R-SLIB-TIME-0010: the zone of a POSIX TZ string, named by the string itself. */
zone posix_zone(str rule) throws zone_error, std.alloc::alloc_error {
    zone made = empty_zone(rule);
    const u8[] text = rule;
    posix_rule read = read_rule(text, &made.names);
    made.rule = read;
    made.has_rule = true;
    return move made;
}

/* The big-endian number of `width` bytes at `at`. */
protected i64 big_endian(const u8[] data, usize at, usize width) throws zone_error {
    throw (at + width > len(data)) zone_failure(zone_error_code::malformed);
    u64 value = 0u64;
    for (usize index = 0usize; index < width; index += 1usize) {
        value = (value << 8u64) | (data[at + index] as u64);
    }
    /* Two's complement: a set top bit is a negative number. */
    if (width == 4usize) {
        if (value >= 2147483648u64) { return (value as i64) - 4294967296i64; }
        return value as i64;
    }
    if (value > 9223372036854775807u64) {
        u64 magnitude = (~value) + 1u64;
        if (magnitude == 9223372036854775808u64) { return -9223372036854775807i64 - 1i64; }
        return 0i64 - (magnitude as i64);
    }
    return value as i64;
}

/* The counts of a TZif header (RFC 8536 section 3.1). */
protected struct tzif_counts { i64 isut; i64 isstd; i64 leap; i64 time; i64 type; i64 chars; };

protected tzif_counts counts_at(const u8[] data, usize at) throws zone_error {
    throw (at + 44usize > len(data) || data[at] != 84u8 || data[at + 1usize] != 90u8 || data[at + 2usize] != 105u8 ||
           data[at + 3usize] != 102u8) zone_failure(zone_error_code::malformed);
    tzif_counts counts = tzif_counts {.isut = big_endian(data, at + 20usize, 4usize), .isstd = big_endian(data, at + 24usize, 4usize),
                                      .leap = big_endian(data, at + 28usize, 4usize), .time = big_endian(data, at + 32usize, 4usize),
                                      .type = big_endian(data, at + 36usize, 4usize), .chars = big_endian(data, at + 40usize, 4usize)};
    throw (counts.isut < 0i64 || counts.isstd < 0i64 || counts.leap < 0i64 || counts.time < 0i64 || counts.type < 1i64 ||
           counts.type > 255i64 || counts.chars < 1i64) zone_failure(zone_error_code::malformed);
    return counts;
}

/* The size of the data block of a header with times of `width` bytes. */
protected usize block_size(const tzif_counts* counts, usize width) {
    return (counts->time as usize) * (width + 1usize) + (counts->type as usize) * 6usize + (counts->chars as usize) +
           (counts->leap as usize) * (width + 4usize) + (counts->isstd as usize) + (counts->isut as usize);
}

/* R-SLIB-TIME-0010: the zone of the bytes of a TZif file (RFC 8536, versions 1 to 4), named
   `name`. Version 2 and later files are read in their 64-bit form with the rule of their
   footer; leap-second records are ignored. */
zone parse_tzif(str name, const u8[] data) throws zone_error, std.alloc::alloc_error {
    tzif_counts first = counts_at(data, 0usize);
    u8 version = data[4usize];
    usize width = 4usize;
    usize at = 44usize;
    tzif_counts counts = first;
    if (version >= 50u8) {
        at = 44usize + block_size(&first, 4usize);
        counts = counts_at(data, at);
        at += 44usize;
        width = 8usize;
    }
    throw (at + block_size(&counts, width) > len(data)) zone_failure(zone_error_code::malformed);
    zone made = empty_zone(name);
    usize time_count = counts.time as usize;
    usize type_count = counts.type as usize;
    usize chars_at = at + time_count * (width + 1usize) + type_count * 6usize;
    std.bytes::append(&made.names, data[chars_at..(chars_at + (counts.chars as usize))]);
    for (usize index = 0usize; index < time_count; index += 1usize) {
        put(&made.times, big_endian(data, at + index * width, width));
        u8 kind = data[at + time_count * width + index];
        throw ((kind as usize) >= type_count) zone_failure(zone_error_code::malformed);
        put(&made.indices, kind);
    }
    usize types_at = at + time_count * (width + 1usize);
    for (usize index = 0usize; index < type_count; index += 1usize) {
        usize entry = types_at + index * 6usize;
        i64 offset = big_endian(data, entry, 4usize);
        u8 abbreviation = data[entry + 5usize];
        throw (offset < -89999i64 || offset > 93599i64 || (abbreviation as i64) >= counts.chars ||
               data[entry + 4usize] > 1u8) zone_failure(zone_error_code::malformed);
        usize name_start = abbreviation as usize;
        usize name_end = name_start;
        while (name_end < len(made.names) && made.names[name_end] != 0u8) { name_end += 1usize; }
        put(&made.types, zone_type {.offset = offset as i32, .dst = data[entry + 4usize] == 1u8,
                                    .name_start = name_start, .name_end = name_end});
    }
    if (version >= 50u8) {
        usize footer = at + block_size(&counts, width);
        throw (footer >= len(data) || data[footer] != 10u8) zone_failure(zone_error_code::malformed);
        usize stop = footer + 1usize;
        while (stop < len(data) && data[stop] != 10u8) { stop += 1usize; }
        throw (stop >= len(data)) zone_failure(zone_error_code::malformed);
        if (stop > footer + 1usize) {
            posix_rule read = read_rule(data[(footer + 1usize)..stop], &made.names);
            made.rule = read;
            made.has_rule = true;
        }
    }
    return move made;
}

/* The local time type of the zone at an instant. */
protected zone_type type_at(const zone* this, i64 instant) {
    usize count = len(this->times);
    if (count == 0usize || instant >= this->times[count - 1usize]) {
        if (this->has_rule == true) { return rule_type(&this->rule, instant); }
        if (count == 0usize) {
            if (len(this->types) == 0usize) { return this->rule.standard; }
            return this->types[0usize];
        }
        return this->types[this->indices[count - 1usize] as usize];
    }
    if (instant < this->times[0usize]) { return this->types[0usize]; }
    usize low = 0usize;
    usize high = count - 1usize;
    while (high - low > 1usize) {
        usize middle = low + (high - low) / 2usize;
        if (this->times[middle] <= instant) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return this->types[this->indices[low] as usize];
}

str zone::name(const zone* this) {
    return this->zone_name;
}

/* R-SLIB-TIME-0011: the offset east of UTC in seconds at an instant. */
i32 zone::offset(const zone* this, std.time::system_time at) {
    return type_at(this, at.unix_seconds).offset;
}

/* R-SLIB-TIME-0011: the abbreviation of the local time at an instant, such as CET or CEST. */
str zone::abbreviation(const zone* this, std.time::system_time at) {
    zone_type kind = type_at(this, at.unix_seconds);
    const u8[] all = this->names.as_slice();
    try {
        return core::validate_utf8(all[kind.name_start..kind.name_end]);
    } catch (core::utf8_error rejected) {
        rejected as void;
    }
    return "";
}

protected local_time local_of(i64 instant, u32 nanosecond, zone_type kind) {
    i64 local = instant + kind.offset as i64;
    i64 days = floor_days(local);
    i64 second_of_day = local - days * 86400i64;
    civil date = civil_from_days(days);
    i64 weekday = (days + 3i64) % 7i64;
    if (weekday < 0i64) { weekday += 7i64; }
    i64 year_day = days - days_from_civil(date.year, 1i64, 1i64) + 1i64;
    return local_time {.year = date.year as i32, .month = date.month as u8, .day = date.day as u8,
                       .hour = (second_of_day / 3600i64) as u8, .minute = ((second_of_day / 60i64) % 60i64) as u8,
                       .second = (second_of_day % 60i64) as u8, .nanosecond = nanosecond, .weekday = (weekday + 1i64) as u8,
                       .year_day = year_day as u16, .offset = kind.offset, .dst = kind.dst};
}

/* R-SLIB-TIME-0011: the local time of an instant; a nanoseconds field of 10^9 or more reports
   invalid_value, a year outside i32 overflow. */
local_time zone::to_local(const zone* this, std.time::system_time at) throws std.time::time_error {
    throw (at.nanoseconds >= 1000000000u32) failure(std.time::error_code::invalid_value);
    throw (at.unix_seconds > 67767976233532799i64 || at.unix_seconds < -67768040609740800i64)
        failure(std.time::error_code::overflow);
    return local_of(at.unix_seconds, at.nanoseconds, type_at(this, at.unix_seconds));
}

/* The local second of a wall time counted from 1970-01-01 00:00 local. */
protected i64 wall_seconds(const local_time* wall) throws std.time::time_error {
    throw (wall->month < 1u8 || wall->month > 12u8 || wall->day < 1u8 || wall->hour > 23u8 || wall->minute > 59u8 ||
           wall->second > 59u8 || wall->nanosecond >= 1000000000u32) failure(std.time::error_code::invalid_value);
    throw ((wall->day as i64) > days_in_month(wall->year as i64, wall->month as i64))
        failure(std.time::error_code::invalid_value);
    i64 days = days_from_civil(wall->year as i64, wall->month as i64, wall->day as i64);
    return days * 86400i64 + (wall->hour as i64) * 3600i64 + (wall->minute as i64) * 60i64 + wall->second as i64;
}

/* The instant of a local second: one instant, two of an overlap or none of a gap. */
protected i64 resolve(const zone* this, i64 local, ambiguity choice) throws zone_error {
    i32 before = type_at(this, local - 172800i64).offset;
    i32 after = type_at(this, local + 172800i64).offset;
    i64 first = local - before as i64;
    i64 second = local - after as i64;
    bool first_valid = type_at(this, first).offset == before;
    bool second_valid = type_at(this, second).offset == after;
    if (before == after || (first_valid == true && second_valid == false)) { return first; }
    if (first_valid == false && second_valid == true) { return second; }
    if (first_valid == true) {
        throw (choice == ambiguity::reject) zone_failure(zone_error_code::ambiguous_time);
        i64 low = first;
        i64 high = second;
        if (second < first) {
            low = second;
            high = first;
        }
        if (choice == ambiguity::earlier) { return low; }
        return high;
    }
    throw (choice == ambiguity::reject) zone_failure(zone_error_code::nonexistent_time);
    if (choice == ambiguity::earlier) { return first; }
    return second;
}

/* R-SLIB-TIME-0012: the instant of a local time of the zone; its offset, dst, weekday and
   year_day are ignored. A time that occurs twice resolves to its earlier or later instant; a
   time in a gap is read with the offset before the gap (earlier) or after it (later); reject
   reports both cases. */
std.time::system_time zone::from_local(const zone* this, local_time wall, ambiguity choice)
    throws zone_error, std.time::time_error {
    i64 local = wall_seconds(&wall);
    return std.time::system_time {.unix_seconds = resolve(this, local, choice), .nanoseconds = wall.nanosecond};
}

/* R-SLIB-TIME-0013: the first instant of the local day of an instant. */
std.time::system_time zone::start_of_day(const zone* this, std.time::system_time at)
    throws zone_error, std.time::time_error {
    local_time today = this->to_local(at);
    today.hour = 0u8;
    today.minute = 0u8;
    today.second = 0u8;
    today.nanosecond = 0u32;
    return this->from_local(today, ambiguity::earlier);
}

/* R-SLIB-TIME-0013: the same local time `days` days later, or earlier for a negative count. */
std.time::system_time zone::add_days(const zone* this, std.time::system_time at, i64 days)
    throws zone_error, std.time::time_error {
    local_time wall = this->to_local(at);
    i64 date = days_from_civil(wall.year as i64, wall.month as i64, wall.day as i64);
    throw ((days > 0i64 && date > 9223372036854775807i64 / 86400i64 - days) ||
           (days < 0i64 && date < -9223372036854775807i64 / 86400i64 - days))
        failure(std.time::error_code::overflow);
    civil moved = civil_from_days(date + days);
    throw (moved.year > 2147483647i64 || moved.year < -2147483647i64) failure(std.time::error_code::overflow);
    wall.year = moved.year as i32;
    wall.month = moved.month as u8;
    wall.day = moved.day as u8;
    return this->from_local(wall, ambiguity::earlier);
}

/* R-SLIB-TIME-0013: the same local time `months` months later; a day past the end of the
   target month becomes its last day. */
std.time::system_time zone::add_months(const zone* this, std.time::system_time at, i64 months)
    throws zone_error, std.time::time_error {
    local_time wall = this->to_local(at);
    throw (months > 24000000000i64 || months < -24000000000i64) failure(std.time::error_code::overflow);
    i64 index = (wall.year as i64) * 12i64 + (wall.month as i64 - 1i64) + months;
    i64 year = index / 12i64;
    if (index % 12i64 < 0i64) { year -= 1i64; }
    i64 month = index - year * 12i64 + 1i64;
    throw (year > 2147483647i64 || year < -2147483647i64) failure(std.time::error_code::overflow);
    i64 last = days_in_month(year, month);
    wall.year = year as i32;
    wall.month = month as u8;
    if ((wall.day as i64) > last) { wall.day = last as u8; }
    return this->from_local(wall, ambiguity::earlier);
}

/* ---- Dates and times by pattern (R-SLIB-TIME-0014) ---- */

/* One element of a pattern: a field of a fixed number of digits, or a literal byte. */
protected enum pattern_part { year, month, day, hour, minute, second, fraction, literal };

protected struct pattern_item {
    pattern_part part;
    usize width;
};

protected bool ascii_letter(u8 symbol) {
    return (symbol >= 65u8 && symbol <= 90u8) || (symbol >= 97u8 && symbol <= 122u8);
}

/* The field of a run of one letter: yyyy, MM, dd, HH, mm, ss, or S repeated 1 to 9 times. */
protected pattern_part field_of(u8 letter, usize run) throws std.time::time_error {
    if (letter == 121u8 && run == 4usize) { return pattern_part::year; }
    if (letter == 77u8 && run == 2usize) { return pattern_part::month; }
    if (letter == 100u8 && run == 2usize) { return pattern_part::day; }
    if (letter == 72u8 && run == 2usize) { return pattern_part::hour; }
    if (letter == 109u8 && run == 2usize) { return pattern_part::minute; }
    if (letter == 115u8 && run == 2usize) { return pattern_part::second; }
    if (letter == 83u8 && run <= 9usize) { return pattern_part::fraction; }
    throw failure(std.time::error_code::invalid_value);
}

/* The element of pattern that starts at at: a whole run of one ASCII letter is a field, any
   other byte stands for itself. */
protected pattern_item item_at(const u8[] pattern, usize at) throws std.time::time_error {
    u8 first = pattern[at];
    if (ascii_letter(first) == false) { return pattern_item {.part = pattern_part::literal, .width = 1usize}; }
    usize run = 1usize;
    while (at + run < len(pattern) && pattern[at + run] == first) { run += 1usize; }
    return pattern_item {.part = field_of(first, run), .width = run};
}

/* The decimal digits of value in width bytes, appended to out. */
protected void append_number(bytes* out, u64 value, usize width) throws std.alloc::alloc_error {
    u8[9] digits = {};
    put_number(&digits, 0usize, value, width);
    std.bytes::append(out, digits[0usize..width]);
}

/* R-SLIB-TIME-0014: the fields of a local time written by a pattern. */
std.string::string format_local(local_time value, str pattern) throws std.time::time_error, std.alloc::alloc_error {
    wall_seconds(&value) as void;
    const u8[] layout = pattern;
    bytes out = {};
    usize at = 0usize;
    while (at < len(layout)) {
        pattern_item item = item_at(layout, at);
        switch (item.part) {
        case pattern_part::literal: std.bytes::append(&out, layout[at..at + 1usize]);
        case pattern_part::year:
            throw (value.year < 0 || value.year > 9999) failure(std.time::error_code::invalid_value);
            append_number(&out, value.year as u64, 4usize);
        case pattern_part::month: append_number(&out, value.month as u64, 2usize);
        case pattern_part::day: append_number(&out, value.day as u64, 2usize);
        case pattern_part::hour: append_number(&out, value.hour as u64, 2usize);
        case pattern_part::minute: append_number(&out, value.minute as u64, 2usize);
        case pattern_part::second: append_number(&out, value.second as u64, 2usize);
        case pattern_part::fraction:
            u64 fraction = value.nanosecond as u64;
            for (usize cut = item.width; cut < 9usize; cut += 1usize) { fraction /= 10u64; }
            append_number(&out, fraction, item.width);
        }
        at += item.width;
    }
    const u8[] written = out.as_slice();
    std.string::string result = std.string::with_capacity(len(written));
    try {
        std.string::append_str(&result, core::validate_utf8(written));
    } catch (core::utf8_error error) {
        /* The literal bytes come from a str and the fields are ASCII digits. */
        error as void;
    }
    return move result;
}

/* R-SLIB-TIME-0014: a local time read from text by a pattern. The fields the pattern does not
   name are those of 1970-01-01 00:00:00; the offset is 0 and dst false; weekday and year_day are
   those of the date. */
local_time parse_local(str text, str pattern) throws std.convert::parse_error, std.time::time_error {
    const u8[] source = text;
    const u8[] layout = pattern;
    u32[7] value = {1970u32, 1u32, 1u32, 0u32, 0u32, 0u32, 0u32};
    usize[7] where = {0usize, 0usize, 0usize, 0usize, 0usize, 0usize, 0usize};
    usize at = 0usize;
    usize read = 0usize;
    while (at < len(layout)) {
        pattern_item item = item_at(layout, at);
        if (item.part == pattern_part::literal) {
            expect_byte(source, read, layout[at]);
        } else {
            usize slot = core::enum_ordinal(item.part) as usize;
            where[slot] = read;
            u32 number = read_number(source, read, item.width);
            if (item.part == pattern_part::fraction) {
                for (usize scale = item.width; scale < 9usize; scale += 1usize) { number *= 10u32; }
            }
            value[slot] = number;
        }
        read += item.width;
        at += item.width;
    }
    throw (read < len(source)) bad(std.convert::parse_error_code::invalid_digit, read);
    check_field(value[1], 1u32, 12u32, where[1]);
    check_field(value[2], 1u32, days_in_month(value[0], value[1]), where[2]);
    check_field(value[3], 0u32, 23u32, where[3]);
    check_field(value[4], 0u32, 59u32, where[4]);
    check_field(value[5], 0u32, 59u32, where[5]);
    i64 days = days_from_civil(value[0] as i64, value[1] as i64, value[2] as i64);
    i64 weekday = (days + 3i64) % 7i64;
    if (weekday < 0i64) { weekday += 7i64; }
    i64 year_day = days - days_from_civil(value[0] as i64, 1i64, 1i64) + 1i64;
    return local_time {.year = value[0] as i32, .month = value[1] as u8, .day = value[2] as u8,
                       .hour = value[3] as u8, .minute = value[4] as u8, .second = value[5] as u8,
                       .nanosecond = value[6], .weekday = (weekday + 1i64) as u8, .year_day = year_day as u16,
                       .offset = 0, .dst = false};
}

@if (core::profile is hosted-native-async) {
    /* Whether a zone name is a relative path of letters, digits, `_`, `+`, `-` and `/` without
       `.` or `..` components. */
    protected bool valid_zone_name(const u8[] name) {
        if (len(name) == 0usize || name[0usize] == 47u8 || name[len(name) - 1usize] == 47u8) { return false; }
        usize component = 0usize;
        bool only_dots = true;
        for (usize index = 0usize; index <= len(name); index += 1usize) {
            if (index == len(name) || name[index] == 47u8) {
                if (index == component || only_dots == true) { return false; }
                component = index + 1usize;
                only_dots = true;
            } else {
                u8 c = name[index];
                bool allowed = is_letter(c) == true || is_digit(c) == true || c == 95u8 || c == 43u8 || c == 45u8 ||
                               c == 46u8;
                if (allowed == false) { return false; }
                if (c != 46u8) { only_dots = false; }
            }
        }
        return true;
    }

    /* The directory of the zone files: TZDIR when it is set and not empty, else the system's. */
    protected std.string::string zone_directory() throws std.alloc::alloc_error {
        try {
            o<std.string::string> found = std.env::get("TZDIR");
            switch (move found) {
            case variant o::some(move value):
                const u8[] text = value;
                if (len(text) > 0usize) { return move value; }
                drop value;
            case variant o::none: break;
            }
        } catch (std.env::env_error rejected) {
            rejected as void;
        }
        return std.string::from_str("/usr/share/zoneinfo");
    }

    /* R-SLIB-TIME-0010: the zone of an IANA name, such as Asia/Shanghai, from the zone files
       of the system. A name that is not a relative path of zone names is invalid_name, a name
       without a file unknown_zone, a file that is not TZif malformed. */
    async zone load_zone(std.string::string name) throws zone_error, std.error::fault {
        throw (valid_zone_name(name) == false) zone_failure(zone_error_code::invalid_name);
        std.string::string file = zone_directory();
        file.append("/");
        file.append(name);
        bytes data = {};
        try {
            std.fs::path path = std.fs::path_from_utf8(file);
            bytes read = await std.fs::read_file(&path, 1048576usize);
            bytes old = core::replace(&data, move read);
            drop old;
        } catch (std.fs::fs_error refused) {
            throw (refused.code == std.fs::error_code::not_found || refused.code == std.fs::error_code::is_directory)
                zone_failure(zone_error_code::unknown_zone);
            throw refused;
        }
        return parse_tzif(name, data.as_slice());
    }

    /* R-SLIB-TIME-0010: the zone of the process: TZ when it is set, a zone name or a POSIX TZ
       string after an optional colon, UTC when it is empty, else the file /etc/localtime. */
    async zone local_zone() throws zone_error, std.error::fault {
        o<std.string::string> setting = o::none;
        try {
            o<std.string::string> found = std.env::get("TZ");
            o<std.string::string> old = core::replace(&setting, move found);
            drop old;
        } catch (std.env::env_error rejected) {
            rejected as void;
        }
        switch (move setting) {
        case variant o::some(move value):
            const u8[] text = value;
            usize start = 0usize;
            if (len(text) > 0usize && text[0usize] == 58u8) { start = 1usize; }
            if (start == len(text)) { return utc_zone(); }
            str rest = core::validate_utf8(text[start..len(text)]);
            if (valid_zone_name(text[start..len(text)]) == true) {
                try {
                    return await load_zone(std.string::from_str(rest));
                } catch (zone_error rejected) {
                    throw (rejected.code != zone_error_code::unknown_zone) rejected;
                }
            }
            return posix_zone(rest);
        case variant o::none: break;
        }
        std.fs::path path = std.fs::path_from_utf8("/etc/localtime");
        bytes data = await std.fs::read_file(&path, 1048576usize);
        return parse_tzif("localtime", data.as_slice());
    }
}
