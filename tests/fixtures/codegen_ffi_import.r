module test.codegen.ffi_import;

// Shared mutable storage preserves the update and cleanup scenario under test.
struct TestStorage1 { i32 value; };

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @safety("PROBE-INCREMENT", "The function has no preconditions")
    c_int probe_increment(c_int value);

    @safety("PROBE-APPLY", "callback shall be a valid C function pointer for the whole call")
    c_int probe_apply(raw fn(c_int) -> c_int callback, c_int value);

    @safety("PROBE-NAME", "The returned pointer addresses an immutable NUL-terminated string")
    raw const c_char* probe_name();

    @safety("PROBE-TEXT-LENGTH", "text shall address a NUL-terminated string")
    c_size probe_text_length(raw const c_char* text);

    @safety("PROBE-STORE", "destination shall address one writable int for the whole call")
    void probe_store(raw c_int* destination, c_int value);
}

@callback
@safety("PROBE-DOUBLE", "The runtime is initialized and the caller may enter R")
extern "C" c_int probe_double(c_int value) {
    return value + value;
}

i32 main() {
    try {
        own c_int* boxed = new c_int(0i32 as c_int);
        TestStorage1 storage_stored = {.value = 0};
        unsafe {
            raw c_int* pointer = core::release(move boxed);
            c_int incremented = probe_increment(41i32 as c_int);
            c_int applied = probe_apply(probe_double, 21i32 as c_int);
            raw const c_char* name = probe_name();
            c_size length = probe_text_length(name);
            if (incremented != 42i32 as c_int) {
                throw TestAssertionFailed {.code = 1};
            }
            if (applied != 42i32 as c_int) {
                throw TestAssertionFailed {.code = 2};
            }
            if (length != 5usize as c_size) {
                throw TestAssertionFailed {.code = 3};
            }
            probe_store(pointer, 7i32 as c_int);
            own c_int* adopted = core::adopt(pointer);
            storage_stored.value = *adopted as i32;
            drop adopted;
        }
        if (storage_stored.value != 7) {
            throw TestAssertionFailed {.code = 4};
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
