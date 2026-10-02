module codegen.async_core_slice_from_raw_parts;

async i32 main() {
    i32 value = 13;
    unsafe {
        raw i32* write_pointer = &value as raw i32*;
        i32[] mutable_view = core::slice_from_raw_parts_mut(write_pointer, 1usize);
        len(mutable_view) as void;
        raw const i32* read_pointer = &value as raw const i32*;
        const i32[] shared_view = core::slice_from_raw_parts(read_pointer, 1usize);
        len(shared_view) as void;
        const i32[] empty_view = core::slice_from_raw_parts(read_pointer, 0usize);
        len(empty_view) as void;
    }
    return 0;
}
