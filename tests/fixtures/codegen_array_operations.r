module test.codegen.array_operations;

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
        array<pair> values = std.array::with_capacity::<pair>(1);
        pair value = {
            .first = 11,
            .second = 7,
        };
        std.array::push(&values, value);
        return 0;
    } catch (std.array::push_error<pair> error) {
        error as void;
        return 2;
    } catch (std.alloc::alloc_error error) {
        error as void;
        return 1;
    }
}
