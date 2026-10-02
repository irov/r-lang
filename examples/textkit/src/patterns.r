module example.textkit.patterns;
import std.regex;

/* The text of a span; spans lie at scalar boundaries. */
str span_text(str text, std.regex::span place) {
    const u8[] bytes = text;
    try {
        return core::validate_utf8(bytes[place.start..place.end]);
    } catch (core::utf8_error failure) {
        failure as void;
        return "";
    }
}

/* textkit groups PATTERN TEXT [OFFSET]: the match that search selects from OFFSET and every
   capturing group of it. */
std.string::string captured(str pattern, str text, usize offset)
    throws std.regex::error, std.alloc::alloc_error {
    std.regex::regex compiled = std.regex::compile(pattern);
    usize count = std.regex::group_count(&compiled);
    o<array<o<std.regex::span>>> found = o::none;
    if (offset == 0usize) {
        found = std.regex::captures(&compiled, text);
    } else {
        found = std.regex::captures_from(&compiled, text, offset);
    }
    std.string::string out = f"{count} groups\n";
    switch (found) {
    case variant o::some(spans):
        for (usize index = 0usize; index < len(*spans); index += 1usize) {
            auto slot = std.array::get(spans, index);
            switch (slot) {
            case variant o::some(entry):
                switch (**entry) {
                case variant o::some(place):
                    usize start = place->start;
                    usize end = place->end;
                    str piece = span_text(text, *place);
                    std.string::string row = f"{index}: {start}..{end} [{piece}]\n";
                    std.string::append_str(&out, row.as_str());
                case variant o::none:
                    std.string::string row = f"{index}: none\n";
                    std.string::append_str(&out, row.as_str());
                }
            case variant o::none:
                break;
            }
        }
    case variant o::none:
        std.string::append_str(&out, "no match\n");
    }
    return move out;
}
