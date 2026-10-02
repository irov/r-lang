module tests.std.console;
import std.test;
import std.console;

// The tests of std.console (Library R-SLIB-CONSOLE-0001..0002): printing short texts to standard
// output and standard error, with and without a line feed, and the limit of read_line. The tests
// read nothing from standard input. Run in test mode (Core R-FUNC-0025).

@test
async void prints_to_standard_output() throws std.error::fault {
    u32 count = 2u32;
    await std.console::print(f"std.console: {count} ");
    await std.console::println(std.string::from_str("texts on standard output"));
}

@test
async void prints_to_standard_error() throws std.error::fault {
    f64 ratio = 1.5;
    await std.console::eprint(std.string::from_str("std.console: "));
    await std.console::eprintln(f"a line on standard error, {ratio}");
}

@test
async void prints_text_beyond_ascii() throws std.error::fault {
    std.string::string text = std.string::from_str("std.console: ");
    std.string::append_str(&text, "héllo, wörld € 😀 ");
    bool done = true;
    await std.console::print(move text);
    await std.console::println(f"{done}");
}

@test
async void prints_nothing_for_empty_text() throws std.error::fault {
    await std.console::print(std.string::create());
    await std.console::eprint(std.string::create());
    await std.console::print(f"");
}

@test
void declares_the_longest_line() throws std.test::failure, std.alloc::alloc_error {
    std.test::equal(std.console::max_line, 65536usize);
}
