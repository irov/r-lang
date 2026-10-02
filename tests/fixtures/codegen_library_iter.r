module test.codegen.library_iter;

import std.iter;

i32 main() {
    try {
        try {
            i32[5] fixed = {1, 2, 3, 4, 5};
            const i32[] view = &fixed;
            fn o<i32> keep_odd(const i32* x) { if (*x % 2 == 1) { return o::some(*x); } return o::none; }
            auto source = std.iter::of_slice(view);
            auto odds = std.iter::filter_map(move source, &keep_odd);
            fn i32 add(i32 acc, i32 x) { return acc + x; }
            i32 odd_sum = std.iter::fold(move odds, 0, &add);
            if (odd_sum != 9) { throw TestAssertionFailed {.code = 1}; }

            fn i32 twice(i32 x) { return x * 2; }
            std.iter::range_i32 numbers = std.iter::range(0, 4);
            auto doubled = std.iter::map(move numbers, &twice);
            array<i32> collected = std.iter::collect_array(move doubled);
            if (len(collected) != 4usize) { throw TestAssertionFailed {.code = 2}; }
            o<const i32*> slot = std.array::get(&collected, 3usize);
            switch (slot) {
            case variant o::some(v):
                if (**v != 6) { throw TestAssertionFailed {.code = 3}; }
                break;
            case variant o::none:
                throw TestAssertionFailed {.code = 3};
            }

            std.iter::range_i32 again = std.iter::range(0, 10);
            auto middle = std.iter::skip(move again, 2usize);
            auto few = std.iter::take(move middle, 3usize);
            usize n = std.iter::count(move few);
            if (n != 3usize) { throw TestAssertionFailed {.code = 4}; }

            fn bool big(i32 x) { return x > 7; }
            std.iter::range_i32 third = std.iter::range(0, 10);
            bool has_big = std.iter::any(move third, &big);
            if (has_big == false) { throw TestAssertionFailed {.code = 5}; }
            std.iter::range_i32 fourth = std.iter::range(0, 10);
            o<usize> where = std.iter::position(move fourth, &big);
            switch (where) {
            case variant o::some(index):
                if (*index != 8usize) { throw TestAssertionFailed {.code = 6}; }
                break;
            case variant o::none:
                throw TestAssertionFailed {.code = 7};
            }

            std.iter::range_i32 left = std.iter::range(1, 4);
            std.iter::range_i32 right = std.iter::range(10, 13);
            auto pairs = std.iter::zip(move left, move right);
            i32 pair_sum = 0;
            for (std.iter::pair<i32, i32> p in &pairs) { pair_sum += p.left * p.right; }
            if (pair_sum != 68) { throw TestAssertionFailed {.code = 8}; }

            std.iter::range_i32 fifth = std.iter::range(5, 8);
            auto numbered = std.iter::enumerate(move fifth);
            usize weighted = 0usize;
            for (std.iter::indexed<i32> e in &numbered) { weighted += e.index * (e.value as usize); }
            if (weighted != 20usize) { throw TestAssertionFailed {.code = 9}; }

            std.iter::range_i32 a = std.iter::range(0, 2);
            std.iter::range_i32 b = std.iter::range(2, 4);
            auto joined = std.iter::chain(move a, move b);
            o<i32> final = std.iter::last(move joined);
            switch (final) {
            case variant o::some(v):
                if (*v != 3) { throw TestAssertionFailed {.code = 10}; }
                break;
            case variant o::none:
                throw TestAssertionFailed {.code = 11};
            }
        } catch (std.array::push_error<i32> failure) {
            failure as void;
            throw TestAssertionFailed {.code = 20};
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
