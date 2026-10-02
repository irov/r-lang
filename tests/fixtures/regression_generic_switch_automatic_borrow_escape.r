module regression.generic_switch_automatic_borrow_escape;

error Missing {
    Value,
};

@generic<T>
const T* leak(T value) throws Missing {
    o<const T*> wrapped = o::some(&value);
    switch (move wrapped) {
    case variant o::some(move pointer):
        return pointer;
    case variant o::none:
        throw Missing::Value;
    }
}

void instantiate() throws Missing {
    const i32* escaped = leak(7);
}

i32 main() {
    return 0;
}
