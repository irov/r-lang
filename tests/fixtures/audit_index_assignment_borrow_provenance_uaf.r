module audit.index_assignment_borrow_provenance_uaf;

struct Holder {
    const i32* alias;
};

i32 main() {
    i32 initial = 0;
    Holder[1] holders = {Holder { .alias = &initial }};
    own i32* owner = new i32(7);
    holders[0] = Holder { .alias = &*owner };
    drop owner;
    return *holders[0].alias;
}
