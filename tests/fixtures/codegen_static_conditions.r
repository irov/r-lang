module test.codegen.static_conditions;

@if (core::profile is hosted-native-async) {
    @noalloc @nonblocking i32 platform_bias() { return 1; }
} @else @if (core::profile is hosted) {
    i32 platform_bias() { return 1; }
} @else {
    i32 platform_bias() { return absent_platform; }
}

struct Child { own i32* data; };
struct Parent { Child child; };
error Failure { Parent payload; };
drop(Child* self) { *(self->data) = 9; }
drop(Parent* self) { *(self->child.data) = 8; }

trait Measured { i32 measure(const Self* this); };
impl Measured for i32 {
    i32 measure(const i32* this) { return *this; }
};

@generic<T>
T transfer(T value) {
    @if (T is copy) {
        T copied = value;
        return copied;
    } @else {
        @if (T is copy) { return inactive_name; }
        return move value;
    }
}

@generic<T>
i32 measure_or_zero(const T* value) {
    @if (T is Measured) {
        i32 measured = value->measure();
        return measured;
    } @else { return 0; }
}

@generic<T>
T adjust(T value) {
    @if (T is i32) { return value + 1; }
    @else { return move value; }
}

@generic<F>
i32 invoke_or_zero(const F* callback) {
    @if (F is fn(i32) -> i32) {
        i32 result = callback(41);
        return result;
    } @else { return 0; }
}

@noalloc @nonblocking
u32 checksum(const u8[] input) {
    @if (u8 is pod && !(u8 is std.string::string)) {
        return std.hash::crc32(input);
    } @else {
        own i32* inactive = new i32(0);
        return 0u32;
    }
}

i32 main() {
    try {
        i32 bias = platform_bias();
        i32 original = 40;
        i32 copied = transfer(original);
        i32 adjusted = adjust(copied);
        i32 measured = measure_or_zero(&adjusted);
        bool unsupported = false;
        i32 fallback = measure_or_zero(&unsupported);
        fn i32 increment(i32 value) { return value + bias; }
        i32 called = invoke_or_zero(&increment);
        i32 not_callable = invoke_or_zero(&original);
        u8[3] input = {0x61u8, 0x62u8, 0x63u8};
        u32 crc = checksum(input);
        if (original != 40 || measured != 41 || fallback != 0 || called != 42 ||
            not_callable != 0 || crc != 0x352441c2u32) { throw TestAssertionFailed {.code = 1}; }

        Parent source = {.child = Child {.data = new i32(7)}};
        Parent moved = transfer(move source);
        if (*(moved.child.data) != 7) { throw TestAssertionFailed {.code = 2}; }
        drop moved;
        try {
            Failure failure = {.payload = Parent {.child = Child {.data = new i32(7)}}};
            Failure transferred = transfer(move failure);
            throw move transferred;
        } catch (Failure caught) {
            if (*(caught.payload.child.data) != 7) { throw TestAssertionFailed {.code = 3}; }
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
