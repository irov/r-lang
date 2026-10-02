module audit.update_field_borrow_provenance_conflict;

struct Holder {
    i32 value;
    const i32* alias;
};

i32 main() {
    i32 initial = 0;
    Holder holder = Holder { .value = 7, .alias = &initial };
    holder.alias = &holder.value;
    holder.value++;
    return *holder.alias;
}
