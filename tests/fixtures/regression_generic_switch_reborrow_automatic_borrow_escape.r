module regression.generic_switch_reborrow_automatic_borrow_escape;

error Missing {
    Value,
};

@generic<T>
enum Choice {
    Value(T),
    Empty,
};

@generic<T>
const T* leak(Choice<T> choice) throws Missing {
    switch (choice) {
    case variant Choice<T>::Value(value):
        return &*value;
    case variant Choice<T>::Empty:
        throw Missing::Value;
    }
}

void instantiate() throws Missing {
    const i32* escaped = leak(Choice<i32>::Value(7));
}

i32 main() {
    return 0;
}
