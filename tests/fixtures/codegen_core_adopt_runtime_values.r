module codegen.core_adopt_runtime_values;

// Retain values whose purpose here is type or lifetime coverage.
@generic<T> @noalloc @nonblocking
protected void test_observe(const T* value) { value as void; }


struct runtime_values {
    array<i32> dynamic_array;
    list<i32> stable_list;
    dict<i32, i32> ordered_dict;
    arc i32 shared_arc;
    rc i32 shared_rc;
    weak arc i32 weak_shared_arc;
    weak rc i32 weak_shared_rc;
    au32 atomic_value;
};

void drop_runtime_values(raw runtime_values* pointer) {
    unsafe {
        own runtime_values* owner = core::adopt(pointer);
        test_observe(&owner);
    }
}

raw runtime_values* release_runtime_values(raw runtime_values* pointer) {
    unsafe {
        own runtime_values* owner = core::adopt(pointer);
        raw runtime_values* returned = core::release(move owner);
        return returned;
    }
}

i32 main() {
    return 0;
}
