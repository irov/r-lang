module regression.option_payload_borrow_owner_lifetime;

i32 main() {
    own i32* owner = new i32(29);
    const i32* source = &*owner;
    o<const i32*> borrowed = o::some(source);
    switch (borrowed) {
    case variant o::some(move value):
        i32 result = *value - 29;
        drop owner;
        return result;
    case variant o::none:
        drop owner;
        return 1;
    }
}
