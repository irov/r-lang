module test.codegen.overloads;

error TextError { Invalid, };

i32 measure(i32 value) { return value + 1; }
i32 measure(str value) throws TextError {
    throw (len(value) == 0usize) TextError::Invalid;
    usize length = len(value);
    return length as i32;
}
i32 measure(i32 left, i32 right) { return left + right; }
i32 widen(i64 value) { return value as i32; }
i32 widen(str value) { usize length = len(value); return length as i32; }

struct Receipt { i32 total; };
void Receipt::append(Receipt* this, i32 value) { this->total += value; }
void Receipt::append(Receipt* this, str value) {
    usize length = len(value);
    this->total += length as i32;
}

@generic<T: copy>
i32 select(const T* value) { return 11; }
i32 select(str value) { return 22; }

@generic<T: copy>
i32 relay(const T* value) {
    i32 chosen = select(value);
    return chosen;
}

i32 promoted(i32 value) { return 1; }
i32 promoted(u8 value) { return 2; }
i32 access(i32* value) { return 3; }
i32 access(const i32* value) { return 4; }
i32 allocated(own i32* value) { return *value; }
i32 allocated(str value) { return 0; }

trait Labelled {
    i32 label(const Self* this, i32 width);
    i32 label(const Self* this, str prefix);
};

impl Labelled for Receipt {
    i32 label(const Receipt* this, i32 width) { return width; }
    i32 label(const Receipt* this, str prefix) { return 12; }
};

@generic<T: copy>
struct Label { T value; };

@generic<T: copy>
impl Labelled for Label<T> {
    i32 label(const Label<T>* this, i32 width) { return width; }
    i32 label(const Label<T>* this, str prefix) { return 12; }
};

@generic<T: Labelled>
i32 labels(const T* value) {
    i32 first = value->label(30);
    i32 second = value->label("Receipt");
    return first + second;
}

i32 main() {
    try {
        // The text overload's checked error does not affect an integer call.
        i32 numeric = measure(41);
        if (numeric != 42) { throw TestAssertionFailed {.code = 1}; }
        i32 pair = measure(20, 22);
        if (pair != 42) { throw TestAssertionFailed {.code = 2}; }
        i32 widened = widen(42);
        if (widened != 42) { throw TestAssertionFailed {.code = 3}; }
        Receipt receipt = Receipt { .total = 0 };
        receipt.append(39);
        receipt.append("abc");
        if (receipt.total != 42) { throw TestAssertionFailed {.code = 5}; }
        Receipt::append(&receipt, 1);
        if (receipt.total != 43) { throw TestAssertionFailed {.code = 6}; }
        i32 original = 7;
        i32 chosen = relay(&original);
        if (chosen != 11 || original != 7) { throw TestAssertionFailed {.code = 7}; }
        try {
            i32 length = measure("receipt");
            if (length != 7) { throw TestAssertionFailed {.code = 8}; }
        } catch (TextError failure) { throw TestAssertionFailed {.code = 9}; }
        u8 small = 2u8;
        i32 addition = promoted(small + small);
        i32 negation = promoted(-small);
        i32 literal = promoted(-2i8);
        if (addition != 1 || negation != 1 || literal != 1) { throw TestAssertionFailed {.code = 10}; }
        const i32* readonly = &original;
        i32 shared = access(&*readonly);
        if (shared != 4) { throw TestAssertionFailed {.code = 11}; }
        i32 combined = labels(&receipt);
        if (combined != 42) { throw TestAssertionFailed {.code = 12}; }
        Label<i32> label = Label<i32> { .value = 7 };
        i32 generic_labels = labels(&label);
        if (generic_labels != 42) { throw TestAssertionFailed {.code = 13}; }
        i32 owned = allocated(new i32(42));
        if (owned != 42) { throw TestAssertionFailed {.code = 14}; }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
