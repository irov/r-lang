module codegen.audit_disjoint_internal_field_assignment;

struct Holder {
    i32 value;
    const i32* alias;
    i32 counter;
};

i32 main() {
    i32 initial = 0;
    Holder holder = Holder { .value = 7, .alias = &initial, .counter = 0 };
    holder.alias = &holder.value;
    holder.counter = 1;
    i32 selected = *holder.alias == 7 && holder.counter == 1 ? 0 : 1;
    return selected;
}
