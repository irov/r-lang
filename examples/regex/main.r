module example.regex;

import std.regex;
import std.text;

/* Compile once, borrow for each search, and retain independently owned output. */
i32 main() {
    try {
        std.regex::regex number = std.regex::compile("[0-9]+");
        o<std.regex::span> first = number.find("item=42 count=7");
        switch (first) {
        case variant o::some(value):
            if (value->start != 5usize || value->end != 7usize) { return 1; }
            break;
        case variant o::none: return 2;
        }
        std.string::string redacted = number.replace_all("item=42 count=7", "#");
        str text = redacted.as_str();
        bool correct = std.text::equal_ignore_ascii_case(text, "item=# count=#");
        if (correct == false) { return 3; }

        std.regex::regex separator = std.regex::compile("[,;]\\s*");
        array<std.string::string> fields = separator.split("one, two;three");
        if (len(fields) != 3usize) { return 4; }
        return 0;
    } catch (std.regex::error failure) {
        // failure.code identifies the error; failure.offset is a UTF-8 byte position.
        return 5;
    }
}
