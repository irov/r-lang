module test.semantic.regex_unhandled;
import std.regex;
i32 main() {
    std.regex::regex value = std.regex::compile("a");
    return 0;
}
