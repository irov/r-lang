module audit.async_field_assignment_borrow_provenance_uaf;

struct Holder {
    const i32* alias;
};

async i32 main() {
    i32 initial = 0;
    Holder holder = Holder { .alias = &initial };
    own i32* owner = new i32(7);
    holder.alias = &*owner;
    drop owner;
    return *holder.alias;
}
