module test.semantic.fields_constraint_missing;

/* R-REFL-0006: the methods of a field of a type parameter are those of its fields constraint. */
trait Weigh {
    usize weight(const Self* this);
};

@generic<T>
usize total(const T* value) {
    usize sum = 0usize;
    for (constexpr usize index in 0usize..core::field_count::<T>()) {
        sum += core::field(value, index)->weight();
    }
    return sum;
}

i32 main() { return 0; }
