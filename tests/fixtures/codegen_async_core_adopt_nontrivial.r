module codegen.async_core_adopt_nontrivial;

/* R-FUNC-0011: an asynchronous frame owns every committed by-value parameter and every local
   live across a suspension and destroys each exactly once, also during cancellation cleanup.
   The wrapper logs the releases of the two Box owners below and of their members. */

// Retain values whose purpose here is type or lifetime coverage.
@generic<T> @noalloc @nonblocking
protected void test_observe(const T* value) { value as void; }

struct Box {
    own i32* first;
    own i32* second;
};

protected own Box* make_box(i32 first, i32 second) throws std.alloc::alloc_error {
    own Box* box = new Box {.first = new i32(first), .second = new i32(second)};
    return move box;
}

protected raw Box* release_box(own Box* box) {
    unsafe {
        raw Box* pointer = core::release(move box);
        return pointer;
    }
}

/* Releases the Box it receives and adopts it again (R-UNSAFE-0008), reports that it holds it and
   waits for a notification that never comes: the adopted owner is in the frame, and the moved-out
   parameter is not, when main cancels the task. */
protected async void hold_box(own Box* box, std.async::notify held, std.async::notify silent)
    throws std.async::start_error {
    raw Box* pointer = release_box(move box);
    unsafe {
        own Box* owner = core::adopt(pointer);
        held.notify_one();
        await silent.notified();
        test_observe(&owner);
    }
}

/* Cancelled at once: whether or not its body ran, the frame owns the Box parameter. */
protected async void keep_box(own Box* box, std.async::notify silent)
    throws std.async::start_error {
    await silent.notified();
    test_observe(&box);
}

async i32 main() {
    try {
        std.async::notify held = std.async::notify_new();
        std.async::notify silent = std.async::notify_new();
        own Box* adopted = make_box(31, 47);
        task<void> ready = held.notified();
        task<void throws std.async::start_error> holder =
            hold_box(move adopted, held.clone(), silent.clone());
        await move ready;
        std.async::cancel(move holder);
        own Box* parameter = make_box(131, 147);
        task<void throws std.async::start_error> keeper = keep_box(move parameter, silent.clone());
        std.async::cancel(move keeper);
        return 0;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 1;
    } catch (std.async::start_error failure) {
        failure as void;
        return 2;
    }
}
