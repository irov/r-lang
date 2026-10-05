module example.clock.main;
import std.console;
import std.time;
import example.calculator.common::{Usage};
import example.clock.operations;
import example.clock.zones;

struct CommandStorage1 { std.time::duration value; };

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string output; i32 status; };

enum Command { now, shift, utc, add, sub, compare, mul, sleep, rfc3339, http, parse, ticks, zone, zones, wall, tzif, local };

async i32 main(const str[] arguments) {
    CommandResponse response = {.output = std.string::create(), .status = 0};

    try {
        usize count = len(arguments);
        if (count == 1usize) {
            response.output = std.string::from_str("clock now\nclock shift UNIX_SECONDS NANOSECONDS DELTA_SECONDS\nclock utc YEAR MONTH DAY HOUR MINUTE SECOND NANOSECOND\nclock add|sub|compare SECONDS NANOSECONDS SECONDS NANOSECONDS\nclock mul SECONDS NANOSECONDS FACTOR\nclock sleep MILLISECONDS\nclock rfc3339 UNIX_SECONDS NANOSECONDS DIGITS\nclock http UNIX_SECONDS\nclock parse TEXT\nclock ticks COUNT MILLISECONDS [PAUSE_MILLISECONDS]\nclock zone NAME UNIX_SECONDS\nclock zones UNIX_SECONDS\nclock wall NAME YEAR MONTH DAY HOUR MINUTE SECOND\nclock tzif PATH UNIX_SECONDS\nclock local UNIX_SECONDS\n");
        } else {
            o<Command> parsed = core::enum_from_name::<Command>(arguments[1]);
            Command command = Command::now;
            switch (parsed) {
            case variant o::some(value): command = *value; break;
            case variant o::none: throw Usage { .message = "unknown clock command" };
            }
            switch (command) {
            case Command::now:
                throw (count != 2usize) Usage { .message = "now takes no operands" };
                std.time::system_time instant = std.time::system_now();
                response.output = example.clock.operations::calendar(instant); break;
            case Command::shift:
                throw (count != 5usize) Usage { .message = "shift needs a timestamp and delta" };
                i64 seconds = std.convert::parse_i64(arguments[2], 10u32);
                u32 nanoseconds = std.convert::parse_u32(arguments[3], 10u32);
                i64 change = std.convert::parse_i64(arguments[4], 10u32);
                std.time::system_time base = { .unix_seconds = seconds, .nanoseconds = nanoseconds };
                std.time::duration delta = std.time::duration_from_seconds(change);
                std.time::system_time shifted = base.add(delta);
                response.output = example.clock.operations::calendar(shifted); break;
            case Command::utc:
                throw (count != 9usize) Usage { .message = "utc needs seven calendar components" };
                i32 year = std.convert::parse_i32(arguments[2], 10u32);
                u8 month = std.convert::parse_u8(arguments[3], 10u32);
                u8 day = std.convert::parse_u8(arguments[4], 10u32);
                u8 hour = std.convert::parse_u8(arguments[5], 10u32);
                u8 minute = std.convert::parse_u8(arguments[6], 10u32);
                u8 second = std.convert::parse_u8(arguments[7], 10u32);
                u32 nanosecond = std.convert::parse_u32(arguments[8], 10u32);
                std.time::utc_datetime utc = { .year = year, .month = month, .day = day,
                    .hour = hour, .minute = minute, .second = second, .nanosecond = nanosecond };
                std.time::system_time instant = std.time::from_utc(utc);
                response.output = example.clock.operations::calendar(instant); break;
            case Command::sleep:
                throw (count != 3usize) Usage { .message = "sleep needs a millisecond duration" };
                u32 milliseconds = std.convert::parse_u32(arguments[2], 10u32);
                throw (milliseconds > 5000u32) Usage { .message = "demo sleep limit is 5000 milliseconds" };
                std.string::string elapsed = await example.clock.operations::delay(milliseconds);
                response.output = move elapsed; break;
            case Command::rfc3339:
                throw (count != 5usize) Usage { .message = "rfc3339 needs a timestamp and a digit count" };
                i64 seconds = std.convert::parse_i64(arguments[2], 10u32);
                u32 nanoseconds = std.convert::parse_u32(arguments[3], 10u32);
                u32 digits = std.convert::parse_u32(arguments[4], 10u32);
                std.time::system_time instant = { .unix_seconds = seconds, .nanoseconds = nanoseconds };
                std.string::string text = std.time::format_rfc3339(instant, digits);
                response.output = f"{text}\n"; break;
            case Command::http:
                throw (count != 3usize) Usage { .message = "http needs a timestamp" };
                i64 seconds = std.convert::parse_i64(arguments[2], 10u32);
                std.string::string text = std.time::format_http_date(
                    std.time::system_time { .unix_seconds = seconds, .nanoseconds = 0u32 });
                response.output = f"{text}\n"; break;
            case Command::parse:
                throw (count != 3usize) Usage { .message = "parse needs one date and time" };
                response.output = example.clock.operations::parsed(arguments[2]); break;
            case Command::ticks:
                throw (count != 4usize && count != 5usize)
                    Usage { .message = "ticks needs a count, a period and an optional pause" };
                u32 ticks = std.convert::parse_u32(arguments[2], 10u32);
                u32 period = std.convert::parse_u32(arguments[3], 10u32);
                u32 pause = 0u32;
                if (count == 5usize) { pause = std.convert::parse_u32(arguments[4], 10u32); }
                throw (ticks == 0u32 || ticks > 100u32 || period == 0u32 || period > 1000u32 ||
                       pause > 5000u32)
                    Usage { .message = "demo limit is 1 to 100 ticks of 1 to 1000 ms and a 5000 ms pause" };
                std.string::string report = await example.clock.operations::ticked(ticks, period, pause);
                response.output = move report; break;
            case Command::zone:
                throw (count != 4usize) Usage { .message = "zone needs a zone name and a timestamp" };
                std.time::system_time instant = { .unix_seconds = std.convert::parse_i64(arguments[3], 10u32), .nanoseconds = 0u32 };
                std.string::string text = await example.clock.zones::named(std.string::from_str(arguments[2]), instant);
                response.output = move text; break;
            case Command::zones:
                throw (count != 3usize) Usage { .message = "zones needs a timestamp" };
                std.time::system_time instant = { .unix_seconds = std.convert::parse_i64(arguments[2], 10u32), .nanoseconds = 0u32 };
                response.output = example.clock.zones::three_zones(instant); break;
            case Command::wall:
                throw (count != 9usize) Usage { .message = "wall needs a zone name and six calendar components" };
                std.time::local_time wall = { .year = std.convert::parse_i32(arguments[3], 10u32),
                    .month = std.convert::parse_u8(arguments[4], 10u32), .day = std.convert::parse_u8(arguments[5], 10u32),
                    .hour = std.convert::parse_u8(arguments[6], 10u32), .minute = std.convert::parse_u8(arguments[7], 10u32),
                    .second = std.convert::parse_u8(arguments[8], 10u32), .nanosecond = 0u32, .weekday = 0u8,
                    .year_day = 0u16, .offset = 0i32, .dst = false };
                std.string::string text = await example.clock.zones::named_wall(std.string::from_str(arguments[2]), wall);
                response.output = move text; break;
            case Command::tzif:
                throw (count != 4usize) Usage { .message = "tzif needs a file and a timestamp" };
                std.time::system_time instant = { .unix_seconds = std.convert::parse_i64(arguments[3], 10u32), .nanoseconds = 0u32 };
                std.string::string text = await example.clock.zones::from_file(std.string::from_str(arguments[2]), instant);
                response.output = move text; break;
            case Command::local:
                throw (count != 3usize) Usage { .message = "local needs a timestamp" };
                std.time::system_time instant = { .unix_seconds = std.convert::parse_i64(arguments[2], 10u32), .nanoseconds = 0u32 };
                std.string::string text = await example.clock.zones::process_zone(instant);
                response.output = move text; break;
            case Command::add: fallthrough;
            case Command::sub: fallthrough;
            case Command::compare: fallthrough;
            case Command::mul:
                usize expected = command == Command::mul ? 5usize : 6usize;
                throw (count != expected) Usage { .message = "wrong number of duration operands" };
                i64 seconds = std.convert::parse_i64(arguments[2], 10u32);
                u32 nanoseconds = std.convert::parse_u32(arguments[3], 10u32);
                i64 right_seconds = std.convert::parse_i64(arguments[4], 10u32);
                std.time::duration left = std.time::duration_from_parts(seconds, nanoseconds);
                if (command == Command::mul) {
                    std.time::duration product = left.multiply(right_seconds);
                    response.output = example.clock.operations::duration(product);
                } else {
                    u32 right_nanoseconds = std.convert::parse_u32(arguments[5], 10u32);
                    std.time::duration right = std.time::duration_from_parts(right_seconds, right_nanoseconds);
                    if (command == Command::compare) {
                        i32 order = left.compare(right);
                        response.output = f"order={order}\n";
                    } else {
                        CommandStorage1 state_result = {.value = std.time::duration_from_seconds(0i64)};
                        if (command == Command::add) { state_result.value = left.add(right); }
                        else { state_result.value = left.sub(right); }
                        response.output = example.clock.operations::duration(state_result.value);
                    }
                }
                break;
            }
        }
    } catch (Usage failure) { response.output = f"{failure.message}\n"; response.status = 64; }
    catch (std.convert::parse_error failure) {
        std.error::error error = std.error::from_parse(failure);
        std.string::string diagnostic = error.diagnostic();
        usize index = failure.index;
        response.output = f"{diagnostic} index={index}\n"; response.status = 65;
    } catch (std.time::zone_error failure) {
        std.time::zone_error_code code = failure.code;
        response.output = f"zone: {code}\n"; response.status = 65;
    } catch (std.time::duration_error failure) {
        response.output = std.string::from_str("invalid or overflowing duration\n"); response.status = 65;
    } catch (std.time::time_error failure) {
        std.time::error_code code = failure.code;
        if (code == std.time::error_code::invalid_value) {
            response.output = std.string::from_str("invalid calendar or clock value\n");
        } else {
            std.error::error error = failure.as_error();
            response.output = error.diagnostic();
        }
        response.status = 65;
    }
    await std.console::print(core::replace(&response.output, std.string::create()));
    return response.status;
}
