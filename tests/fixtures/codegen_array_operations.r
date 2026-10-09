module test.codegen.array_operations;

/* The wrapper fails the allocation of the first with_capacity and the growth of the second push;
   the program checks the errors, the staged value that the failed push hands back and that the
   array is unchanged (Library R-LIB-0019: push_error<T> carries the value, target unchanged). */

struct pair {
    u32 first;
    u16 second;
};

array<pair> make_pairs(usize capacity) throws std.alloc::alloc_error {
    array<pair> created =
        std.array::with_capacity::<pair>(capacity);
    return move created;
}

void push_pair(array<pair>* target, pair value) throws std.array::push_error<pair> {
    std.array::push(target, value);
}

i32 main() {
    try {
        array<pair> refused = make_pairs(1);
        drop refused;
        return 1;
    } catch (std.alloc::alloc_error error) {
        if (error != std.alloc::alloc_error::out_of_memory) {
            return 2;
        }
    }
    try {
        array<pair> values = std.array::with_capacity::<pair>(1);
        if ((len(values) != 0usize) || (std.array::capacity(&values) != 1usize)) {
            return 3;
        }
        pair value = {
            .first = 11,
            .second = 7,
        };
        std.array::push(&values, value);
        if ((len(values) != 1usize) || (values[0].first != 11u32) || (values[0].second != 7u16)) {
            return 4;
        }
        try {
            push_pair(&values, pair {.first = 29, .second = 13});
            return 5;
        } catch (std.array::push_error<pair> failure) {
            switch (move failure) {
                case variant std.array::push_error::allocation_failed(move payload):
                    if ((payload.reason != std.alloc::alloc_error::out_of_memory) ||
                        (payload.value.first != 29u32) || (payload.value.second != 13u16)) {
                        return 6;
                    }
                    break;
            }
        }
        if ((len(values) != 1usize) || (std.array::capacity(&values) != 1usize) ||
            (values[0].first != 11u32) || (values[0].second != 7u16)) {
            return 7;
        }
        return 0;
    } catch (std.array::push_error<pair> error) {
        error as void;
        return 8;
    } catch (std.alloc::alloc_error error) {
        error as void;
        return 9;
    }
}
