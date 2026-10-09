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

protected async i32 seven() {
    return 7;
}

/* The C side of the wrapper (R-FFI-0011). makeBox releases a new Box to C whose members hold
   base + 10 to base + 40; selected 0 puts base + 40 in outcome, otherwise in unsigned_outcome.
   adoptBox hands a Box back to adopt_box, which drops it. */
@safety("BOX-MAKE", "Called after runtime start; a non-null result goes to adoptBox exactly once")
@export_name("makeBox")
extern "C" raw void*? make_box(c_int base, c_int selected) {
    i32 first = base as i32;
    try {
        o<own i32*> outcome = o::none;
        o<own u32*> unsigned_outcome = o::none;
        if (selected == (0 as c_int)) {
            outcome = o::some(new i32(first + 40));
        } else {
            unsigned_outcome = o::some(new u32((first + 40) as u32));
        }
        task<i32> pending = seven();
        own Box* owner = new Box {
            .fixed = {Cell {.inner = new i32(first + 10)}, Cell {.inner = new i32(first + 20)}},
            .optional = o::some(new i32(first + 30)),
            .outcome = move outcome,
            .unsigned_outcome = move unsigned_outcome,
            .pending = move pending,
        };
        unsafe {
            raw Box* pointer = core::release(move owner);
            return pointer as raw void*;
        }
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return null;
    } catch (std.async::start_error failure) {
        failure as void;
        return null;
    }
}

@safety("BOX-ADOPT", "pointer is an owned, initialized Box allocated by the R allocator")
@export_name("adoptBox")
extern "C" void adopt_box_entry(raw void* pointer) {
    unsafe {
        adopt_box(pointer as raw Box*);
    }
}

/* Each round trip hands back the allocation it adopted, which still holds its value. */
i32 main() {
    i32 cell = 9;
    own (constexpr str)* text = new (constexpr str)("glue");
    own (own i32*)* nested = new (own i32*)(new i32(5));
    unsafe {
        own (raw i32*)* address = new (raw i32*)(&cell as raw i32*);
        raw (constexpr str)* text_pointer = core::release(move text);
        round_trip_constexpr_str(text_pointer);
        own (constexpr str)* text_back = core::adopt(text_pointer);
        switch (*text_back) {
        case "glue": break;
        default: return 1;
        }
        raw (raw i32*)* address_pointer = core::release(move address);
        round_trip_raw_pointer(address_pointer);
        own (raw i32*)* address_back = core::adopt(address_pointer);
        if (**address_back != 9) {
            return 2;
        }
        raw (own i32*)* nested_pointer = core::release(move nested);
        round_trip_owner(nested_pointer);
        own (own i32*)* nested_back = core::adopt(nested_pointer);
        if (**nested_back != 5) {
            return 3;
        }
    }
    return 0;
}
