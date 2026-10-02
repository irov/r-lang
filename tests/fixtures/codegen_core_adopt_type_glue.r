module codegen.core_adopt_type_glue;

// Retain values whose purpose here is type or lifetime coverage. An owner's payload holds no
// borrow, slice or ordinary str (R-BORROW-0018); those pointees are rejected by core::adopt.
@generic<T> @noalloc @nonblocking
protected void test_observe(const T* value) { value as void; }


struct Cell {
    own i32* inner;
};

struct Box {
    Cell[2] fixed;
    o<own i32*> optional;
    o<own i32*> outcome;
    o<own u32*> unsigned_outcome;
    task<i32> pending;
};

void adopt_box(raw Box* pointer) {
    unsafe {
        own Box* owner = core::adopt(pointer);
        test_observe(&owner);
    }
}

void round_trip_constexpr_str(raw (constexpr str)* pointer) {
    unsafe {
        own (constexpr str)* owner = core::adopt(pointer);
        raw (constexpr str)* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_raw_pointer(raw (raw i32*)* pointer) {
    unsafe {
        own (raw i32*)* owner = core::adopt(pointer);
        raw (raw i32*)* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_owner(raw (own i32*)* pointer) {
    unsafe {
        own (own i32*)* owner = core::adopt(pointer);
        raw (own i32*)* returned = core::release(move owner);
        returned as void;
    }
}

i32 main() {
    return 0;
}
