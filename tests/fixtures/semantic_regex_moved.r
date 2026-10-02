module test.semantic.regex_moved;
import std.regex;
i32 main() {
    try {
        std.regex::regex first = std.regex::compile("a");
        std.regex::regex second = move first;
        bool matched = std.regex::is_match(&first, "a");
        i32 selected = matched == true ? 0 : 1;
        return selected;
    } catch (std.regex::error failure) { return 2; }
    catch (std.alloc::alloc_error failure) { return 3; }
}
