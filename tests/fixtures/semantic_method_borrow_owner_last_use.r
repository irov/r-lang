module test.semantic.method_borrow_owner_last_use;

struct Box {
    own i32* value;
};

const i32* Box::view(const Box* this) {
    return &*this->value;
}

i32 main() {
    Box box = Box { .value = new i32(9) };
    const i32* borrowed = box.view();
    i32 value = *borrowed;
    drop box;
    i32 selected = value == 9 ? 0 : 1;
    return selected;
}
