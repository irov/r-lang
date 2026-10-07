module test.codegen.async_lending_methods;

import std.string;

/* R-TYPE-0045 (L16.1, L16.2): an async frame uses lending methods between its awaits. A lent
   element is a view of the receiver storage, so it ends before each await (R-BORROW-0024); the
   receivers themselves hold no views and stay in the frame. */
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

struct Words {
    std.string::string buffer;
    usize served;
};

impl core::LendingIterator for Words {
    type Item = str;
    o<str> next(Words* this) {
        if (this->served >= 2usize) { return o::none; }
        this->served += 1usize;
        return o::some(this->buffer);
    }
};

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

protected async i32 tick(i32 value) {
    return value + 1;
}

protected async i32 work() throws std.alloc::alloc_error, std.async::start_error {
    Lines lines = Lines {.buffer = std.string::from_str("abc"), .served = 0usize, .limit = 3usize};
    i32 one = await tick(0);
    usize total = 0usize;
    bool more = true;
    while (more == true) {
        o<str> line = lines.next_line();
        switch (move line) {
        case variant o::some(move text):
            total += len(text);
            break;
        case variant o::none:
            more = false;
            break;
        }
    }
    i32 status = 0;
    if (total != 9usize) { status = 1; }
    i32 two = await tick(one);
    Lines again = Lines {.buffer = std.string::from_str("xy"), .served = 0usize, .limit = 4usize};
    usize counted = count(&again);
    Words words = Words {.buffer = std.string::from_str("four"), .served = 0usize};
    usize letters = 0usize;
    for (str word in &words) {
        letters += len(word);
    }
    i32 three = await tick(two);
    if (counted != 4usize) { status = 2; }
    if (letters != 8usize) { status = 3; }
    return status + three - 3;
}

async i32 main() {
    try {
        return await work();
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 90;
    } catch (std.async::start_error failure) {
        failure as void;
        return 91;
    }
}
