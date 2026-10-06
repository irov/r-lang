module example.text.search;
import std.regex;
import std.text;
import example.calculator.common::{Usage};

protected void append_match(std.string::string* output, str source, std.regex::span match)
    throws std.alloc::alloc_error, core::utf8_error {
    const u8[] data = source;
    const u8[] selected = data[match.start..match.end];
    str selected_text = std.utf8::validate(selected);
    std.string::string row = f"{match.start}:{match.end}\t{selected_text}\n";
    str row_text = row;
    output->append(row_text);
}

std.string::string run(str command, str pattern, str source, str extra)
    throws Usage, std.regex::error, std.alloc::alloc_error, std.convert::parse_error, core::utf8_error {
    bool escape = std.text::equal_ignore_ascii_case(command, "escape");
    if (escape == true) {
        std.string::string escaped = std.regex::escape_literal(pattern);
        escaped.append("\n");
        return move escaped;
    }
    std.regex::options settings = std.regex::default_options();
    bool insensitive = std.text::equal_ignore_ascii_case(command, "find-i");
    settings.ignore_ascii_case = insensitive;
    settings.multiline = true;
    std.regex::regex expression = std.regex::compile_with_options(pattern, settings);
    bool replace = std.text::equal_ignore_ascii_case(command, "replace");
    if (replace == true) {
        std.string::string replaced = expression.replace_all(source, extra);
        replaced.append("\n");
        return move replaced;
    }
    bool split = std.text::equal_ignore_ascii_case(command, "split");
    if (split == true) {
        drop expression;
        // Compile the separator with the default (single-line, case-sensitive) settings.
        std.regex::regex separator = std.regex::compile(pattern);
        array<std.string::string> fields = separator.split(source);
        std.string::string output = std.string::create();
        for (const std.string::string* item in &fields) {
            str item_text = item->as_str();
            output.append(item_text);
            output.append("\n");
        }
        return move output;
    }
    bool check = std.text::equal_ignore_ascii_case(command, "check");
    if (check == true) {
        bool partial = expression.is_match(source);
        bool full = expression.full_match(source);
        std.string::string output = f"match={partial} full={full}\n";
        return move output;
    }
    std.string::string output = std.string::create();
    bool all = std.text::equal_ignore_ascii_case(command, "all");
    if (all == true) {
        array<std.regex::span> matches = expression.find_all(source);
        for (const std.regex::span* match in &matches) { append_match(&output, source, *match); }
        return move output;
    }
    o<std.regex::span> found = o::none;
    bool from = std.text::equal_ignore_ascii_case(command, "from");
    if (from == true) {
        usize offset = std.convert::parse_usize(extra, 10u32);
        found = expression.find_from(source, offset);
    } else {
        bool find = std.text::equal_ignore_ascii_case(command, "find");
        throw (find == false && insensitive == false) Usage { .message = "unknown search command" };
        found = expression.find(source);
    }
    switch (found) {
    case variant o::some(match): append_match(&output, source, *match); break;
    case variant o::none: output.append("none\n"); break;
    }
    return move output;
}
