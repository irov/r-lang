module audit.aggregate_initializer_exclusive_borrow_then_read_same_origin;

struct Coupled {
    i32* exclusive;
    i32 copied;
};

i32 main() {
    i32 value = 17;
    Coupled coupled = Coupled {
        .exclusive = &value,
        .copied = value,
    };
    return *(coupled.exclusive) - coupled.copied;
}
