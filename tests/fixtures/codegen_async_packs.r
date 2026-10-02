module test.codegen.async_packs;

import std.string;

/* R-TYPE-0053 (L18.2-L18.4): async functions with packs; spreads of tuples and packs are staged
   Move operands of their start (R-FUNC-0010). */
i32 drop_count(bool bump) {
    static i32 drops = 0;
    unsafe {
        if (bump == true) { drops += 1; }
        return drops;
    }
}

struct Token { i32 id; };
drop(Token* self) { drop_count(true) as void; }

@generic<T...: send & unborrowed>
async usize count(T... values) {
    move values as void;
    return len(T...);
}

@generic<T...: send & unborrowed>
async usize prepend(T... values) throws std.async::start_error {
    return await count(Token {.id = 0}, ...move values);
}

@generic<H: send & unborrowed, T...: send & unborrowed>
async usize depth(H head, T... tail) throws std.async::start_error {
    move head as void;
    @if (len(T...) != 0usize) {
        usize rest = await depth(...move tail);
        return rest + 1usize;
    }
    move tail as void;
    return 1usize;
}

@generic<F: async fn once(P...) -> R, R: send & unborrowed, P...: send & unborrowed>
async R invoke(F operation, P... args) throws std.async::start_error {
    return await (move operation).call(...move args);
}

async usize measure(std.string::string text, Token token, i32 extra) {
    usize n = std.string::len(&text);
    move text as void;
    move token as void;
    return n + extra as usize;
}

async i32 main() {
    try {
        try {
            usize counted = await count(1, std.string::from_str("ab"), Token {.id = 1});
            if (counted != 3usize) { return 1; }
            if (drop_count(false) != 1) { return 2; }

            usize prepended = await prepend(Token {.id = 2}, true);
            if (prepended != 3usize) { return 3; }
            if (drop_count(false) != 3) { return 4; }

            usize levels = await depth(Token {.id = 3}, 2u8, std.string::from_str("x"));
            if (levels != 3usize) { return 5; }
            if (drop_count(false) != 4) { return 6; }

            auto measured = measure;
            usize length = await invoke(measured, std.string::from_str("abcd"), Token {.id = 4}, 1);
            if (length != 5usize) { return 7; }
            if (drop_count(false) != 5) { return 8; }

            (std.string::string, Token, i32) args =
                (std.string::from_str("xyz"), Token {.id = 5}, 2);
            usize spread = await measure(...move args);
            if (spread != 5usize) { return 9; }
            if (drop_count(false) != 6) { return 10; }
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
