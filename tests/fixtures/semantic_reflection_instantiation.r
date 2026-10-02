module test.semantic.reflection_instantiation;

/* R-REFL-0001: a form over a generic parameter is checked when the parameter is substituted;
   instantiating with a non-enum type is rejected. */

@generic<T: copy>
usize count_of(T value) {
    usize count = core::enum_count::<T>();
    value as void;
    return count;
}

i32 main() {
    usize count = count_of(7i32);
    return count as i32;
}
