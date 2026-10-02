module regression.pointer_const_implicit_conversion;

i32 main() {
    i32 value = 1;
    unsafe {
        raw i32* mutable_pointer = &value as raw i32*;
        raw const i32* const_pointer = mutable_pointer;
        return *const_pointer;
    }
}
