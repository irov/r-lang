module codegen.core_slice_from_raw_parts;

i32 main() {
    i32 value = 7;
    i32 result = 1;
    unsafe {
        raw i32* write_pointer = &value as raw i32*;
        i32[] mutable_view = core::slice_from_raw_parts_mut(write_pointer, 1usize);
        mutable_view[0] = 41;
        raw const i32* read_pointer = &value as raw const i32*;
        const i32[] shared_view = core::slice_from_raw_parts(read_pointer, 1usize);
        const i32[] empty_view = core::slice_from_raw_parts(read_pointer, 0usize);
        usize empty_count = len(empty_view);
        if (shared_view[0] == 41 && empty_count == 0) {
            result = 0;
        }
    }
    return result;
}
