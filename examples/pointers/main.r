module examples.pointers.nullable;

struct State {
    own i32*? cached;
    raw i32*? external;
};

@generic<T>
struct Box {
    T value;
};

@generic<T>
const T* box_value(const Box<T>* box) {
    return &box->value;
}

@generic<T>
const T* first(const T[] values) {
    return &values[0];
}

@generic<T>
const T[] first_slice(const T[] values) {
    return values[0..1];
}

const i32*? find(bool available, const i32* value) {
    if (available == true) {
        return value;
    }
    return null;
}

i32 main() {
    i32 value = 42;
    const i32*? found = find(true, &value);
    State state = {};
    Box<i32> box = Box<i32> { .value = 8 };
    i32[1] values = {34};
    const i32[] view = &values;
    const i32* boxed = box_value(&box);
    const i32* first_value = first(view);
    const i32[] first_view = first_slice(view);
    i32 result = found != null ? *found : 0;
    i32 borrowed_sum = *boxed + *first_value + first_view[0] - 34;
    unsafe {
        bool raw_is_null = state.external == null;
        if (state.cached == null && raw_is_null == true && result == 42 && borrowed_sum == 42) {
            drop state;
            return 0;
        }
    }
    drop state;
    return 1;
}
