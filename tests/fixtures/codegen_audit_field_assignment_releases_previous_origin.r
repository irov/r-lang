module codegen.audit_field_assignment_releases_previous_origin;

struct Holder {
    const i32* alias;
};

i32 main() {
    own i32* first = new i32(7);
    own i32* second = new i32(9);
    Holder holder = Holder { .alias = &*first };
    holder.alias = &*second;
    drop first;
    i32 selected = *holder.alias == 9 ? 0 : 1;
    return selected;
}
