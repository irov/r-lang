module test.codegen.async_opaque_results;

trait Read { i32 read(const Self* this); };
struct Owner { own i32* value; };
drop(Owner* self) { *(self->value) = 9; }
impl Read for Owner {
    i32 read(const Owner* this) { return *(this->value); }
};

async opaque(Read & send & unborrowed) owned(i32 value) {
    return Owner {.value = new i32(value)};
}

opaque(fn once() -> i32 & send & unborrowed) deferred(i32 value) {
    Owner owner = {.value = new i32(value)};
    fn once i32 read() move(owner) { return *(owner.value); }
    return move read;
}

struct CopyValue { i32 value; };
impl Read for CopyValue { i32 read(const CopyValue* this) { return this->value; } };
@generic<T: Read & send & unborrowed>
opaque(Read & send & unborrowed) forward(T value) {
    @if (T is copy) { return value; }
    @else { return move value; }
}

async i32 main() {
    try {
        try {
            auto owner = await owned(20);
            auto transferred = forward(move owner);
            auto copied = forward(CopyValue {.value=42});
            if (copied.read() != 42) { throw TestAssertionFailed {.code = 2}; }
            auto read = deferred(22);
            i32 total = transferred.read() + (move read).call();
            if (total != 42) { throw TestAssertionFailed {.code = 1}; }
        } catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 90}; }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
