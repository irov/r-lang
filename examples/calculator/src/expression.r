module example.calculator.expression;

/* A recursive descent evaluator of f64 arithmetic: a sum of products of factors, where a factor
   is a number, a negated factor or a parenthesized sum. Every parenthesis nests one more call of
   sum, product and factor, so the nesting of the input decides the depth of the recursion.
   @recursion bounds it (Core R-FUNC-0026): the call that would begin activation 17 of one of
   these functions throws core::recursion_error before it runs, and the stack bound of the
   generated C counts 16 frames of each. */

error Syntax { usize offset; };

protected void skip_spaces(const u8[] text, usize* at) {
    while (*at < len(text) && text[*at] == 32u8) {
        *at += 1usize;
    }
}

protected bool is_number_byte(u8 value) {
    return (value >= 48u8 && value <= 57u8) || value == 46u8;
}

// Digits with an optional fraction, parsed by std.convert.
protected f64 number(const u8[] text, usize* at) throws Syntax {
    usize start = *at;
    while (*at < len(text) && is_number_byte(text[*at]) == true) {
        *at += 1usize;
    }
    throw (*at == start) Syntax {.offset = start};
    try {
        str digits = core::validate_utf8(text[start..*at]);
        f64 value = std.convert::parse_f64(digits);
        return value;
    } catch (core::utf8_error failure) {
        throw Syntax {.offset = start};
    } catch (std.convert::parse_error failure) {
        throw Syntax {.offset = start};
    }
}

@recursion(depth = 16)
protected f64 sum(const u8[] text, usize* at) throws Syntax, core::recursion_error;

@recursion(depth = 16)
protected f64 factor(const u8[] text, usize* at) throws Syntax, core::recursion_error {
    skip_spaces(text, at);
    throw (*at >= len(text)) Syntax {.offset = *at};
    if (text[*at] == 45u8) {
        // '-': a negated factor.
        *at += 1usize;
        f64 negated = factor(text, at);
        return -negated;
    }
    if (text[*at] == 40u8) {
        // '(': a nested sum up to its ')'.
        *at += 1usize;
        f64 inner = sum(text, at);
        skip_spaces(text, at);
        throw (*at >= len(text)) Syntax {.offset = *at};
        throw (text[*at] != 41u8) Syntax {.offset = *at};
        *at += 1usize;
        return inner;
    }
    f64 value = number(text, at);
    return value;
}

@recursion(depth = 16)
protected f64 product(const u8[] text, usize* at) throws Syntax, core::recursion_error {
    f64 value = factor(text, at);
    while (true) {
        skip_spaces(text, at);
        if (*at >= len(text)) { return value; }
        u8 symbol = text[*at];
        if (symbol != 42u8 && symbol != 47u8) { return value; }
        *at += 1usize;
        f64 right = factor(text, at);
        if (symbol == 42u8) {
            value = value * right;
        } else {
            value = value / right;
        }
    }
}

@recursion(depth = 16)
protected f64 sum(const u8[] text, usize* at) throws Syntax, core::recursion_error {
    f64 value = product(text, at);
    while (true) {
        skip_spaces(text, at);
        if (*at >= len(text)) { return value; }
        u8 symbol = text[*at];
        if (symbol != 43u8 && symbol != 45u8) { return value; }
        *at += 1usize;
        f64 right = product(text, at);
        if (symbol == 43u8) {
            value = value + right;
        } else {
            value = value - right;
        }
    }
}

// The value of the whole expression; a byte after the top-level sum is a syntax error.
f64 evaluate(str source) throws Syntax, core::recursion_error {
    const u8[] text = source;
    usize at = 0usize;
    f64 value = sum(text, &at);
    skip_spaces(text, &at);
    throw (at != len(text)) Syntax {.offset = at};
    return value;
}
