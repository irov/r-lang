module test.codegen.borrowed_views;

// Retain values whose purpose here is type or lifetime coverage.
@generic<T> @noalloc @nonblocking
protected void test_observe(const T* value) { value as void; }

@generic<T> struct Held { own i32* owner; const T[] data; };
@generic<T> drop(Held<T>* self) { *(self->owner) = 9; }
@generic<T> Held<T> transfer(Held<T> value) { return move value; }
@generic<T> struct View { const T[] data; };
@generic<T> struct Pair { View<T> first; View<T> second; };
@generic<T> error Invalid { View<T> first; View<T> second; };
@generic<T>
View<T> view(const T[] data) { return View<T> {.data = data}; }
@generic<T>
View<T> checked_view(const T[] data) throws Invalid<T> {
    return View<T> {.data = data};
}
@generic<T>
Pair<T> pair(const T[] left, const T[] right) {
    View<T> first = view(left);
    View<T> second = view(right);
    return Pair<T> {.first = first, .second = second};
}
@generic<T>
void reject(const T[] left, const T[] right) throws Invalid<T> {
    View<T> first = view(left);
    View<T> second = view(right);
    throw Invalid<T> {.first = first, .second = second};
}
@generic<T>
void relay(const T[] left, const T[] right) throws Invalid<T> {
    try { reject(left, right); }
    catch (Invalid<T> failure) { throw; }
}
@generic<T, F: fn @noalloc @nonblocking(const T[]) -> View<T>>
@noalloc @nonblocking
View<T> decode(const F* operation, const T[] source) {
    View<T> result = operation(source);
    return result;
}
i32 run() {
    i32[2] left = {20, 21};
    i32[2] right = {22, 23};
    const i32[] left_data = left[0usize..2usize];
    Held<i32> first = {.owner = new i32(1), .data = left_data};
    Held<i32> held = transfer(move first);
    Held<i32> second = {.owner = new i32(2), .data = left_data};
    fn once Held<i32> pass(Held<i32> value) { return move value; }
    Held<i32> passed = pass(move second);
    test_observe(&passed);
    if (held.data[0] != 20 || passed.data[0] != 20) { return 5; }

    fn @noalloc @nonblocking View<i32> project(const i32[] data) {
        return View<i32> {.data = data};
    }
    View<i32> result = decode(&project, left);
    Pair<i32> both = pair(left, right);
    if (result.data[0] + both.second.data[0] != 42) { return 1; }
    fn i32 checked(const i32[] a, const i32[] b) throws Invalid<i32> {
        relay(a, b);
        return 0;
    }
    try {
        View<i32> validated = checked_view(left);
        if (validated.data[0] != 20) { return 4; }
        i32 value = checked(left, right);
        return value + 2;
    }
    catch (Invalid<i32> failure) {
        if (failure.first.data[0] + failure.second.data[0] != 42) { return 3; }
    }
    return 0;
}
i32 main() { i32 result = run(); return result; }
