module test.semantic.regex_private;
import std.regex;
i32 main() {
    try {
        std.regex::regex value = std.regex::compile("a");
        value.entry = 50000usize;
    } catch (std.regex::error failure) { return 2; }
    catch (std.alloc::alloc_error failure) { return 3; }
    return 0;
}
