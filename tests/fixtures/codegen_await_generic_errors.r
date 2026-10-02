module test.codegen.async_generic_errors;
@generic<T>
error Failure { T value; };
@generic<T>
enum Value { Some(T), None, };
@generic<T: send & unborrowed>
async T selected(Value<T> source, T fallback) {
    switch (move source) {
        case variant Value<T>::Some(move value): return move value;
        case variant Value<T>::None: return move fallback;
    }
}
@generic<E: error & send & unborrowed>
async void fail(bool condition, E first, E second) throws E {
    try {
        throw (condition == true) move first else move second;
    } catch (E caught) {
        throw;
    }
}
@generic<T: copy & send & unborrowed>
async T choose(bool condition, T first, T second) {
    T result = (condition == true) ? move first : move second;
    return move result;
}
async i32 main() {
    i32 finalized = 0;
    try {
        Value<i32> source = Value<i32>::Some(7);
        i32 result = await selected(move source, 0);
        if (result != 7) { return 1; }
        i32 copied = await selected(move source, 0);
        if (copied != 7) { return 2; }
        own i32* a = new i32(11);
        own i32* b = new i32(13);
        Value<own i32*> moved = Value<own i32*>::Some(move a);
        own i32* owner = await selected(move moved, move b);
        if (*owner != 11) { return 3; }
        own i32* c = new i32(17);
        own i32* d = new i32(19);
        Failure<own i32*> left = Failure<own i32*> { .value = move c };
        Failure<own i32*> right = Failure<own i32*> { .value = move d };
        await fail(false, move left, move right);
        return 4;
    } catch (Failure<own i32*> caught) {
        if (*(caught.value) != 19) { return 5; }
    } catch (std.async::start_error error) {
        return 6;
    } finally {
        finalized += 1;
    }
    try {
        i32 value = await choose(true, 23, 29);
        if (value != 23) { return 7; }
    } catch (std.async::start_error error) { return 8; }
    if (finalized != 1) { return 9; }
    return 0;
}
