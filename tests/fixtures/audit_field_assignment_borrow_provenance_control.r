module audit.field_assignment_borrow_provenance_control;

struct Holder {
    const i32* alias;
};

i32 main() {
    i32 initial = 0;
    Holder holder = Holder { .alias = &initial };
    own i32* owner = new i32(7);
    holder.alias = &*owner;
    i32 observed = *holder.alias;
    holder.alias = &initial;
    drop owner;
    i32 selected = observed == 7 && *holder.alias == 0 ? 0 : 1;
    return selected;
}
