module examples.c_callbacks;

@callback
@export_name("r_example_increment")
@safety("EXAMPLE-INCREMENT", "The runtime is initialized and the caller may enter R")
extern "C" c_int increment(c_int value) {
    return value + 1i32 as c_int;
}

@callback
@safety("EXAMPLE-DESTROY-OWNER", "The pointer uniquely owns a released R i32 allocation; the runtime is initialized")
extern "C" void destroy_owner(raw void* pointer) {
    unsafe {
        own i32* owner = core::adopt(pointer as raw i32*);
        drop owner; // Destroy the adopted allocation.
    }
}

std.c::handle create_handle(i32 value) {
    own i32* owner = new i32(value);
    unsafe {
        raw i32* pointer = core::release(move owner);
        std.c::handle handle = std.c::adopt_handle(pointer as raw void*, destroy_owner);
        return move handle;
    }
}

i32 main() {
    raw fn(c_int) -> c_int callback = increment;
    std.c::handle handle = create_handle(41);
    unsafe {
        raw i32* pointer = handle.pointer() as raw i32*;
        if (callback(*pointer as c_int) != 42i32 as c_int) { return 1; }
        raw fn?(c_int) -> c_int optional_callback = callback;
        if (optional_callback != null) {
            if (optional_callback(0i32 as c_int) != 1i32 as c_int) { return 2; }
        }
    }
    // Releasing transfers the obligation to the caller without invoking the destructor.
    raw void* released = (move handle).release();
    unsafe {
        raw fn(raw void*) -> void destroy = destroy_owner;
        destroy(released);
    }
    {
        std.c::handle automatic = create_handle(7);
        unsafe { raw void* observed = automatic.pointer(); observed as void; }
        // This handle invokes destroy_owner exactly once at scope exit.
    }
    return 0;
}
