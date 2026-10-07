module test.semantic.fields_constraint_violation;

/* R-TYPE-0043: every field of the argument of a fields(Trait) constraint implements the trait. */
trait Weigh {
    usize weight(const Self* this);
};

impl Weigh for u32 { usize weight(const u32* this) { return 4usize; } };

struct Mixed { u32 count; f64 ratio; };

@generic<T: fields(Weigh)>
usize total(const T* value) {
    usize sum = 0usize;
    for (constexpr usize index in 0usize..core::field_count::<T>()) {
        sum += core::field(value, index)->weight();
    }
    return sum;
}

i32 main() {
    Mixed mixed = {.count = 1u32, .ratio = 0.5};
    return total(&mixed) as i32;
}
