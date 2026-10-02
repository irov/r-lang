module test.codegen.function_items;

error Failed { i32 value; };

@noalloc @nonblocking
i32 increment(i32 value) { return value + 1; }
i32 checked(i32 value) throws Failed {
    if (value < 0) { throw Failed {.value = value}; }
    return value;
}
const i32* identity(const i32* value) { return value; }
i32 counter() {
    static i32 count = 0;
    unsafe { count += 1; return count; }
}

@generic<F: fn @noalloc @nonblocking(i32) -> i32>
@noalloc @nonblocking
i32 invoke(F operation, i32 value) { return operation(value); }
@generic<F: fn mut(i32) -> i32>
i32 invoke_mut(F operation, i32 value) { return operation.call(value); }
@generic<F: fn once(i32) -> i32>
i32 invoke_once(F operation, i32 value) { return (move operation).call(value); }
@generic<F: fn(i32) -> i32 throws(Failed)>
i32 invoke_checked(F operation, i32 value) throws Failed { return operation(value); }
@generic<F: fn(const i32*) -> const i32*>
const i32* invoke_borrow(const F* operation, const i32* value) { return operation(value); }
opaque(fn(i32) -> i32 & copy) factory() { return increment; }

void noop() {}

i32 main() {
    auto empty = noop;
    empty();
    auto operation = increment;
    auto copy = operation;
    if (operation(41) != 42 || copy.call(41) != 42) { return 1; }
    if (invoke(increment, 41) != 42) { return 2; }
    if (invoke_mut(increment, 41) != 42 || invoke_once(increment, 41) != 42) { return 3; }
    auto hidden = factory();
    if (hidden(41) != 42) { return 4; }
    auto borrowed = identity;
    i32 value = 42;
    const i32* returned = invoke_borrow(&borrowed, &value);
    if (*returned != 42) { return 5; }
    auto counted = counter;
    if (counter() != 1 || counted() != 2 || counter() != 3) { return 6; }
    try { invoke_checked(checked, -42) as void; return 7; }
    catch (Failed failure) { if (failure.value != -42) { return 8; } }
    try { if (invoke_checked(checked, 42) != 42) { return 9; } }
    catch (Failed failure) { failure as void; return 10; }
    return 0;
}
