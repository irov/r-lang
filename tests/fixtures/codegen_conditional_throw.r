module test.codegen.conditional_throw;

enum ZipErrorCode { InvalidDeflate, };
error ZipError {
    ZipErrorCode code;
    i32 offset;
    constexpr str message;
};
error MoveError { own i32* payload; i32 code; };

protected void check_size(i32 written) throws ZipError {
    i32 expected_size = 4;
    throw (written != expected_size) {
        .code = ZipErrorCode::InvalidDeflate,
        .offset = written,
        .message = "decoded size differs from central directory",
    };
}

protected i32 tick(i32* count) {
    *count += 1;
    return *count;
}

protected i32 conditional(bool fail, i32* condition_count,
                          i32* finally_count) throws ZipError {
    i32 divisor = 0;
    if (fail == true) { divisor = 1; }
    try {
        throw ((tick(condition_count) == 1) && (fail == true)) ZipError {
            .code = ZipErrorCode::InvalidDeflate,
            .offset = 1 / divisor,
            .message = "payload",
        };
        return 17;
    } finally {
        *finally_count += 1;
    }
}

protected i32 explicit_if(bool fail, i32* condition_count,
                          i32* finally_count) throws ZipError {
    i32 divisor = 0;
    if (fail == true) { divisor = 1; }
    try {
        if ((tick(condition_count) == 1) && (fail == true)) {
            throw ZipError {
                .code = ZipErrorCode::InvalidDeflate,
                .offset = 1 / divisor,
                .message = "payload",
            };
        }
        return 17;
    } finally {
        *finally_count += 1;
    }
}

protected i32 compare(bool sugar, bool fail) {
    i32 condition_count = 0;
    i32 finally_count = 0;
    i32 result = 0;
    try {
        if (sugar == true) {
            result = conditional(fail, &condition_count, &finally_count);
        } else {
            result = explicit_if(fail, &condition_count, &finally_count);
        }
    } catch (ZipError error) {
        result = error.offset;
    }
    return result + 100 * condition_count + 10000 * finally_count;
}

protected void transfer(bool fail, MoveError error) throws MoveError {
    throw (fail == true) move error;
    if (error.code != 9) {
        throw move error;
    }
}

protected i32 constant_true() throws ZipError {
    throw (true) {
        .code = ZipErrorCode::InvalidDeflate, .offset = 8, .message = "constant",
    };
}

i32 main() {
    try {
        i32 a = compare(true, false);
        i32 b = compare(false, false);
        i32 c = compare(true, true);
        i32 d = compare(false, true);
        if (a != b || a != 10117 || c != d || c != 10101) { throw TestAssertionFailed {.code = 1}; }
        i32 count = 0;
        try {
            check_size(4);
            throw (false && (tick(&count) > 0)) ZipError {
                .code = ZipErrorCode::InvalidDeflate, .offset = 1 / count, .message = "skipped",
            };
            if (count != 0) { throw TestAssertionFailed {.code = 2}; }
            check_size(3);
            throw TestAssertionFailed {.code = 3};
        } catch (ZipError error) {
            if (error.code != ZipErrorCode::InvalidDeflate || error.offset != 3) { throw TestAssertionFailed {.code = 4}; }
        }
        try {
            own i32* value = new i32(41);
            MoveError error = {.payload = move value, .code = 9};
            throw (false) move error;
            if (error.code != 9) { throw TestAssertionFailed {.code = 6}; }
            transfer(false, move error);
            own i32* other = new i32(41);
            MoveError next = {.payload = move other, .code = 10};
            transfer(true, move next);
            throw TestAssertionFailed {.code = 7};
        } catch (MoveError error) {
            if (error.code != 10) { throw TestAssertionFailed {.code = 8}; }
        }
        try {
            ZipError error = {.code = ZipErrorCode::InvalidDeflate, .offset = 11, .message = "named"};
            switch (count) {
            case 0:
                throw (count != 0) error;
                count += 1;
                break;
            default:
                throw error;
            }
            if (count != 1) { throw TestAssertionFailed {.code = 9}; }
            switch (count) {
            case 1:
                throw (count == 1) (error);
                throw TestAssertionFailed {.code = 10};
            default:
                throw TestAssertionFailed {.code = 11};
            }
        } catch (ZipError error) {
            if (error.offset != 11) { throw TestAssertionFailed {.code = 12}; }
        }
        try {
            i32 ignored = constant_true();
            ignored as void;
            throw TestAssertionFailed {.code = 13};
        } catch (ZipError error) {
            if (error.offset != 8) { throw TestAssertionFailed {.code = 14}; }
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
