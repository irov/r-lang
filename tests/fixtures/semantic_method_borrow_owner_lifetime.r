module test.semantic.method_borrow_owner_lifetime;

struct Box {
    own i32* value;
};

const i32* Box::view(const Box* this) {
    return &*this->value;
}

i32 main() {
    Box box = Box { .value = new i32(9) };
    const i32* borrowed = box.view();
    drop box;
    return *borrowed;
}
