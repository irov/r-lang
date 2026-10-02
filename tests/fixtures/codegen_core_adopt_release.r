module codegen.core_adopt_release;

i32 main() {
    if (false) {
        i32 value = 17;
        unsafe {
            raw i32* pointer = &value as raw i32*;
            own i32* owner = core::adopt(pointer);
            raw i32* returned = core::release(move owner);
        }
    }
    if (false) {
        i32 value = 23;
        unsafe {
            raw i32* pointer = &value as raw i32*;
            own i32* owner = core::adopt(pointer);
        }
    }
    return 0;
}
