module test.codegen.library_sorted_text;

import std.sorted;
import std.text;

i32 main() {
    try {
        try {
            std.sorted::set<i32> s = std.sorted::set<i32>::create();
            bool a = s.insert(5);
            bool b = s.insert(1);
            b as void;
            bool c = s.insert(3);
            c as void;
            bool d = s.insert(3);
            if (a == false) { throw TestAssertionFailed {.code = 1}; }
            if (d == true) { throw TestAssertionFailed {.code = 2}; }
            usize n = s.count();
            if (n != 3usize) { throw TestAssertionFailed {.code = 3}; }
            const i32[] keys = s.as_slice();
            if (keys[0] != 1) { throw TestAssertionFailed {.code = 4}; }
            if (keys[2] != 5) { throw TestAssertionFailed {.code = 5}; }
            i32 three = 3;
            bool has = s.contains(&three);
            if (has == false) { throw TestAssertionFailed {.code = 6}; }
            bool gone = s.remove(&three);
            if (gone == false) { throw TestAssertionFailed {.code = 7}; }

            std.sorted::map<i32, i32> m = std.sorted::map<i32, i32>::create();
            o<i32> none = m.insert(2, 20);
            none as void;
            o<i32> again = m.insert(1, 10);
            again as void;
            o<i32> replaced = m.insert(2, 25);
            switch (replaced) {
            case variant o::some(old):
                if (*old != 20) { throw TestAssertionFailed {.code = 8}; }
                break;
            case variant o::none:
                throw TestAssertionFailed {.code = 9};
            }
            i32 two = 2;
            o<const i32*> value = m.get(&two);
            switch (value) {
            case variant o::some(v):
                if (**v != 25) { throw TestAssertionFailed {.code = 10}; }
                break;
            case variant o::none:
                throw TestAssertionFailed {.code = 11};
            }

            bool prefix = std.text::starts_with("hello world", "hello");
            if (prefix == false) { throw TestAssertionFailed {.code = 12}; }
            bool suffix = std.text::ends_with("hello world", "world");
            if (suffix == false) { throw TestAssertionFailed {.code = 13}; }
            o<usize> at = std.text::find("hello world", "o w");
            switch (at) {
            case variant o::some(i):
                if (*i != 4usize) { throw TestAssertionFailed {.code = 14}; }
                break;
            case variant o::none:
                throw TestAssertionFailed {.code = 15};
            }
            bool same = std.text::equal_ignore_ascii_case("MiXed", "mixed");
            if (same == false) { throw TestAssertionFailed {.code = 16}; }
            usize spaces = std.text::count_byte("a b c", 32u8);
            if (spaces != 2usize) { throw TestAssertionFailed {.code = 17}; }
        } catch (std.array::push_error<i32> failure) {
            failure as void;
            throw TestAssertionFailed {.code = 20};
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
