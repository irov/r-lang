"""Deterministic differential test of the text forms of std.time (M21, Library
R-SLIB-TIME-0006..0008) through executable R code. A reference written from the rules gives the
expected text, value or error code and byte index of every case, including random edits of valid
texts; valid values are also checked against datetime, calendar and email.utils of the Python
standard library."""

from __future__ import annotations

import argparse
import calendar
import datetime
import email.utils
import random
import subprocess

SEED = 20260930
DAYS = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
LONG_DAYS = ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"]
MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]
FIRST = -62167219200  # 0000-01-01T00:00:00Z
LAST = 253402300799  # 9999-12-31T23:59:59Z


def days_from_civil(year: int, month: int, day: int) -> int:
    year -= month <= 2
    era = year // 400
    year_of_era = year - era * 400
    day_of_year = (153 * (month + (-3 if month > 2 else 9)) + 2) // 5 + day - 1
    day_of_era = year_of_era * 365 + year_of_era // 4 - year_of_era // 100 + day_of_year
    return era * 146097 + day_of_era - 719468


def civil(seconds: int) -> tuple[int, int, int, int, int, int]:
    days, rest = divmod(seconds, 86400)
    z = days + 719468
    era = z // 146097
    day_of_era = z - era * 146097
    year_of_era = (day_of_era - day_of_era // 1460 + day_of_era // 36524 - day_of_era // 146096) // 365
    day_of_year = day_of_era - (365 * year_of_era + year_of_era // 4 - year_of_era // 100)
    mp = (5 * day_of_year + 2) // 153
    day = day_of_year - (153 * mp + 2) // 5 + 1
    month = mp + 3 if mp < 10 else mp - 9
    year = year_of_era + era * 400 + (month <= 2)
    return year, month, day, rest // 3600, rest % 3600 // 60, rest % 60


def days_in_month(year: int, month: int) -> int:
    if month == 2:
        return 29 if (year % 4 == 0 and year % 100 != 0) or year % 400 == 0 else 28
    return 30 if month in (4, 6, 9, 11) else 31


def weekday(seconds: int) -> int:
    return (seconds // 86400 + 3) % 7


def format_rfc3339(seconds: int, nanoseconds: int, digits: int) -> str:
    if digits > 9 or nanoseconds >= 1_000_000_000:
        return "error invalid_value"
    if not FIRST <= seconds <= LAST:
        return "error overflow"
    year, month, day, hour, minute, second = civil(seconds)
    text = f"{year:04}-{month:02}-{day:02}T{hour:02}:{minute:02}:{second:02}"
    if digits:
        text += "." + f"{nanoseconds:09}"[:digits]
    return text + "Z"


def format_http(seconds: int) -> str:
    if not FIRST <= seconds <= LAST:
        return "error overflow"
    year, month, day, hour, minute, second = civil(seconds)
    return (f"{DAYS[weekday(seconds)]}, {day:02} {MONTHS[month - 1]} {year:04} "
            f"{hour:02}:{minute:02}:{second:02} GMT")


class Failure(Exception):
    def __init__(self, code: str, index: int) -> None:
        super().__init__(code, index)
        self.code = code
        self.index = index


class Reader:
    """Reads a text from its start as R-SLIB-TIME-0007 describes."""

    def __init__(self, text: str) -> None:
        self.data = text.encode()

    def byte(self, at: int) -> int:
        if at >= len(self.data):
            raise Failure("invalid_digit", len(self.data))
        return self.data[at]

    def expect(self, at: int, value: str) -> None:
        if self.byte(at) != ord(value):
            raise Failure("invalid_digit", at)

    def number(self, at: int, width: int) -> int:
        value = 0
        for position in range(at, at + width):
            symbol = self.byte(position)
            if not 48 <= symbol <= 57:
                raise Failure("invalid_digit", position)
            value = value * 10 + symbol - 48
        return value

    def name(self, at: int, names: list[str]) -> int:
        for width in range(1, 4):
            position = at + width - 1
            self.byte(position)
            prefix = self.data[at:at + width]
            matches = [index for index, name in enumerate(names) if name.encode()[:width] == prefix]
            if not matches:
                raise Failure("invalid_digit", position)
        return matches[0]


def check(value: int, minimum: int, maximum: int, at: int) -> None:
    if value < minimum:
        raise Failure("below_minimum", at)
    if value > maximum:
        raise Failure("above_maximum", at)


def from_fields(year: int, month: int, day: int, hour: int, minute: int, second: int,
                positions: tuple[int, int, int, int, int]) -> int:
    check(month, 1, 12, positions[0])
    check(day, 1, days_in_month(year, month), positions[1])
    check(hour, 0, 23, positions[2])
    check(minute, 0, 59, positions[3])
    check(second, 0, 59, positions[4])
    return days_from_civil(year, month, day) * 86400 + hour * 3600 + minute * 60 + second


def parse_rfc3339(text: str) -> str:
    try:
        reader = Reader(text)
        data = reader.data
        if not data:
            raise Failure("empty", 0)
        year = reader.number(0, 4)
        reader.expect(4, "-")
        month = reader.number(5, 2)
        reader.expect(7, "-")
        day = reader.number(8, 2)
        if reader.byte(10) not in b"Tt ":
            raise Failure("invalid_digit", 10)
        hour = reader.number(11, 2)
        reader.expect(13, ":")
        minute = reader.number(14, 2)
        reader.expect(16, ":")
        second = reader.number(17, 2)
        at = 19
        nanoseconds = 0
        if at < len(data) and data[at] == ord("."):
            at += 1
            first = at
            digits = ""
            while at < len(data) and 48 <= data[at] <= 57:
                digits += chr(data[at])
                at += 1
            if at == first:
                raise Failure("invalid_digit", at)
            nanoseconds = int((digits + "000000000")[:9])
        zone = reader.byte(at)
        sign = 0
        offset_hour = offset_minute = 0
        offset_at = at + 1
        if zone in b"Zz":
            end = at + 1
        else:
            if zone not in b"+-":
                raise Failure("invalid_digit", at)
            offset_hour = reader.number(at + 1, 2)
            reader.expect(at + 3, ":")
            offset_minute = reader.number(at + 4, 2)
            sign = 1 if zone == ord("+") else -1
            end = at + 6
        if end < len(data):
            raise Failure("trailing_character", end)
        local = from_fields(year, month, day, hour, minute, second, (5, 8, 11, 14, 17))
        check(offset_hour, 0, 23, offset_at)
        check(offset_minute, 0, 59, offset_at + 3)
        return f"{local - sign * (offset_hour * 3600 + offset_minute * 60)}:{nanoseconds}"
    except Failure as failure:
        return f"error {failure.code} {failure.index}"


def parse_http(text: str) -> str:
    try:
        reader = Reader(text)
        data = reader.data
        if not data:
            raise Failure("empty", 0)
        day_name = reader.name(0, DAYS)
        if len(data) <= 3:
            raise Failure("invalid_digit", len(data))
        if data[3] == ord(","):
            reader.expect(4, " ")
            day = reader.number(5, 2)
            reader.expect(7, " ")
            month = reader.name(8, MONTHS) + 1
            reader.expect(11, " ")
            year = reader.number(12, 4)
            reader.expect(16, " ")
            clock = 17
            month_at, day_at = 8, 5
            hour = reader.number(clock, 2)
            reader.expect(clock + 2, ":")
            minute = reader.number(clock + 3, 2)
            reader.expect(clock + 5, ":")
            second = reader.number(clock + 6, 2)
            for offset, symbol in enumerate(" GMT"):
                reader.expect(clock + 8 + offset, symbol)
            end = 29
        elif data[3] == ord(" "):
            month = reader.name(4, MONTHS) + 1
            reader.expect(7, " ")
            reader.byte(8)
            if data[8] == ord(" "):
                day_at, width = 9, 1
            else:
                day_at, width = 8, 2
            day = reader.number(day_at, width)
            reader.expect(10, " ")
            clock = 11
            month_at = 4
            hour = reader.number(clock, 2)
            reader.expect(clock + 2, ":")
            minute = reader.number(clock + 3, 2)
            reader.expect(clock + 5, ":")
            second = reader.number(clock + 6, 2)
            reader.expect(19, " ")
            year = reader.number(20, 4)
            end = 24
        else:
            name = LONG_DAYS[day_name].encode()
            at = 3
            while at < len(name):
                if reader.byte(at) != name[at]:
                    raise Failure("invalid_digit", at)
                at += 1
            reader.expect(at, ",")
            reader.expect(at + 1, " ")
            day_at = at + 2
            day = reader.number(day_at, 2)
            reader.expect(at + 4, "-")
            month_at = at + 5
            month = reader.name(month_at, MONTHS) + 1
            reader.expect(at + 8, "-")
            short_year = reader.number(at + 9, 2)
            year = 2000 + short_year if short_year < 70 else 1900 + short_year
            reader.expect(at + 11, " ")
            clock = at + 12
            hour = reader.number(clock, 2)
            reader.expect(clock + 2, ":")
            minute = reader.number(clock + 3, 2)
            reader.expect(clock + 5, ":")
            second = reader.number(clock + 6, 2)
            for offset, symbol in enumerate(" GMT"):
                reader.expect(clock + 8 + offset, symbol)
            end = clock + 12
        if len(data) > end:
            raise Failure("trailing_character", end)
        value = from_fields(year, month, day, hour, minute, second,
                            (month_at, day_at, clock, clock + 3, clock + 6))
        if weekday(value) != day_name:
            raise Failure("invalid_digit", 0)
        return f"{value}:0"
    except Failure as failure:
        return f"error {failure.code} {failure.index}"


def utc(seconds: int) -> datetime.datetime:
    return datetime.datetime(1970, 1, 1, tzinfo=datetime.timezone.utc) + datetime.timedelta(
        seconds=seconds)


def mutate(rng: random.Random, text: str) -> str:
    alphabet = "0123456789-:.TtZz+ ,GMTSunNovdayé"
    position = rng.randrange(0, len(text) + 1)
    kind = rng.randrange(4)
    if kind == 0 and text:
        return text[:position] + text[position + 1:]
    if kind == 1:
        return text[:position] + rng.choice(alphabet) + text[position:]
    if kind == 2 and position < len(text):
        return text[:position] + rng.choice(alphabet) + text[position + 1:]
    return text[:position]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True)
    arguments = parser.parse_args()
    rng = random.Random(SEED)
    cases: list[tuple[str, str, str, str]] = []
    stdlib_checks = 0
    moments = [FIRST, LAST, 0, -1, 951782400, 1709164800, 784111777, FIRST - 1, LAST + 1]
    moments += [rng.randrange(FIRST, LAST + 1) for _ in range(300)]
    moments += [rng.randrange(-2208988800, 4102444800) for _ in range(200)]
    for seconds in moments:
        nanoseconds = rng.randrange(0, 1_000_000_000)
        digits = rng.randrange(0, 11)
        expected = format_rfc3339(seconds, nanoseconds, digits)
        cases.append(("format", f"{seconds}:{nanoseconds}", str(digits), expected))
        cases.append(("http", f"{seconds}:0", "-", format_http(seconds)))
        if -62135596800 <= seconds <= LAST:
            moment = utc(seconds)
            assert format_rfc3339(seconds, 0, 0) == moment.replace(tzinfo=None).isoformat() + "Z"
            assert format_http(seconds) == email.utils.format_datetime(moment, usegmt=True)
            stdlib_checks += 2
    cases.append(("format", "0:1000000000", "0", "error invalid_value"))
    cases.append(("http", "0:1000000000", "-", "error invalid_value"))
    valid: list[str] = []
    for _ in range(400):
        seconds = rng.randrange(FIRST, LAST + 1)
        year, month, day, hour, minute, second = civil(seconds)
        separator = rng.choice("Tt ")
        fraction = "".join(rng.choice("0123456789") for _ in range(rng.choice([0, 0, 1, 3, 6, 9, 12])))
        zone = rng.choice(["Z", "z", "offset", "offset"])
        if zone == "offset":
            zone = f"{rng.choice('+-')}{rng.randrange(0, 24):02}:{rng.randrange(0, 60):02}"
        text = (f"{year:04}-{month:02}-{day:02}{separator}{hour:02}:{minute:02}:{second:02}"
                + (f".{fraction}" if fraction else "") + zone)
        valid.append(text)
        expected = parse_rfc3339(text)
        assert not expected.startswith("error"), (text, expected)
        if year >= 1:
            offset = 0
            if zone[0] in "+-":
                offset = (1 if zone[0] == "+" else -1) * (int(zone[1:3]) * 3600 + int(zone[4:6]) * 60)
            local = calendar.timegm((year, month, day, hour, minute, second))
            nanoseconds = int((fraction + "000000000")[:9])
            assert expected == f"{local - offset}:{nanoseconds}", (text, expected)
            stdlib_checks += 1
        cases.append(("parse", text, "-", expected))
    http_valid: list[str] = []
    for _ in range(300):
        form = rng.randrange(3)
        low, high = (0, 3155759999) if form == 1 else (FIRST, LAST)
        seconds = rng.randrange(low, high + 1)
        year, month, day, hour, minute, second = civil(seconds)
        name = weekday(seconds)
        if form == 0:
            text = format_http(seconds)
        elif form == 1:
            text = (f"{LONG_DAYS[name]}, {day:02}-{MONTHS[month - 1]}-{year % 100:02} "
                    f"{hour:02}:{minute:02}:{second:02} GMT")
        else:
            text = (f"{DAYS[name]} {MONTHS[month - 1]} {day:2} {hour:02}:{minute:02}:{second:02} "
                    f"{year:04}")
        http_valid.append(text)
        expected = parse_http(text)
        assert expected == f"{seconds}:0", (text, expected)
        # email.utils reads every year below 100 as two digits, and 69 as 1969.
        if year >= 100 and not (form == 1 and year % 100 == 69):
            parsed = email.utils.parsedate_to_datetime(text)
            if parsed.tzinfo is None:
                # The asctime form names no zone; it is GMT (RFC 9110 section 5.6.7).
                parsed = parsed.replace(tzinfo=datetime.timezone.utc)
            assert int(parsed.timestamp()) == seconds, (text, parsed)
            stdlib_checks += 1
        cases.append(("parsehttp", text, "-", expected))
    for _ in range(1500):
        text = mutate(rng, rng.choice(valid))
        cases.append(("parse", text, "-", parse_rfc3339(text)))
    for _ in range(1500):
        text = mutate(rng, rng.choice(http_valid))
        cases.append(("parsehttp", text, "-", parse_http(text)))
    command = [arguments.executable]
    for mode, first, second, _expected in cases:
        command.extend([mode, first, second])
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if result.returncode != 0 or result.stderr:
        raise AssertionError((result.returncode, result.stderr))
    lines = result.stdout.split("\n")
    assert lines[-1] == "" and len(lines) == len(cases) + 1, (len(lines), len(cases))
    errors = 0
    for (mode, first, second, expected), actual in zip(cases, lines):
        if actual != expected:
            errors += 1
            if errors <= 20:
                print(f"MISMATCH {mode} {first!r} {second!r}: expected {expected!r}, got {actual!r}")
    if errors:
        raise SystemExit(f"{errors} of {len(cases)} time cases differ")
    rejected = sum(1 for case in cases if case[3].startswith("error"))
    print(f"time differential cases checked: {len(cases)} ({rejected} rejected, "
          f"{stdlib_checks} values also checked against the Python standard library)")


if __name__ == "__main__":
    main()
