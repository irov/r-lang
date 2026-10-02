module test.codegen.async_tuples;

import std.string;

/* R-TYPE-0052 (L18.1): tuples held across await, passed to and returned from async functions,
   spread into an async start and destructured. */
i32 drop_count(bool bump) {
    static i32 count = 0;
    unsafe {
        if (bump == true) { count += 1; }
        return count;
    }
}

struct Token { i32 id; };
drop(Token* self) { drop_count(true) as void; }

async (i32, bool) divide(i32 value, i32 by) {
    if (by == 0) { return (0, false); }
    return (value / by, true);
}

async usize measure(std.string::string text, Token token, i32 extra) {
    usize n = std.string::len(&text);
    move text as void;
    move token as void;
    return n + extra as usize;
}

async i32 consume(Token token, i32 value) {
    i32 id = token.id;
    move token as void;
    return id + value;
}

async i32 main() {
    try {
        try {
            (i32, bool) quotient = await divide(7, 2);
            bool quotient_valid = quotient.0 == 3 && quotient.1 == true;
            (Token, std.string::string) held = (Token {.id = 1}, std.string::from_str("ab"));
            (i32, bool) failed = await divide(1, 0);
            bool failed_valid = failed.1 == false;
            if (quotient_valid == false || failed_valid == false) { return 1; }
            if (held.0.id != 1 || std.string::len(&held.1) != 2usize) { return 2; }
            move held as void;
            if (drop_count(false) != 1) { return 3; }

            (std.string::string, Token, i32) args =
                (std.string::from_str("xyz"), Token {.id = 2}, 4);
            usize measured = await measure(...move args);
            if (measured != 7usize) { return 4; }
            if (drop_count(false) != 2) { return 4; }

            (Token, i32) pair = (Token {.id = 5}, 6);
            i32 total = match (move pair) {
                case {.0 = move token, .1 = value}: await consume(move token, value);
            };
            if (total != 11) { return 5; }
            if (drop_count(false) != 3) { return 5; }
            return 0;
        } catch (std.async::start_error failure) {
            failure as void;
            return 80;
        }
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 90;
    }
}
