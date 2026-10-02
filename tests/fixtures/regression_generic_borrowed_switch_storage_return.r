module regression.generic_borrowed_switch_storage_return;

error Missing {
    Value,
};

@generic<T>
enum Choice {
    Value(T),
    Empty,
};

@generic<T>
const T* get(const Choice<T>* choice) throws Missing {
    switch (*choice) {
    case variant Choice<T>::Value(value):
        return value;
    case variant Choice<T>::Empty:
        throw Missing::Value;
    }
}

void instantiate(const Choice<i32>* choice) throws Missing {
    const i32* value = get(choice);
    value as void;
}

i32 main() {
    return 0;
}
