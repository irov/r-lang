module test.codegen.field_access;

/* R-STMT-0023, R-REFL-0006 (L44): a translation-time loop repeats its block with a constant index,
   and core::field and core::field_mut borrow the field of that index, each repetition checked
   with the type of its own field. */

struct Settings {
    u32 threshold;
    u64 retries;
    bool verbose;
};

struct Pair {
    u32 left;
    u32 right;
};

usize width(const u8* value) { return 1usize; }
usize width(const u16* value) { return 2usize; }
usize width(const u32* value) { return 4usize; }
usize width(const u64* value) { return 8usize; }
usize width(const bool* value) { return 1usize; }

void bump(u32* value) { *value += 1u32; }
void bump(u64* value) { *value += 2u64; }
void bump(bool* value) { *value = true; }

/* Through a pointer, as `settings->field`. */
usize total_width(const Settings* settings) {
    usize total = 0usize;
    for (constexpr usize index in 0usize..core::field_count::<Settings>()) {
        total += width(core::field(settings, index));
    }
    return total;
}

void bump_all(Settings* settings) {
    for (constexpr usize index in 0usize..core::field_count::<Settings>()) {
        bump(core::field_mut(settings, index));
    }
}

/* The loop constant decides constant conditions and names the fields. */
usize named_letters() {
    usize letters = 0usize;
    for (constexpr usize index in 0usize..core::field_count::<Settings>()) {
        @if (index != 1usize) {
            str name = core::field_name::<Settings>(index);
            letters += len(name);
        }
    }
    return letters;
}

/* A return inside a repetition leaves the function, and a signed range counts up from below. */
i32 first_large() {
    for (constexpr i32 value in -2..5) {
        @if (value * value > 8) { return value; }
    }
    return 0;
}

/* Defects L44-14, L44-16, L44-17: a bound or field index is any constant expression; a lambda in
   a repetition captures the loop constant; a labeled jump leaves the repetitions for its loop. */
usize three() { return 3usize; }

usize constant_forms() {
    const usize limit = 2usize;
    usize total = 0usize;
    for (constexpr usize index in 0usize..sizeof(u32)) { total += index; }
    for (constexpr usize index in 0usize..limit) { total += 10usize; }
    for (constexpr usize index in 1usize..three()) { total += 100usize * index; }
    Pair pair = {.left = 1000u32, .right = 2000u32};
    const u32* right = core::field(&pair, limit - 1usize);
    const u32* left = core::field(&pair, three() - 3usize);
    total += (*right + *left) as usize;
    for (constexpr usize index in 0usize..2usize) {
        fn usize scaled() { return index * 10000usize; }
        total += scaled();
    }
    outer: while (true) {
        for (constexpr usize index in 0usize..3usize) {
            total += 100000usize;
            @if (index == 1usize) { break outer; }
        }
    }
    return total;
}

i32 run() {
    Settings settings = {.threshold = 1u32, .retries = 2u64, .verbose = false};
    if (total_width(&settings) != 13usize) { return 1; }
    bump_all(&settings);
    if (settings.threshold != 2u32 || settings.retries != 4u64 || settings.verbose == false) { return 2; }
    if (named_letters() != 16usize) { return 3; }
    if (first_large() != 3) { return 4; }

    /* `&place` borrows the field of the place, so borrows of two fields are independent. */
    Pair pair = {.left = 1u32, .right = 2u32};
    const u32* left = core::field(&pair, 0usize);
    u32* right = core::field_mut(&pair, 1usize);
    *right += *left;
    if (pair.right != 3u32) { return 5; }

    /* Nested loops, an empty range and an ordinary loop inside a repetition. */
    usize sum = 0usize;
    for (constexpr usize outer in 0usize..2usize) {
        for (constexpr usize inner in 0usize..2usize) {
            sum += outer * 10usize + inner;
        }
        while (true) { break; }
    }
    for (constexpr usize skipped in 3usize..3usize) {
        return 6;
    }
    if (sum != 22usize) { return 7; }

    /* Elements of a tuple. */
    (u8, u16) small = (1u8, 2u16);
    usize tuple_width = 0usize;
    for (constexpr usize element in 0usize..2usize) {
        tuple_width += width(core::field(&small, element));
    }
    if (tuple_width != 3usize) { return 8; }

    /* A loop constant names nothing outside its block: the module scope keeps `index` free. */
    usize index = 2usize;
    if (index + sum != 24usize) { return 9; }
    if (constant_forms() != 213326usize) { return 10; }
    return 0;
}

i32 main() { return run(); }
