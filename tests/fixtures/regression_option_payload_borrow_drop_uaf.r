module regression.option_payload_borrow_drop_uaf;

i32 main() {
    own i32* owner = new i32(29);
    const i32* source = &*owner;
    o<const i32*> borrowed = o::some(source);
    drop owner;
    switch (borrowed) {
    case variant o::some(move value):
        return *value - 29;
    case variant o::none:
        return 1;
    }
}
