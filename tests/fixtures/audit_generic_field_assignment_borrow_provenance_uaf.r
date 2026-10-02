module audit.generic_field_assignment_borrow_provenance_uaf;

@generic<T>
struct Holder {
    const T* alias;
};

i32 main() {
    i32 initial = 0;
    Holder<i32> holder = Holder<i32> { .alias = &initial };
    own i32* owner = new i32(7);
    holder.alias = &*owner;
    drop owner;
    return *holder.alias;
}
