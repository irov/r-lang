module audit.std_c_adopt_handle;

error ExpectedError { Raised, };

@safety("AUDIT-DESTRUCTION-COUNT", "Calls are serialized on the current thread")
unsafe i32 destruction_count(i32 increment) {
    static i32 count = 0;
    count += increment;
    return count;
}

@callback
@safety("AUDIT-DESTROY-OWNER", "The pointer uniquely owns a released R i32 allocation; the runtime is initialized")
extern "C" void destroy_owner(raw void* pointer) {
    unsafe {
        raw i32* typed = pointer as raw i32*;
        own i32* owner = core::adopt(typed);
        destruction_count(*owner) as void;
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

void fail_with_handle(std.c::handle handle) throws ExpectedError {
    throw ExpectedError::Raised;
}

i32 main() {
    try {
        {
            std.c::handle handle = create_handle(1);
            std.c::handle moved = move handle;
            unsafe {
                raw i32* pointer = std.c::handle_pointer(&moved) as raw i32*;
                if (*pointer != 1 || destruction_count(0) != 0) { throw TestAssertionFailed {.code = 1}; }
            }
        }
        unsafe { if (destruction_count(0) != 1) { throw TestAssertionFailed {.code = 2}; } }
        {
            std.c::handle handle = create_handle(10);
            raw void* pointer = std.c::release_handle(move handle);
            unsafe {
                if (destruction_count(0) != 1) { throw TestAssertionFailed {.code = 3}; }
                raw fn(raw void*) -> void destroy = destroy_owner;
                destroy(pointer);
            }
        }
        unsafe { if (destruction_count(0) != 11) { throw TestAssertionFailed {.code = 4}; } }
        i32 finally_count = 0;
        try {
            try {
                std.c::handle handle = create_handle(100);
                fail_with_handle(move handle);
            } finally {
                finally_count += 1;
            }
        } catch (ExpectedError failure) {
            unsafe {
                if (destruction_count(0) != 111 || finally_count != 1) { throw TestAssertionFailed {.code = 5}; }
            }
            return 0;
        }
        throw TestAssertionFailed {.code = 6};
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
