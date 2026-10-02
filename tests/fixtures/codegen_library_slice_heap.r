module test.codegen.library_slice_heap;

import std.slice;
import std.heap;

i32 main() {
    try {
        i32[6] fixed = {5, 1, 4, 2, 6, 3};
        i32[] mutable = &fixed;
        std.slice::sort(mutable);
        const i32[] view = &fixed;
        bool ordered = std.slice::is_sorted(view);
        if (ordered == false) { return 1; }
        if (fixed[0] != 1) { return 2; }
        if (fixed[5] != 6) { return 3; }
        i32 four = 4;
        o<usize> where = std.slice::binary_search(view, &four);
        switch (where) {
        case variant o::some(index):
            if (*index != 3usize) { return 4; }
            break;
        case variant o::none:
            return 5;
        }
        bool has = std.slice::contains(view, &four);
        if (has == false) { return 6; }
        o<const i32*> biggest = std.slice::max_of(view);
        switch (biggest) {
        case variant o::some(b):
            if (**b != 6) { return 7; }
            break;
        case variant o::none:
            return 8;
        }
        i32[] again = &fixed;
        std.slice::reverse(again);
        if (fixed[0] != 6) { return 9; }
        bool still = std.slice::contains(&fixed, &four);
        if (still == false) { return 15; }
        std.slice::sort(&fixed);
        if (fixed[0] != 1) { return 16; }

        std.heap::heap<i32> h = std.heap::heap<i32>::create();
        h.push(3);
        h.push(9);
        h.push(1);
        h.push(7);
        usize n = h.count();
        if (n != 4usize) { return 10; }
        o<i32> top = h.pop();
        switch (top) {
        case variant o::some(t):
            if (*t != 9) { return 11; }
            break;
        case variant o::none:
            return 12;
        }
        o<i32> second = h.pop();
        switch (second) {
        case variant o::some(s):
            if (*s != 7) { return 13; }
            break;
        case variant o::none:
            return 14;
        }
    } catch (std.array::push_error<i32> failure) {
        failure as void;
        return 20;
    }
    return 0;
}
