module codegen.core_adopt_nontrivial;

// Retain values whose purpose here is type or lifetime coverage.
@generic<T> @noalloc @nonblocking
protected void test_observe(const T* value) { value as void; }


struct Box {
    own i32* first;
    own i32* second;
};

void drop_box(raw Box* pointer, bool early) {
    unsafe {
        own Box* owner = core::adopt(pointer);
        test_observe(&owner);
        if (early == true) {
            return;
        }
    }
}

raw Box* release_box(raw Box* pointer) {
    unsafe {
        own Box* owner = core::adopt(pointer);
        raw Box* returned = core::release(move owner);
        return returned;
    }
}

i32 main() {
    return 0;
}
