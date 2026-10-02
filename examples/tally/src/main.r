module example.tally.main;
import std.console;
import example.tally.cells;

/* tally VALUE...: prints the count, sum, minimum and maximum of the values and whether the sum
   is even, as labelled columns. */
async i32 main(const str[] arguments) {
    try {
        usize count = len(arguments);
        if (count == 1usize) {
            await std.console::print(std.string::from_str("tally VALUE...\n"));
            return 0;
        }
        array<i64> values = std.array::with_capacity::<i64>(count - 1usize);
        for (usize index = 1usize; index < count; index += 1usize) {
            values.push(std.convert::parse_i64(arguments[index], 10u32));
        }
        // The summary tuple is destructured into one local per element (R-STMT-0022).
        auto (entries, sum, low, high) = example.tally.cells::summarize(values.as_slice());
        bool even = sum % 2 == 0;
        auto columns = (example.tally.cells::named("count", entries),
                        example.tally.cells::named("sum", sum),
                        example.tally.cells::named("min", low),
                        example.tally.cells::named("max", high),
                        example.tally.cells::named("even", even));
        std.string::string text = example.tally.cells::line(...columns);
        std.string::string output = f"{text}\n";
        await std.console::print(move output);
        return 0;
    } catch (std.convert::parse_error failure) {
        return 65;
    } catch (std.array::push_error<i64> failure) {
        return 71;
    } catch (std.alloc::alloc_error failure) {
        return 70;
    }
}
