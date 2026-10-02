module test.codegen.generic_vector_push_oom;

@generic<T>
struct Vector {
    array<T> storage;
};

@generic<T>
Vector<T> singleton(T value) throws std.alloc::alloc_error, std.array::push_error<T> {
    array<T> storage = std.array::with_capacity::<T>(1);
    std.array::push(&storage, move value);
    return Vector<T> { .storage = move storage };
}

@generic<T: unborrowed>
void append(Vector<T>* target, T value) throws std.array::push_error<T> {
    std.array::push(&target->storage, move value);
}

i32 main() {
    try {
        own i32* first = new i32(17);
        Vector<own i32*> owners = singleton(move first);
        own i32* second = new i32(29);
        try {
            append(&owners, move second);
            return 1;
        } catch (std.array::push_error<own i32*> failure) {
            if (len(owners.storage) != 1) {
                return 2;
            }
            switch (move failure) {
                case variant std.array::push_error::allocation_failed(move payload):
                    if (*(payload.value) != 29) {
                        return 3;
                    }
                    return 0;
            }
        }
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 4;
    } catch (std.array::push_error<own i32*> failure) {
        return 5;
    }
}
