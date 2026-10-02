module test.codegen.generic_key;
@generic<T: key> struct Box { T value; };
@generic<T: key> u64 Box<T>::hash(const Box<T>* value) {
    u64 result = core::hash(&value->value);
    return result;
}
@generic<T: key> bool Box<T>::equal(const Box<T>* left, const Box<T>* right) {
    bool result = core::key_equal(&left->value, &right->value);
    return result;
}
i32 main() {
    try {
        Box<i32> value = Box<i32> { .value = 1 };
        Box<i32> other = Box<i32> { .value = 1 };
        u64 hash = core::hash(&value);
        bool same = Box<i32>::equal(&value, &other);
        if (same != true) { throw TestAssertionFailed {.code = 1}; }
        u64 second_hash = Box<i32>::hash(&other);
        if (hash != second_hash) { throw TestAssertionFailed {.code = 2}; }
        constexpr str text = "same";
        constexpr str second = "same";
        u64 text_hash = core::hash(&text);
        u64 second_text_hash = core::hash(&second);
        bool same_text = core::key_equal(&text, &second);
        if (same_text == false || text_hash != second_text_hash) { throw TestAssertionFailed {.code = 3}; }
        f64 zero = 0.0;
        f64 minus_zero = -0.0;
        u64 zero_hash = core::hash(&zero);
        u64 minus_zero_hash = core::hash(&minus_zero);
        bool same_zero = core::key_equal(&zero, &minus_zero);
        if (same_zero == false || zero_hash != minus_zero_hash) { throw TestAssertionFailed {.code = 4}; }
        own i32* owner = new i32(31);
        u64 owner_hash = core::hash(&owner);
        u64 same_owner_hash = core::hash(&owner);
        bool same_owner = core::key_equal(&owner, &owner);
        if (same_owner == false || owner_hash != same_owner_hash || *owner != 31) { throw TestAssertionFailed {.code = 5}; }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
