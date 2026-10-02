module test.codegen.resource_contracts;

// Shared mutable storage preserves the update and cleanup scenario under test.
struct TestStorage1 { c_int value; };
struct TestStorage2 { i32 value; };
struct TestStorage3 { c_int value; };

error InvalidSample { u32 value; };
struct Observation { i32* counter; };

@noalloc @nonblocking
drop(Observation* self) {
    *self->counter += 1;
}

@generic<T: copy>
@noalloc @nonblocking
T identity(T value) {
    return move value;
}

@noalloc @nonblocking
u32 validate(u32 value) throws InvalidSample {
    throw (value != 7u32) InvalidSample { .value = value };
    return value;
}

@noalloc @nonblocking
u32 checksum(const u8[] input) {
    return std.hash::crc32(input);
}

@generic<F: fn @noalloc @nonblocking(u32) -> u32>
@noalloc @nonblocking
u32 dispatch(F operation, u32 value) {
    u32 result = operation.call(value);
    return result;
}

@callback
@safety("RESOURCE-POINTER", "Called with an attached runtime and exact C types")
@export_name("resourceIncrement")
@noalloc
extern "C" c_int increment(c_int value) { return value + (1 as c_int); }

@noalloc
c_int indirect(raw fn @noalloc(c_int) -> c_int operation, c_int value) {
    TestStorage1 storage_result = {.value = (0 as c_int)};
    unsafe { storage_result.value = operation(value); }
    return storage_result.value;
}

@safety("RESOURCE-CHECK", "Called after runtime initialization")
@export_name("checkResources")
@noalloc
extern "C" c_int check_resources() {
    fn @noalloc @nonblocking u32 transform(u32 value) { return value + 1u32; }
    u32 transformed = dispatch(transform, 41u32);
    if (transformed != 42u32) { return (8 as c_int); }
    raw fn @noalloc(c_int) -> c_int operation = increment;
    raw fn(c_int) -> c_int ordinary = operation;
    raw fn @noalloc?(c_int) -> c_int optional = operation;
    c_int indirect_result = indirect(operation, 41 as c_int);
    if (indirect_result != (42 as c_int)) { return (9 as c_int); }
    unsafe {
        if (optional == null) { return (10 as c_int); }
        c_int optional_result = optional(41 as c_int);
        if (optional_result != (42 as c_int)) { return (11 as c_int); }
    }
    i32 observations = 0;
    TestStorage2 storage_completed = {.value = 0};
    i32 pending = 41;
    i32 old_pending = core::replace(&pending, 42);
    i32 taken_pending = core::take(&pending);
    taken_pending as void;
    if (old_pending != 41 || taken_pending != 42 || pending != 0) { return (7 as c_int); }
    {
        Observation observation = { .counter = &observations };
        u8[3] input = {0x61u8, 0x62u8, 0x63u8};
        u32 actual = checksum(input);
        if (actual != 0x352441c2u32) { return (1 as c_int); }
        u32 copied = identity(actual);
        if (copied != actual) { return (2 as c_int); }
        try {
            u32 accepted = validate(7u32);
            if (accepted != 7u32) { return (3 as c_int); }
            u32 rejected = validate(8u32);
            rejected as void;
            return (4 as c_int);
        } catch (InvalidSample failure) {
            if (failure.value != 8u32) { return (5 as c_int); }
        } finally {
            u32 marker = identity(42u32);
            storage_completed.value = marker as i32;
        }
    }
    c_int selected = observations == 1 && storage_completed.value == 42 ? (0 as c_int) : (6 as c_int);
    return selected;
}

@noalloc
async u32 calculate(u32 input) {
    fn @noalloc @nonblocking u32 transform(u32 value) { return value + 1u32; }
    u32 result = dispatch(transform, input);
    return result;
}

async i32 main() {
    try {
        TestStorage3 storage_status = {.value = (0 as c_int)};
        unsafe { storage_status.value = check_resources(); }
        if (storage_status.value != (0 as c_int)) { throw TestAssertionFailed {.code = 1}; }
        try {
            u32 result = await calculate(41u32);
            i32 selected = result == 42u32 ? 0 : 2;
            return selected;
        } catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 3}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
