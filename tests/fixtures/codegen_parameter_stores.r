module test.codegen.parameter_stores;

/* R-BORROW-0019 (L15.2): a function that stores views of one parameter into the storage another
   view parameter designates records it in its store contract; the caller's storage then holds
   those views, also through the caller's own parameters and through the exclusive views that
   storage holds. */
void split_into(const u8[] text, array<const u8[]>* parts)
    throws std.array::push_error<const u8[]> {
    usize start = 0usize;
    for (usize i = 0usize; i < len(text); i += 1usize) {
        if (text[i] == 32u8) {
            if (i > start) { std.array::push(parts, text[start..i]); }
            start = i + 1usize;
        }
    }
    if (len(text) > start) { std.array::push(parts, text[start..len(text)]); }
}

struct Parser {
    array<str> tokens;
    usize fed;
};

void feed(Parser* this, str chunk) throws std.array::push_error<str> {
    std.array::push(&this->tokens, chunk);
    this->fed += 1usize;
}

/* The store of `feed` reaches `this`, so this contract stores `a` and `b` into it too. */
void feed_twice(Parser* this, str a, str b) throws std.array::push_error<str> {
    feed(this, a);
    feed(this, b);
}

@generic<T: copy>
struct Stack {
    array<T> items;
};

@generic<T: copy>
void push(Stack<T>* this, T value) throws std.array::push_error<T> {
    std.array::push(&this->items, value);
}

(str)* first_slot(array<(str)*>* slots) {
    return (*slots)[0];
}

/* The slot is a variable of the caller that an exclusive view in `slots` designates. */
void put_first(array<(str)*>* slots, str text) {
    *first_slot(slots) = text;
}

i32 main() {
    u8[17] line = {97u8, 108u8, 112u8, 104u8, 97u8, 32u8, 98u8, 101u8, 116u8, 97u8, 32u8, 32u8,
                   103u8, 97u8, 109u8, 109u8, 97u8};
    const u8[] text = &line;
    try {
        try {
            array<const u8[]> parts = std.array::create::<const u8[]>();
            split_into(text, &parts);
            if (len(parts) != 3usize) { throw TestAssertionFailed {.code = 1}; }
            if ((len(parts[0usize]) != 5usize) || (len(parts[1usize]) != 4usize) ||
                (len(parts[2usize]) != 5usize)) {
                throw TestAssertionFailed {.code = 2};
            }
            if (parts[2usize][0usize] != 103u8) { throw TestAssertionFailed {.code = 3}; }

            Parser p = Parser {.tokens = std.array::create::<str>(), .fed = 0usize};
            feed(&p, "one");
            feed_twice(&p, "two", "three");
            if ((len(p.tokens) != 3usize) || (p.fed != 3usize)) {
                throw TestAssertionFailed {.code = 4};
            }
            if (len(p.tokens[2usize]) != 5usize) { throw TestAssertionFailed {.code = 5}; }

            Stack<str> s = Stack<str> {.items = std.array::create::<str>()};
            push::<str>(&s, "top");
            if (len(s.items[0usize]) != 3usize) { throw TestAssertionFailed {.code = 6}; }

            str x = "a";
            array<(str)*> slots = std.array::create::<(str)*>();
            try {
                std.array::push(&slots, &x);
            } catch (std.array::push_error<(str)*> failure) {
                move failure as void;
                throw TestAssertionFailed {.code = 7};
            }
            put_first(&slots, "hello");
            move slots as void;
            if (len(x) != 5usize) { throw TestAssertionFailed {.code = 8}; }
        } catch (std.array::push_error<const u8[]> failure) {
            failure as void;
            throw TestAssertionFailed {.code = 20};
        } catch (std.array::push_error<str> failure) {
            failure as void;
            throw TestAssertionFailed {.code = 21};
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
