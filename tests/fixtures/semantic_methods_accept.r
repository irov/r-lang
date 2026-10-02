module test.semantic.methods_accept;

/* R-FUNC-0013, R-FUNC-0014, R-TYPE-0041 through R-TYPE-0043, R-NAME-0010. */
struct Counter {
    i32 value;
};

trait Valued {
    i32 amount(const Self* this);
};

Counter Counter::zero() {
    return Counter { .value = 0 };
}

protected void Counter::add(Counter* this, i32 delta) {
    this->value += delta;
}

impl Valued for Counter {
    i32 amount(const Counter* this) {
        return this->value;
    }
};

impl Valued for i32 {
    i32 amount(const i32* this) {
        return *this;
    }
};

@generic<T: Valued>
i32 sum_of(const T* left, const T* right) {
    i32 first = left->amount();
    i32 second = right->amount();
    return first + second;
}

i32 main() {
    Counter counter = Counter::zero();
    counter.add(5);
    Counter other = Counter { .value = 2 };
    i32 total = sum_of(&counter, &other);
    i32 direct = counter.amount();
    return total + direct;
}
