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

/* The C side of the wrapper (R-FFI-0011): makeBox releases a new Box to C, and dropBox and
   releaseBox hand it back to drop_box and release_box, which adopt it again. */
@safety("BOX-MAKE", "Called after runtime start; a non-null result goes to dropBox exactly once")
@export_name("makeBox")
extern "C" raw void*? make_box(c_int first, c_int second) {
    try {
        own Box* owner = new Box {.first = new i32(first as i32), .second = new i32(second as i32)};
        unsafe {
            raw Box* pointer = core::release(move owner);
            return pointer as raw void*;
        }
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return null;
    }
}

@safety("BOX-DROP", "pointer is the owned Box of makeBox or releaseBox and is not used again")
@export_name("dropBox")
extern "C" void drop_box_entry(raw void* pointer, c_int early) {
    unsafe {
        drop_box(pointer as raw Box*, early != (0 as c_int));
    }
}

@safety("BOX-RELEASE", "pointer is the owned Box of makeBox; the result carries its ownership")
@export_name("releaseBox")
extern "C" raw void* release_box_entry(raw void* pointer) {
    unsafe {
        return release_box(pointer as raw Box*) as raw void*;
    }
}

i32 main() {
    return 0;
}
