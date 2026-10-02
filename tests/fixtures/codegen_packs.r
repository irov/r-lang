module test.codegen.packs;

import std.string;

/* R-TYPE-0053 (L18.2-L18.4): type packs, spreads, recursion over a pack and callable packs. */
i32 drop_count(bool bump) {
    static i32 drops = 0;
    unsafe {
        if (bump == true) { drops += 1; }
        return drops;
    }
}

struct Token { i32 id; };
drop(Token* self) { drop_count(true) as void; }

trait Shown { i32 show(const Self* this); };
impl Shown for i32 { i32 show(const i32* this) { return *this; } };
impl Shown for bool {
    i32 show(const bool* this) {
        if (*this == true) { return 1; }
        return 0;
    }
};
impl Shown for Token { i32 show(const Token* this) { return this->id * 100; } };

@generic<T...>
usize count(T... values) {
    move values as void;
    return len(T...);
}

@generic<T...>
usize forward(T... values) { return count(...move values); }

@generic<T...>
usize prepend(T... values) { return count(1u8, ...move values); }

/* Recursion instantiates one function per length; the branch stops it. */
@generic<Head: Shown, Tail...: Shown>
i32 show_rest(Head head, Tail... tail) {
    i32 first = head.show();
    @if (len(Tail...) != 0usize) {
        return first + show_rest(...move tail);
    }
    move tail as void;
    move head as void;
    return first;
}

@generic<Head: Shown, Tail...: Shown>
i32 show_all(const Head* head, Tail... tail) {
    i32 first = head->show();
    @if (len(Tail...) != 0usize) {
        return first + show_rest(...move tail);
    }
    move tail as void;
    return first;
}

@generic<A, B>
usize pair(A a, B b) {
    move a as void;
    move b as void;
    return 2usize;
}

@generic<T...>
usize exact(T... values) {
    @if (len(T...) == 2usize) {
        return pair(...move values);
    }
    move values as void;
    return 0usize;
}

@generic<T...>
(i32, T...) tagged(T... values) { return (7, ...move values); }

@generic<T...: copy>
usize shape_of((i32, T...) shaped) {
    shaped as void;
    return len(T...);
}

@generic<F: fn(P...) -> R, R, P...>
R apply(const F* f, P... args) { return f(...move args); }

@generic<F: fn(i32, P...) -> R, R, P...>
R with_first(const F* f, P... args) { return f(10, ...move args); }

@generic<F: fn mut(P...) -> R, R, P...>
R apply_mut(F* f, P... args) { return f->call(...move args); }

@generic<F: fn once(P...) -> R, R, P...>
R apply_once(F f, P... args) { return (move f).call(...move args); }

usize overloaded(i32 value) {
    value as void;
    return 100usize;
}

@generic<T...>
usize overloaded(T... values) {
    move values as void;
    return len(T...);
}

usize measure(std.string::string text, Token token, bool twice) {
    usize n = std.string::len(&text);
    move text as void;
    move token as void;
    if (twice == true) { return 2usize * n; }
    return n;
}

i32 add3(i32 a, i32 b, i32 c) { return a + b + c; }

i32 main() {
    try {
        if (count(1, true, 'c') != 3usize || count() != 0usize) { return 1; }
        if (forward(1, 2, 3) != 3usize || prepend('x', true) != 3usize) { return 2; }

        Token viewed = {.id = 3};
        i32 shown = show_all(&viewed, 5, true, Token {.id = 1});
        if (shown != 300 + 5 + 1 + 100) { return 3; }
        move viewed as void;
        if (drop_count(false) != 2) { return 4; }

        if (exact(1, true) != 2usize || exact(1) != 0usize) { return 5; }
        (i32, bool, u8) made = tagged(true, 3u8);
        if (made.0 != 7 || made.2 != 3u8) { return 6; }
        if (shape_of(made) != 2usize || shape_of((1, 'c')) != 1usize) { return 7; }

        auto measured = measure;
        usize length = apply(&measured, std.string::from_str("abc"), Token {.id = 9}, true);
        if (length != 6usize) { return 8; }
        if (drop_count(false) != 3) { return 9; }
        auto summed = add3;
        if (with_first(&summed, 20, 30) != 60) { return 10; }
        i32 total = 0;
        fn mut i32 counter(i32 step, i32 times) { total += step * times; return total; }
        if (apply_mut(&counter, 2, 3) != 6) { return 11; }
        Token kept = {.id = 4};
        fn once i32 consume(bool keep) move(kept) {
            if (keep == true) { return kept.id; }
            return 0;
        }
        if (apply_once(move consume, true) != 4) { return 12; }
        if (drop_count(false) != 4) { return 13; }

        if (count::<u8, bool>(1, true) != 2usize) { return 14; }
        if (overloaded(1, 2) != 2usize || overloaded() != 0usize) { return 15; }

        (Token, i32) tail = (Token {.id = 6}, 2);
        (bool, Token, i32) grown = (true, ...move tail);
        if (grown.1.id != 6) { return 16; }
        move grown as void;
        if (drop_count(false) != 5) { return 17; }
        return 0;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 90;
    }
}
