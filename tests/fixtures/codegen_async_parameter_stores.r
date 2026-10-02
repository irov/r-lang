module test.codegen.async_parameter_stores;

/* R-BORROW-0019 (L15.2): an async frame applies store contracts between its awaits. Views end
   before each await (R-BORROW-0024), so the frame stores after its awaits. */
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
};

void feed(Parser* this, str chunk) throws std.array::push_error<str> {
    std.array::push(&this->tokens, chunk);
}

protected async i32 tick(i32 value) {
    return value + 1;
}

protected async i32 work() throws std.async::start_error {
    i32 one = await tick(0);
    i32 two = await tick(one);
    if (two != 2) { return 6; }
    u8[7] line = {97u8, 98u8, 32u8, 99u8, 100u8, 32u8, 101u8};
    const u8[] text = &line;
    array<const u8[]> parts = std.array::create::<const u8[]>();
    try {
        split_into(text, &parts);
    } catch (std.array::push_error<const u8[]> failure) {
        failure as void;
        return 91;
    }
    usize count = len(parts);
    if (count != 3usize) { return 1; }
    Parser p = Parser {.tokens = std.array::create::<str>()};
    try {
        feed(&p, "one");
        feed(&p, "three");
    } catch (std.array::push_error<str> failure) {
        failure as void;
        return 92;
    }
    usize total = len(p.tokens[0usize]) + len(p.tokens[1usize]);
    if (total != 8usize) { return 2; }
    return 0;
}

async i32 main() {
    try {
        return await work();
    } catch (std.async::start_error failure) {
        failure as void;
        return 90;
    }
}
