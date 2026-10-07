module test.codegen.lending_methods;

import std.string;

/* R-TYPE-0045 (L16.1): a `@lending` method lends views of the storage its receiver designates;
   the result holds the whole receiver, so its elements are used one at a time. */
trait LineSource {
    type Line;
    @lending o<Self::Line> next_line(Self* this);
};

struct Lines {
    std.string::string buffer;
    usize served;
    usize limit;
};

impl LineSource for Lines {
    type Line = str;
    o<str> next_line(Lines* this) {
        if (this->served >= this->limit) { return o::none; }
        this->served += 1usize;
        return o::some(this->buffer);
    }
};

Lines make_lines(str text, usize limit) throws std.alloc::alloc_error {
    return Lines {.buffer = std.string::from_str(text), .served = 0usize, .limit = limit};
}

usize direct(Lines* lines) {
    usize total = 0usize;
    while (true) {
        o<str> line = lines->next_line();
        switch (move line) {
        case variant o::some(move text):
            total += len(text);
            break;
        case variant o::none:
            return total;
        }
    }
    return total;
}

/* Generic code sees the lending contract through the constraint. */
@generic<L: LineSource>
usize count(L* source) {
    usize lines = 0usize;
    while (true) {
        o<L::Line> line = source->next_line();
        switch (move line) {
        case variant o::some(move item):
            move item as void;
            lines += 1usize;
            break;
        case variant o::none:
            return lines;
        }
    }
    return lines;
}

/* A dyn interface keeps the contract of its trait. */
usize through_dyn(dyn(LineSource & Line = str)* source) {
    usize total = 0usize;
    while (true) {
        o<str> line = source->next_line();
        switch (move line) {
        case variant o::some(move text):
            total += len(text);
            break;
        case variant o::none:
            return total;
        }
    }
    return total;
}

/* An opaque result keeps it as well. */
opaque(LineSource & Line = str) opaque_lines(str text) throws std.alloc::alloc_error {
    return make_lines(text, 2usize);
}

usize through_opaque(str input) throws std.alloc::alloc_error {
    auto source = opaque_lines(input);
    usize total = 0usize;
    while (true) {
        o<str> line = source.next_line();
        switch (move line) {
        case variant o::some(move text):
            total += len(text);
            break;
        case variant o::none:
            return total;
        }
    }
    return total;
}

/* R-TYPE-0046, R-STMT-0014 (L16.2): range-for advances a core::LendingIterator; each element
   lives until the next advance. */
struct Words {
    std.string::string buffer;
    usize served;
};

impl core::LendingIterator for Words {
    type Item = str;
    o<str> next(Words* this) {
        if (this->served >= 3usize) { return o::none; }
        this->served += 1usize;
        return o::some(this->buffer);
    }
};

usize range_total(str text) throws std.alloc::alloc_error {
    Words words = Words {.buffer = std.string::from_str(text), .served = 0usize};
    usize total = 0usize;
    for (str word in &words) {
        total += len(word);
    }
    return total;
}

i32 main() {
    try {
        Lines first = make_lines("abc", 2usize);
        if (direct(&first) != 6usize) { return 1; }
        Lines second = make_lines("hello", 3usize);
        if (count(&second) != 3usize) { return 2; }
        Lines third = make_lines("xy", 4usize);
        if (through_dyn(&third) != 8usize) { return 3; }
        if (through_opaque("four") != 8usize) { return 4; }
        if (range_total("abcd") != 12usize) { return 5; }
        return 0;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 90;
    }
}
