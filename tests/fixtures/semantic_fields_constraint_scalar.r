module test.semantic.fields_constraint_scalar;

/* R-TYPE-0043: a fields constraint requires a struct or tuple argument. */
trait Weigh {
    usize weight(const Self* this);
};

impl Weigh for u32 { usize weight(const u32* this) { return 4usize; } };

@generic<T: fields(Weigh)>
usize total(const T* value) {
    value as void;
    return 0usize;
}

i32 main() {
    u32 plain = 1u32;
    return total(&plain) as i32;
}
