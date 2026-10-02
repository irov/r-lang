module audit.async_std_c_adopt_handle;

// Shared mutable storage preserves the update and cleanup scenario under test.
struct TestStorage1 { i32 value; };

@safety("AUDIT-ASYNC-DESTRUCTION-COUNT", "The single root task serializes every call")
unsafe i32 destruction_count(i32 amount) {
    static i32 count = 0;
    count += amount;
    return count;
}

@callback
@safety("AUDIT-ASYNC-DESTROY", "The pointer uniquely owns a released R i32 allocation; the runtime is initialized")
extern "C" void destroy_owner(raw void* pointer) {
    unsafe {
        own i32* owner = core::adopt(pointer as raw i32*);
        destruction_count(*owner) as void;
    }
}

@callback
@safety("AUDIT-ASYNC-VALUE", "The runtime is initialized and the caller may enter R")
extern "C" c_int increment(c_int value) {
    return value + 1i32 as c_int;
}

async i32 step() { return 10; }

async i32 main() {
    try {
        TestStorage1 storage_value = {.value = 0};
        {
            raw fn(c_int) -> c_int callback = increment;
            own i32* owner = new i32(1);
            unsafe {
                raw i32* pointer = core::release(move owner);
                std.c::handle handle = std.c::adopt_handle(pointer as raw void*, destroy_owner);
                raw i32* observed = std.c::handle_pointer(&handle) as raw i32*;
                storage_value.value = callback(*observed as c_int) as i32;
            }
        }
        unsafe { if (storage_value.value != 2 || destruction_count(0) != 1) { throw TestAssertionFailed {.code = 1}; } }
        try {
            i32 resumed = await step();
            {
                own i32* owner = new i32(resumed);
                unsafe {
                    raw i32* pointer = core::release(move owner);
                    std.c::handle handle = std.c::adopt_handle(pointer as raw void*, destroy_owner);
                    raw void* released = std.c::release_handle(move handle);
                    raw fn(raw void*) -> void destroy = destroy_owner;
                    destroy(released);
                }
            }
            unsafe { if (destruction_count(0) != 11) { throw TestAssertionFailed {.code = 2}; } }
        } catch (std.async::start_error failure) {
            throw TestAssertionFailed {.code = 3};
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
