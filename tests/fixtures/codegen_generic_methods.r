module test.codegen.generic_methods;

/* R-TYPE-0037, R-FUNC-0013: methods and implementations of a generic owner. */
@generic<T>
struct Holder {
    T value;
};

trait Reported {
    u64 report(const Self* this);
};

@generic<T: copy>
protected T Holder<T>::get(const Holder<T>* this) {
    return this->value;
}

@generic<T: copy & unborrowed>
protected void Holder<T>::set(Holder<T>* this, T value) {
    this->value = value;
}

@generic<T: copy>
impl Reported for Holder<T> {
    u64 report(const Holder<T>* this) {
        return 11u64;
    }
};

@generic<U: Reported>
u64 report_of(const U* value) {
    u64 code = value->report();
    return code;
}

i32 main() {
    Holder<i32> numbers = Holder<i32> { .value = 4 };
    i32 first = numbers.get();
    if (first != 4) { return 1; }
    numbers.set(9);
    i32 second = numbers.get();
    if (second != 9) { return 2; }

    Holder<u8> bytes = Holder<u8> { .value = 3u8 };
    u8 third = bytes.get();
    if (third != 3) { return 3; }

    u64 reported = report_of(&numbers);
    if (reported != 11) { return 4; }
    u64 also = report_of(&bytes);
    if (also != 11) { return 5; }
    return 0;
}
