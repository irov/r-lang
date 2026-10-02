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
        task<i32> first = selected(move source, 0);
        i32 result = await move first;
        if (result != 7) { return 1; }
        task<i32> again = selected(move source, 0);
        i32 copied = await move again;
        if (copied != 7) { return 2; }
        own i32* a = new i32(11);
        own i32* b = new i32(13);
        Value<own i32*> moved = Value<own i32*>::Some(move a);
        task<own i32*> pick = selected(move moved, move b);
        own i32* owner = await move pick;
        if (*owner != 11) { return 3; }
        own i32* c = new i32(17);
        own i32* d = new i32(19);
        Failure<own i32*> left = Failure<own i32*> { .value = move c };
        Failure<own i32*> right = Failure<own i32*> { .value = move d };
        task<void throws Failure<own i32*>> failure = fail(false, move left, move right);
        await move failure;
        return 4;
    } catch (Failure<own i32*> caught) {
        if (*(caught.value) != 19) { return 5; }
    } catch (std.async::start_error error) {
        return 6;
    } finally {
        finalized += 1;
    }
    try {
        task<i32> operation = choose(true, 23, 29);
        i32 value = await move operation;
        if (value != 23) { return 7; }
    } catch (std.async::start_error error) { return 8; }
    if (finalized != 1) { return 9; }
    return 0;
}
