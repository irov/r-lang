module test.codegen.std_console;
import std.console;

/* R-SLIB-CONSOLE-0001..0002 (M18): printing to standard output and standard error, and line
   reads that leave the rest of standard input to another reader. Standard input holds
   "alpha\r\nbeta\nrest". */
async i32 main() {
    await std.console::print(f"hello ");
    await std.console::println(std.string::from_str("console"));
    i32 three = 3;
    await std.console::eprint(f"warning ");
    await std.console::eprintln(f"{three}");
    std.string::string joined = std.string::create();
    std.string::reserve(&joined, 32usize);
    o<std.string::string> first = await std.console::read_line();
    switch (move first) {
    case variant o::some(move text): std.string::append_str(&joined, text);
    case variant o::none: return 1;
    }
    std.string::append_str(&joined, "|");
    o<std.string::string> second = await std.console::read_line();
    switch (move second) {
    case variant o::some(move text): std.string::append_str(&joined, text);
    case variant o::none: return 2;
    }
    std.string::append_str(&joined, "|");
    std.io::input input = std.io::stdin();
    bytes rest = std.alloc::bytes(8usize, 0u8);
    usize count = 0usize;
    task_scope(1) io { count += await std.io::read_into(&input, rest.as_slice_mut()); }
    std.string::append_utf8(&joined, rest[0usize..count]);
    o<std.string::string> ended = await std.console::read_line();
    switch (move ended) {
    case variant o::some(move text):
        drop text;
        return 3;
    case variant o::none: await std.console::println(move joined);
    }
    return 0;
}
