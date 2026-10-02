module test.codegen.tuples;

import std.string;

/* R-TYPE-0052 (L18.1): tuple types, literals, elements, patterns and cleanup. */
i32 drop_count(bool bump) {
    static i32 count = 0;
    unsafe {
        if (bump == true) { count += 1; }
        return count;
    }
}

struct Token { i32 id; };
drop(Token* self) { drop_count(true) as void; }

struct Holder { i32 first; (i32, (u8, bool)) inner; i64 last; };
error Failed { (i32, i32) where; };
enum Shape { circle((i32, i32)), square };

(i32, bool) divide(i32 value, i32 by) {
    if (by == 0) { return (0, false); }
    return (value / by, true);
}

@generic<A: copy, B: copy>
(B, A) swap((A, B) pair) { return (pair.1, pair.0); }

i32 add3(i32 a, i32 b, i32 c) { return a + b + c; }

usize measure(std.string::string text, Token token, i32 extra) {
    usize n = std.string::len(&text);
    move text as void;
    move token as void;
    return n + extra as usize;
}

i32 consume(Token token, i32 value) {
    i32 id = token.id;
    move token as void;
    return id + value;
}

i32 take_second((Token, i32) pair) {
    return match (move pair) {
        case {.0 = move token, .1 = value}: consume(move token, value);
    };
}

i32 main() {
    try {
        (i32, bool) quotient = divide(7, 2);
        if (quotient.0 != 3 || quotient.1 != true) { return 1; }
        (i32, bool) failed = divide(1, 0);
        if (failed.1 != false) { return 2; }

        (u8, i64) widened = (1, 2);
        widened.1 = 40i64;
        if (widened.0 != 1u8 || widened.1 != 40i64) { return 3; }

        (bool, u8) swapped = swap((3u8, true));
        if (swapped.0 != true || swapped.1 != 3u8) { return 4; }

        Holder holder = {.first = 5, .inner = (1, (2u8, true)), .last = 9i64};
        if (holder.inner.1.1 != true || holder.inner.1.0 != 2u8 || holder.last != 9i64) {
            return 5;
        }
        Shape shape = Shape::circle((6, 7));
        i32 sum = match (shape) {
            case variant Shape::circle(c): c.0 + c.1;
            default: 0;
        };
        if (sum != 13) { return 6; }
        try {
            throw Failed {.where = (8, 9)};
        } catch (Failed failure) {
            if (failure.where.1 != 9) { return 7; }
        }

        (i32, i32) pair = (2, 3);
        if (add3(1, ...pair) != 6) { return 8; }

        {
            (Token, std.string::string) owned = (Token {.id = 1}, std.string::from_str("ab"));
            if (owned.0.id != 1 || std.string::len(&owned.1) != 2usize) { return 9; }
            (Token, std.string::string) moved = move owned;
            move moved as void;
        }
        if (drop_count(false) != 1) { return 10; }

        (std.string::string, Token, i32) args = (std.string::from_str("xyz"), Token {.id = 2}, 4);
        if (measure(...move args) != 7usize) { return 11; }
        if (drop_count(false) != 2) { return 12; }

        if (take_second((Token {.id = 5}, 6)) != 11) { return 13; }
        if (drop_count(false) != 3) { return 14; }
        return 0;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 90;
    }
}
