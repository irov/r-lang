module test.regression.payload_variant_borrow_owner_lifetime;

enum Mixed {
    Plain,
    Borrowed(const i32*),
};

i32 main() {
    own i32* owner = new i32(47);
    const i32* pointer = &*owner;
    Mixed mixed = Mixed::Borrowed(pointer);
    switch (move mixed) {
        case variant Mixed::Plain:
            return 1;
        case variant Mixed::Borrowed(value):
            drop owner;
            return **value - 47;
    }
}
