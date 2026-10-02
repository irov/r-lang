module example.statistics.main;
import std.console;
import example.calculator.common::{Usage};
import example.statistics.analysis;
import example.statistics.ordering;
import example.statistics.segments;
import std.text;

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string output; i32 status; };

async i32 main(const str[] arguments) {
    CommandResponse response = {.output = std.string::create(), .status = 0};

    try {
        usize count = len(arguments);
        if (count == 1usize) {
            response.output = std.string::from_str("statistics summary|runs VALUES...\nstatistics page OFFSET LIMIT VALUES...\nstatistics sort|descending|inspect|find WANTED VALUES...\nstatistics schedule FIRST_HOUR END_HOUR\n");
        } else {
            bool schedule = std.text::equal_ignore_ascii_case(arguments[1], "schedule");
            if (schedule == true) {
                throw (count != 4usize) Usage { .message = "schedule needs two hours" };
                i32 low = std.convert::parse_i32(arguments[2], 10u32);
                i32 high = std.convert::parse_i32(arguments[3], 10u32);
                throw (low < 0 || high > 24 || low > high) Usage { .message = "hours must satisfy 0 <= first <= end <= 24" };
                response.output = example.statistics.analysis::schedule(low, high);
            } else {
                bool runs = std.text::equal_ignore_ascii_case(arguments[1], "runs");
                usize start = (runs == true || (std.text::equal_ignore_ascii_case(arguments[1], "summary")) == true) ? 2usize : ((std.text::equal_ignore_ascii_case(arguments[1], "page")) == true ? 4usize : 3usize);
                throw (count < start) Usage { .message = "missing command parameters" };
                array<i32> readings = std.array::with_capacity::<i32>(count - start);
                usize index = start;
                while (index < count) {
                    i32 value = std.convert::parse_i32(arguments[index], 10u32);
                    readings.push(value);
                    index += 1usize;
                }
                if (runs == true || (std.text::equal_ignore_ascii_case(arguments[1], "summary")) == true) {
                    const i32[] view = readings.as_slice();
                    if (runs == true) {
                        response.output = example.statistics.segments::describe_runs(view);
                    } else {
                        response.output = example.statistics.analysis::summary(view);
                    }
                } else {
                    if ((std.text::equal_ignore_ascii_case(arguments[1], "page")) == true) {
                        usize offset = std.convert::parse_usize(arguments[2], 10u32);
                        usize limit = std.convert::parse_usize(arguments[3], 10u32);
                        const i32[] view = readings.as_slice();
                        response.output = example.statistics.analysis::page(view, offset, limit);
                    } else {
                        i32 wanted = std.convert::parse_i32(arguments[2], 10u32);
                        bool inspect = std.text::equal_ignore_ascii_case(arguments[1], "inspect");
                        if (inspect == true) {
                            const i32[] view = readings.as_slice();
                            response.output = example.statistics.ordering::describe(view, wanted);
                        } else {
                            bool find = std.text::equal_ignore_ascii_case(arguments[1], "find");
                            if (find == true) {
                                response.output = await example.statistics.segments::locate(core::replace(&readings, std.array::create::<i32>()), wanted);
                            } else {
                                bool descending = std.text::equal_ignore_ascii_case(arguments[1], "descending");
                                throw (descending == false && (std.text::equal_ignore_ascii_case(arguments[1], "sort")) == false) Usage { .message = "unknown command" };
                                response.output = example.statistics.ordering::sorted(&readings, descending, wanted);
                            }
                        }
                    }
                }
            }
        }
    } catch (Usage failure) { response.output = f"{failure.message}\n"; response.status = 64; }
    catch (std.convert::parse_error failure) {
        std.error::error error = std.error::from_parse(failure);
        response.output = error.diagnostic(); response.status = 65;
    } catch (std.array::push_error<i32> failure) { return 71; }
    catch (std.array::push_error<const i32[]> failure) { return 71; }
    catch (std.array::push_error<i64> failure) { return 71; }
    catch (std.list::push_error<i32> failure) { return 71; }
    await std.console::print(core::replace(&response.output, std.string::create()));
    return response.status;
}
