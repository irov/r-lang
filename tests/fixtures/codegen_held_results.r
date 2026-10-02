module test.codegen.held_results;

/* R-BORROW-0009 (L15.1): a result taken only from what the storage a borrow or slice parameter
   designates holds depends on what the argument holds, not on the argument, so the argument's
   storage is borrowed again, written, moved or destroyed while the result lives. */
struct Decoder {
    const u8[] input;
    usize at;
};

const u8[] take(Decoder* this, usize n) {
    const u8[] part = this->input[this->at..this->at + n];
    this->at += n;
    return part;
}

const u8[] peek(const Decoder* this, usize n) {
    return this->input[this->at..this->at + n];
}

/* A caller parameter passes on what it holds, so the contract stays held. */
const u8[] skip_take(Decoder* this) {
    const u8[] first = take(this, 1usize);
    this->at += 1usize;
    return first;
}

const u8[] Decoder::take_one(Decoder* this) {
    return take(this, 1usize);
}

/* The decoder dies with its frame; what it held is the caller's. */
const u8[] first_of(const u8[] data) {
    Decoder d = Decoder {.input = data, .at = 0usize};
    const u8[] head = take(&d, 1usize);
    return head;
}

struct Named {
    str name;
    i32 id;
};

str name_of(const Named* item) {
    return item->name;
}

u32 sum(const u8[] bytes) {
    u32 total = 0u32;
    for (usize i = 0usize; i < len(bytes); i += 1usize) { total += bytes[i] as u32; }
    return total;
}

i32 main() {
    u8[6] data = {1u8, 2u8, 3u8, 4u8, 5u8, 6u8};
    const u8[] view = &data;
    try {
        try {
            Decoder d = Decoder {.input = view, .at = 0usize};
            const u8[] head = take(&d, 1usize);
            const u8[] body = take(&d, 2usize);
            if ((sum(head) != 1u32) || (sum(body) != 5u32)) { throw TestAssertionFailed {.code = 1}; }

            Decoder e = Decoder {.input = view, .at = 0usize};
            const u8[] one = peek(&e, 2usize);
            e.at += 2usize;
            const u8[] two = peek(&e, 2usize);
            if ((sum(one) != 3u32) || (sum(two) != 7u32)) { throw TestAssertionFailed {.code = 2}; }

            if (sum(first_of(view)) != 1u32) { throw TestAssertionFailed {.code = 3}; }

            Decoder g = Decoder {.input = view, .at = 0usize};
            const u8[] a = take(&g, 1usize);
            Decoder h = move g;
            const u8[] b = take(&h, 1usize);
            if ((sum(a) + sum(b)) != 3u32) { throw TestAssertionFailed {.code = 4}; }

            Decoder k = Decoder {.input = view, .at = 0usize};
            const u8[] s1 = skip_take(&k);
            const u8[] s2 = skip_take(&k);
            if ((sum(s1) != 1u32) || (sum(s2) != 3u32)) { throw TestAssertionFailed {.code = 5}; }

            Decoder m = Decoder {.input = view, .at = 0usize};
            Decoder* cursor = &m;
            const u8[] last = view[0usize..0usize];
            u32 seen = 0u32;
            for (usize i = 0usize; i < 3usize; i += 1usize) {
                seen += sum(last);
                last = cursor->take_one();
            }
            if ((seen != 3u32) || (sum(last) != 3u32)) { throw TestAssertionFailed {.code = 6}; }

            array<Named> items = std.array::create::<Named>();
            items.push(Named {.name = "alpha", .id = 1});
            const Named* first = &items[0usize];
            str name = name_of(first);
            std.array::clear(&items);
            if (len(name) != 5usize) { throw TestAssertionFailed {.code = 7}; }
        } catch (std.array::push_error<Named> failure) {
            failure as void;
            throw TestAssertionFailed {.code = 20};
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
