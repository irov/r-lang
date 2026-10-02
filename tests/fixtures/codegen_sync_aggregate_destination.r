module test.codegen.sync_aggregate_destination;

struct payload {
    u8[65_536] bytes;
    usize length;
};

error failure {
    i32 code;
};

struct text_holder {
    constexpr str text;
};

protected payload make_payload(bool fail) throws failure {
    payload value = {};
    if (fail == true) {
        throw {
            .code = 7,
        };
    }
    value.bytes[0] = 42;
    value.length = 1;
    return value;
}

/* Exported, so its body stays in the program although the call below is replaced by its
   translation-time value (R-EXPR-0032). */
usize empty_text_length() {
    text_holder value = {};
    usize length = len(value.text);
    return length;
}

i32 main() {
    usize text_length = empty_text_length();
    if (text_length != 0) {
        return 1;
    }
    try {
        payload value = make_payload(false);
        if (value.length != 1) {
            return 2;
        }
        if (value.bytes[0] != 42) {
            return 3;
        }
        return 0;
    } catch (failure error) {
        return error.code;
    }
}
