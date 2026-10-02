module example.pipeline.main;
import std.console;
import example.pipeline.stages;

/* pipeline LIMIT FACTOR VALUE...: bounds every value by LIMIT, scales it by FACTOR (or takes
   its magnitude when FACTOR is 0) and prints the count, sum and maximum. */
async i32 main(const str[] arguments) {
    try {
        usize count = len(arguments);
        if (count == 1usize) {
            await std.console::print(
                std.string::from_str("pipeline LIMIT FACTOR VALUE...\n"));
            return 0;
        }
        if (count < 4usize) {
            return 64;
        }
        i64 limit = std.convert::parse_i64(arguments[1], 10u32);
        i64 factor = std.convert::parse_i64(arguments[2], 10u32);
        array<i64> values = std.array::with_capacity::<i64>(count - 3usize);
        for (usize index = 3usize; index < count; index += 1usize) {
            values.push(std.convert::parse_i64(arguments[index], 10u32));
        }
        auto admit = example.pipeline.stages::bounded(limit);
        i64[] readings = values.as_slice_mut();
        if (factor == 0) {
            example.pipeline.stages::run(readings, admit, example.pipeline.stages::magnitude);
        } else {
            example.pipeline.stages::run(readings, admit, example.pipeline.stages::scaled(factor));
        }
        const i64[] results = values.as_slice();
        i64 total = example.pipeline.stages::fold(results, 0, example.pipeline.stages::add);
        i64 maximum = results[0usize];
        for (usize index = 1usize; index < len(results); index += 1usize) {
            if (results[index] > maximum) {
                maximum = results[index];
            }
        }
        usize processed = len(results);
        std.string::string output = f"count={processed} sum={total} max={maximum}\n";
        await std.console::print(move output);
        return 0;
    } catch (std.convert::parse_error failure) {
        return 65;
    } catch (example.pipeline.stages::OutOfRange failure) {
        return 66;
    } catch (example.pipeline.stages::Overflow failure) {
        return 67;
    } catch (std.array::push_error<i64> failure) {
        return 71;
    }
}
