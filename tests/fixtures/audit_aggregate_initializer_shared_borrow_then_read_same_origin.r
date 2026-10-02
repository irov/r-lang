module audit.aggregate_initializer_shared_borrow_then_read_same_origin;

struct Observed {
    const i32* shared;
    i32 copied;
};

i32 main() {
    i32 value = 17;
    Observed observed = Observed {
        .shared = &value,
        .copied = value,
    };
    return *(observed.shared) - observed.copied;
}
