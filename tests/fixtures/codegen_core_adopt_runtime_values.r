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

protected own runtime_values* make_runtime_values() throws std.alloc::alloc_error,
    std.array::push_error<i32>, std.list::push_error<i32>, std.dict::insert_error<i32, i32> {
    array<i32> numbers = std.array::create::<i32>();
    std.array::push(&numbers, 10);
    list<i32> chain = std.list::create::<i32>();
    std.list::push_back(&chain, 20) as void;
    dict<i32, i32> table = std.dict::create::<i32, i32>();
    std.dict::insert(&table, 30, 31) as void;
    arc i32 shared = new arc i32(40);
    rc i32 local = new rc i32(50);
    weak arc i32 shared_observer = std.arc::downgrade(&shared);
    weak rc i32 local_observer = std.rc::downgrade(&local);
    own runtime_values* values = new runtime_values {
        .dynamic_array = move numbers,
        .stable_list = move chain,
        .ordered_dict = move table,
        .shared_arc = move shared,
        .shared_rc = move local,
        .weak_shared_arc = move shared_observer,
        .weak_shared_rc = move local_observer,
        .atomic_value = 60u32,
    };
    return move values;
}

/* Status 0 when every member holds what make_runtime_values stored. */
protected i32 check_runtime_values(const runtime_values* values) {
    if ((len(values->dynamic_array) != 1usize) || (values->dynamic_array[0] != 10)) {
        return 1;
    }
    o<const i32*> front = std.list::front(&values->stable_list);
    switch (front) {
        case variant o::some(element):
            if (**element != 20) {
                return 2;
            }
            break;
        case variant o::none:
            return 2;
    }
    i32 key = 30;
    o<const i32*> mapped = std.dict::get(&values->ordered_dict, &key);
    switch (mapped) {
        case variant o::some(element):
            if (**element != 31) {
                return 3;
            }
            break;
        case variant o::none:
            return 3;
    }
    if ((*values->shared_arc != 40) || (std.arc::strong_count(&values->shared_arc) != 1usize) ||
        (std.arc::weak_count(&values->shared_arc) != 1usize)) {
        return 4;
    }
    if ((*values->shared_rc != 50) || (std.rc::strong_count(&values->shared_rc) != 1usize) ||
        (std.rc::weak_count(&values->shared_rc) != 1usize)) {
        return 5;
    }
    if (core::atomic_load(&values->atomic_value, core::memory_order::relaxed) != 60u32) {
        return 6;
    }
    return 0;
}

/* The C side of the wrapper (R-FFI-0011): makeValues releases new values to C; dropValues and
   releaseValues hand them back to drop_runtime_values and release_runtime_values; checkValues
   adopts them, checks every member and releases them to C again. */
@safety("VALUES-MAKE", "Called after runtime start; a non-null result goes to dropValues once")
@export_name("makeValues")
extern "C" raw void*? make_values() {
    try {
        own runtime_values* values = make_runtime_values();
        unsafe {
            raw runtime_values* pointer = core::release(move values);
            return pointer as raw void*;
        }
    } catch (std.alloc::alloc_error failure) {
        failure as void;
    } catch (std.array::push_error<i32> failure) {
        failure as void;
    } catch (std.list::push_error<i32> failure) {
        failure as void;
    } catch (std.dict::insert_error<i32, i32> failure) {
        failure as void;
    }
    return null;
}

@safety("VALUES-DROP", "pointer holds owned runtime_values and is not used again")
@export_name("dropValues")
extern "C" void drop_values(raw void* pointer) {
    unsafe {
        drop_runtime_values(pointer as raw runtime_values*);
    }
}

@safety("VALUES-RELEASE", "pointer holds owned runtime_values; the result carries their ownership")
@export_name("releaseValues")
extern "C" raw void* release_values(raw void* pointer) {
    unsafe {
        return release_runtime_values(pointer as raw runtime_values*) as raw void*;
    }
}

@safety("VALUES-CHECK", "pointer holds owned runtime_values, which stay owned by the caller")
@export_name("checkValues")
extern "C" c_int check_values(raw void* pointer) {
    unsafe {
        own runtime_values* owner = core::adopt(pointer as raw runtime_values*);
        i32 status = check_runtime_values(&*owner);
        raw runtime_values* returned = core::release(move owner);
        returned as void;
        return status as c_int;
    }
}

i32 main() {
    return 0;
}
