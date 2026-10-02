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
