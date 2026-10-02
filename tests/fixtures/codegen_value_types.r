module test.codegen.value_types;

error issue {
    i32 code;
};

enum scalar_state {
    idle,
    ready,
};

struct scalar_leaf {
    bool bool_value;
    i8 i8_value;
    i16 i16_value;
    i32 i32_value;
    i64 i64_value;
    isize isize_value;
    u8 u8_value;
    u16 u16_value;
    u32 u32_value;
    u64 u64_value;
    usize usize_value;
    f32 f32_value;
    f64 f64_value;
    char char_value;
    scalar_state state;
    u16[2] words;
};

struct scalar_tree {
    scalar_leaf leaf;
    o<i64> optional;
    o<u16> outcome;
};

struct scalar_defaults {
    bool bool_value;
    i8 i8_value;
    i16 i16_value;
    i32 i32_value;
    i64 i64_value;
    isize isize_value;
    u8 u8_value;
    u16 u16_value;
    u32 u32_value;
    u64 u64_value;
    usize usize_value;
    f32 f32_value;
    f64 f64_value;
    char char_value;
    u16[2] words;
};

struct scalar_integers {
    i8 i8_value;
    i16 i16_value;
    i32 i32_value;
    i64 i64_value;
    isize isize_value;
    u8 u8_value;
    u16 u16_value;
    u32 u32_value;
    u64 u64_value;
    usize usize_value;
};

struct character_literal_set {
    char ascii;
    char unicode_bmp;
    char unicode_supplementary;
    char escaped_backslash;
    char escaped_double_quote;
    char escaped_single_quote;
    char escaped_newline;
    char escaped_carriage_return;
    char escaped_tab;
    char escaped_nul;
    char escaped_hex;
    char escaped_unicode;
    char maximum_scalar;
};

bool echo_bool(bool input) {
    bool value = input;
    return value;
}

i8 echo_i8(i8 input) {
    i8 value = input;
    return value;
}

i16 echo_i16(i16 input) {
    i16 value = input;
    return value;
}

i32 echo_i32(i32 input) {
    i32 value = input;
    return value;
}

i64 echo_i64(i64 input) {
    i64 value = input;
    return value;
}

isize echo_isize(isize input) {
    isize value = input;
    return value;
}

u8 echo_u8(u8 input) {
    u8 value = input;
    return value;
}

u16 echo_u16(u16 input) {
    u16 value = input;
    return value;
}

u32 echo_u32(u32 input) {
    u32 value = input;
    return value;
}

u64 echo_u64(u64 input) {
    u64 value = input;
    return value;
}

usize echo_usize(usize input) {
    usize value = input;
    return value;
}

f32 echo_f32(f32 input) {
    f32 value = input;
    return value;
}

f64 echo_f64(f64 input) {
    f64 value = input;
    return value;
}

char echo_char(char input) {
    char value = input;
    return value;
}

protected character_literal_set character_literals() {
    character_literal_set value = {
        .ascii = 'A',
        .unicode_bmp = 'é',
        .unicode_supplementary = '🙂',
        .escaped_backslash = '\\',
        .escaped_double_quote = '\"',
        .escaped_single_quote = '\'',
        .escaped_newline = '\n',
        .escaped_carriage_return = '\r',
        .escaped_tab = '\t',
        .escaped_nul = '\0',
        .escaped_hex = '\x7f',
        .escaped_unicode = '\u{1f680}',
        .maximum_scalar = '\u{10ffff}',
    };
    return value;
}

scalar_tree echo_scalar_tree(scalar_tree input) {
    scalar_tree value = input;
    return value;
}

o<i64> echo_scalar_option(o<i64> input) {
    o<i64> value = input;
    return value;
}

o<u16> echo_scalar_result(o<u16> input) {
    o<u16> value = input;
    return value;
}

scalar_defaults default_scalar_values() {
    scalar_defaults value = {};
    return value;
}

scalar_integers contextual_scalar_literals() {
    scalar_integers value = {
        .i8_value = -128,
        .i16_value = -32768,
        .i32_value = -2147483648,
        .i64_value = -9223372036854775808,
        .isize_value = -9223372036854775808,
        .u8_value = 255,
        .u16_value = 65535,
        .u32_value = 4294967295,
        .u64_value = 18446744073709551615,
        .usize_value = 18446744073709551615,
    };
    return value;
}

scalar_integers suffixed_scalar_literals() {
    scalar_integers value = {
        .i8_value = -128i8,
        .i16_value = -32768i16,
        .i32_value = -2147483648i32,
        .i64_value = -9223372036854775808i64,
        .isize_value = -9223372036854775808isize,
        .u8_value = 255u8,
        .u16_value = 65535u16,
        .u32_value = 4294967295u32,
        .u64_value = 18446744073709551615u64,
        .usize_value = 18446744073709551615usize,
    };
    return value;
}

protected o<i32> maybe_value(bool present) {
    if (present == true) {
        o<i32> value = o::some(7);
        return value;
    }
    o<i32> absent = o::none;
    return absent;
}

protected o<i32> propagate_option(bool present) {
    o<i32> candidate = maybe_value(present);
    switch (candidate) {
        case variant o::some(value):
            o<i32> success = o::some(*value);
            return success;
        case variant o::none:
            o<i32> absent = o::none;
            return absent;
    }
}

protected i32 checked_value(bool fail) throws issue {
    if (fail == true) {
        throw {
            .code = 9,
        };
    }
    return 4;
}

protected i32 named_error() throws issue {
    issue error = {
        .code = 12,
    };
    throw error;
}

protected void void_status(bool fail) throws issue {
    if (fail == true) {
        throw {
            .code = 13,
        };
    }
}

protected i32 propagate_result(bool fail) throws issue {
    void_status(fail);
    i32 value = checked_value(fail);
    return value;
}

protected i32 read_at(const i32[] values, u32 index) {
    return values[index];
}

i32 main() {
    try {
        character_literal_set characters = character_literals();
        characters as void;
        i32[3] values = {1, 2};
        i32[] mutable_values = &values;
        mutable_values[1] += 3;
        i32 first = values[0];

        const i32[] view = &values;
        i32 selected = read_at(view, 1);
        selected as void;
        o<i32> option_result = propagate_option(selected == 5);
        i32 option_value = 0;

        switch (option_result) {
            case variant o::some(value):
                option_value = *value;
                break;
            case variant o::none:
                option_value = -1;
                break;
        }

        try {
            // A run-time argument keeps the checked calls at run time (R-EXPR-0032).
            i32 explicit_ok = checked_value(selected != 5);
            if (explicit_ok != 4) {
                throw TestAssertionFailed {.code = 2};
            }
            i32 value = propagate_result(selected != 5);
            if (selected == 5) {
                if (first == 1) {
                    if (option_value == 7) {
                        return value - 4;
                    }
                }
            }
            throw TestAssertionFailed {.code = 1};
        } catch (issue error) {
            return error.code;
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
