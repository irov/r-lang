#!/usr/bin/env python3
"""Check the time zones of std.time against the Python zoneinfo module over the zone files of
the system.

The program under test reads lines of tests/fixtures/codegen_zone_driver.r. For every zone that
zoneinfo finds, instants from 1900 to 2100 give the same local date, time, offset, abbreviation,
weekday and day of the year; around the transitions of zones with daylight saving time, local
times resolve to the same instants for fold 0 (`earlier`) and fold 1 (`later`), and `reject`
reports the gaps and overlaps; the start of the local day, and days and months added in the
zone, agree with the same computations made here. POSIX TZ strings are checked against the
zone files whose footer they are.
"""
from __future__ import annotations

import argparse
import random
import subprocess
import sys
from datetime import datetime, timedelta, timezone
from zoneinfo import ZoneInfo, available_timezones

UTC = timezone.utc


def local_line(zone: ZoneInfo, instant: int) -> str:
    moment = datetime.fromtimestamp(instant, tz=zone)
    offset = int(moment.utcoffset().total_seconds())
    dst = "true" if moment.dst() else "false"
    return (f"{moment.year}-{moment.month}-{moment.day} {moment.hour}:{moment.minute}:{moment.second} "
            f"{offset} {dst} {moment.tzname()} {moment.isoweekday()} {moment.timetuple().tm_yday}")


def instant_of(zone: ZoneInfo, wall: datetime, fold: int) -> int:
    return int(wall.replace(tzinfo=zone, fold=fold).timestamp())


def exists(zone: ZoneInfo, wall: datetime) -> bool:
    moment = wall.replace(tzinfo=zone)
    back = datetime.fromtimestamp(moment.timestamp(), tz=zone).replace(tzinfo=None)
    return back == wall


def ambiguous(zone: ZoneInfo, wall: datetime) -> bool:
    return (wall.replace(tzinfo=zone, fold=0).utcoffset() != wall.replace(tzinfo=zone, fold=1).utcoffset()
            and exists(zone, wall))


def transitions(zone: ZoneInfo, start_year: int, end_year: int) -> list[int]:
    """The instants where the offset changes, found by sampling every 12 hours and bisecting."""
    found = []
    step = 12 * 3600
    instant = int(datetime(start_year, 1, 1, tzinfo=UTC).timestamp())
    end = int(datetime(end_year, 1, 1, tzinfo=UTC).timestamp())
    previous = datetime.fromtimestamp(instant, tz=zone).utcoffset()
    while instant < end:
        following = datetime.fromtimestamp(instant + step, tz=zone).utcoffset()
        if following != previous:
            low, high = instant, instant + step
            while high - low > 1:
                middle = (low + high) // 2
                if datetime.fromtimestamp(middle, tz=zone).utcoffset() == previous:
                    low = middle
                else:
                    high = middle
            found.append(high)
        previous = following
        instant += step
    return found


def wall_command(name: str, wall: datetime, choice: str) -> str:
    return (f"resolve {name} {wall.year} {wall.month} {wall.day} {wall.hour} {wall.minute} {wall.second} "
            f"{choice}")


def resolve_cases(name: str, zone: ZoneInfo, instant: int):
    """Local times just before, at and after a transition and inside its gap or overlap."""
    before = datetime.fromtimestamp(instant - 1, tz=zone).replace(tzinfo=None)
    after = datetime.fromtimestamp(instant, tz=zone).replace(tzinfo=None)
    walls = {before, after, before + timedelta(seconds=1), after - timedelta(seconds=1)}
    middle = before + (after - before) / 2 if after > before else after + (before - after) / 2
    walls.add(middle.replace(microsecond=0))
    for wall in sorted(walls):
        if wall.year < 1900 or wall.year > 2199:
            continue
        for choice, fold in (("earlier", 0), ("later", 1)):
            yield wall_command(name, wall, choice), str(instant_of(zone, wall, fold))
        if not exists(zone, wall):
            yield wall_command(name, wall, "reject"), "error nonexistent_time"
        elif ambiguous(zone, wall):
            yield wall_command(name, wall, "reject"), "error ambiguous_time"
        else:
            yield wall_command(name, wall, "reject"), str(instant_of(zone, wall, 0))


def last_day(year: int, month: int) -> int:
    following = datetime(year + month // 12, month % 12 + 1, 1)
    return (following - timedelta(days=1)).day


def calendar_cases(name: str, zone: ZoneInfo, instant: int, rng: random.Random):
    moment = datetime.fromtimestamp(instant, tz=zone)
    midnight = moment.replace(tzinfo=None, hour=0, minute=0, second=0, microsecond=0)
    yield f"start {name} {instant}", str(instant_of(zone, midnight, 0))
    days = rng.randrange(-400, 400)
    moved = moment.replace(tzinfo=None) + timedelta(days=days)
    yield f"days {name} {instant} {days}", str(instant_of(zone, moved, 0))
    months = rng.randrange(-30, 30)
    index = moment.year * 12 + moment.month - 1 + months
    year, month = divmod(index, 12)
    month += 1
    target = moment.replace(tzinfo=None, year=year, month=month, day=min(moment.day, last_day(year, month)))
    yield f"months {name} {instant} {months}", str(instant_of(zone, target, 0))


def cases(rng: random.Random):
    names = sorted(name for name in available_timezones()
                   if not name.startswith(("posix/", "right/")) and name not in ("Factory", "localtime"))
    low = int(datetime(1900, 1, 2, tzinfo=UTC).timestamp())
    high = int(datetime(2100, 12, 30, tzinfo=UTC).timestamp())
    for name in names:
        zone = ZoneInfo(name)
        for _ in range(8):
            instant = rng.randrange(low, high)
            yield f"local {name} {instant}", local_line(zone, instant)
        instant = rng.randrange(int(datetime(1970, 1, 2, tzinfo=UTC).timestamp()), high)
        yield from calendar_cases(name, zone, instant, rng)
    # Zones with daylight saving time: the transitions of recent years and of the years after the
    # data of the file, where the footer rule applies.
    for name in ("America/New_York", "Europe/Berlin", "Europe/London", "Europe/Dublin", "Australia/Sydney",
                 "Australia/Lord_Howe", "America/Sao_Paulo", "Asia/Beirut", "America/Havana", "Pacific/Chatham",
                 "America/Santiago", "Africa/Casablanca", "Asia/Tehran", "America/Godthab", "Antarctica/Troll",
                 "Europe/Moscow", "America/St_Johns", "Asia/Gaza"):
        if name not in available_timezones():
            continue
        zone = ZoneInfo(name)
        for years in ((2018, 2026), (2085, 2088)):
            for instant in transitions(zone, *years):
                yield from resolve_cases(name, zone, instant)
                yield from calendar_cases(name, zone, instant, rng)
                yield f"local {name} {instant - 1}", local_line(zone, instant - 1)
                yield f"local {name} {instant}", local_line(zone, instant)
    # POSIX TZ strings: the footer of a zone describes the zone after the last transition.
    for rule, name in (("EST5EDT,M3.2.0,M11.1.0", "America/New_York"), ("CET-1CEST,M3.5.0,M10.5.0/3", "Europe/Berlin"),
                       ("AEST-10AEDT,M10.1.0,M4.1.0/3", "Australia/Sydney"), ("<+0330>-3:30", "Asia/Tehran"),
                       ("NZST-12NZDT,M9.5.0,M4.1.0/3", "Pacific/Auckland"), ("<-03>3", "America/Sao_Paulo")):
        zone = ZoneInfo(name)
        for _ in range(12):
            instant = rng.randrange(int(datetime(2040, 1, 1, tzinfo=UTC).timestamp()), high)
            yield f"local posix:{rule} {instant}", local_line(zone, instant)
    yield "local Nowhere/City 0", "error unknown_zone"
    yield "local ../etc/passwd 0", "error invalid_name"
    yield "local /etc/localtime 0", "error invalid_name"
    yield "local posix:X1 0", "error malformed"
    yield "resolve UTC 2024 2 30 0 0 0 earlier", "error time invalid_value"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True)
    arguments = parser.parse_args()
    rng = random.Random(39)
    pairs = list(cases(rng))
    run = subprocess.run([arguments.executable], input="\n".join(command for command, _ in pairs) + "\n",
                         capture_output=True, text=True, timeout=600)
    if run.returncode != 0:
        print(run.stdout[-2000:], run.stderr[-2000:], file=sys.stderr)
        return 1
    outputs = run.stdout.rstrip("\n").split("\n")
    failures = 0
    for (command, expected), actual in zip(pairs, outputs):
        if actual != expected:
            failures += 1
            if failures <= 40:
                print(f"FAIL {command}\n  expected {expected}\n  actual   {actual}")
    if len(outputs) != len(pairs):
        failures += 1
        print(f"FAIL {len(outputs)} outputs for {len(pairs)} commands")
    print(f"zone vectors: {len(pairs) - failures}/{len(pairs)} checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
