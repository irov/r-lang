module test.codegen.standard_sequences;

import std.fs;
import std.string;

/* R-GRAM-0005, R-ARRAY-0001: a standard-library type name is a sequence element like any
   aggregate name, so `std.fs::seek_origin[2]` and `const std.string::string[]` are array and
   slice types. */

usize widths(const std.string::string[] texts) {
    usize total = 0usize;
    for (usize index = 0usize; index < len(texts); index += 1usize) {
        str text = texts[index];
        total += len(text);
    }
    return total;
}

std.fs::seek_origin last(std.fs::seek_origin[2] origins) {
    return origins[1];
}

i32 main() {
    std.fs::seek_origin[2] origins = {std.fs::seek_origin::start, std.fs::seek_origin::end};
    if (last(origins) != std.fs::seek_origin::end) {
        return 1;
    }
    try {
        std.string::string[2] texts = {std.string::from_str("abc"), std.string::from_str("de")};
        if (widths(texts[0usize..2usize]) != 5usize) {
            return 2;
        }
        (std.string::string)[1] single = {std.string::from_str("xyz")};
        if (widths(single[..]) != 3usize) {
            return 3;
        }
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 90;
    }
    return 0;
}
