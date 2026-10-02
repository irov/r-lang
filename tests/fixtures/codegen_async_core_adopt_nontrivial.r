module codegen.async_core_adopt_nontrivial;

struct Box {
    own i32* first;
    own i32* second;
};

async i32 main() {
    if (false) {
        i32 value = 0;
        unsafe {
            raw i32* pointer = &value as raw i32*;
            own i32* owner = core::adopt(pointer);
        }
    }
    return 0;
}

void retain_box_glue(raw Box* pointer) {
    unsafe {
        own Box* owner = core::adopt(pointer);
        raw Box* returned = core::release(move owner);
        returned as void;
    }
}
