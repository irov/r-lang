module example.fanout.main;
import std.console;

opaque(fn(i64) -> i64 & copy) calibration(i64 offset) {
    fn i64 adjust(i64 reading) move(offset) { return reading + offset; }
    return adjust;
}

@scoped
async i64 total(const i64[] readings) {
    i64 result = 0;
    for (usize i = 0usize; i < len(readings); i += 1usize) { result += readings[i]; }
    return result;
}

@scoped
async i64 peak(const i64[] readings) {
    i64 result = readings[0];
    for (usize i = 1usize; i < len(readings); i += 1usize) {
        i64 value = readings[i];
        if (value > result) { result = value; }
    }
    return result;
}

struct Analysis { i64 sum; i64 maximum; };

async i32 main(const str[] arguments) {
    try {
        usize count = len(arguments);
        if (count == 1usize) {
            await std.console::print(std.string::from_str("fanout OFFSET READING...\n"));
            return 0;
        }
        if (count < 3usize) { return 64; }
        auto adjust = calibration(std.convert::parse_i64(arguments[1], 10u32));
        array<i64> readings = std.array::with_capacity::<i64>(count - 2usize);
        readings.push(adjust(std.convert::parse_i64(arguments[2], 10u32)));
        for (usize i = 3usize; i < count; i += 1usize) {
            readings.push(adjust(std.convert::parse_i64(arguments[i], 10u32)));
        }
        Analysis result = {.sum = 0, .maximum = 0};
        bool finished = false;
        std.time::instant deadline =
            std.time::instant_add(std.time::monotonic_now(), std.time::duration_from_seconds(5i64));
        task_scope(2) analysis {
            auto sum_operation = total(readings.as_slice());
            auto peak_operation = peak(readings.as_slice());
            // The first finished analysis is consumed first; the other is awaited in its clause.
            select (analysis) {
            case i64 sum = await move sum_operation:
                result.sum = sum;
                result.maximum = await move peak_operation;
                finished = true;
                break;
            case i64 maximum = await move peak_operation:
                result.maximum = maximum;
                result.sum = await move sum_operation;
                finished = true;
                break;
            case until (deadline):
                // Nothing is cancelled here: the group exit cancels and drains both analyses.
                break;
            }
        }
        if (finished == false) { return 75; }
        usize sample_count = len(readings);
        std.string::string output = f"count={sample_count} sum={result.sum} peak={result.maximum}\n";
        await std.console::print(move output);
        return 0;
    } catch (std.convert::parse_error failure) { return 65; }
    catch (std.array::push_error<i64> failure) { return 71; }
}
