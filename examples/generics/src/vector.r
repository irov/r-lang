module example.generics.vector;

@generic<T>
struct Vector {
    array<T> storage;
};

@generic<T>
Vector<T> singleton(T value)
    throws std.alloc::alloc_error, std.array::push_error<T> {
    array<T> storage = std.array::with_capacity::<T>(1usize);
    storage.push(move value);
    return Vector<T> { .storage = move storage };
}

// T occurs only in the result, so a call names it: empty::<i32>().
@generic<T>
Vector<T> empty() {
    array<T> storage = std.array::create::<T>();
    return Vector<T> { .storage = move storage };
}

// Storing a caller's value through the target requires it to hold no borrow (R-BORROW-0019).
@generic<T: unborrowed>
void append(Vector<T>* target, T value) throws std.array::push_error<T> {
    std.array::push(&target->storage, move value);
}

@generic<T>
usize count(const Vector<T>* value) {
    usize length = len(value->storage);
    return length;
}
