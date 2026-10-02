module audit.borrowed_view_field_storage_reassignment;

struct View {
    const i32* alias;
};

i32 main() {
    i32 value = 7;
    i32 replacement = 11;
    View first = View { .alias = &value };
    const (const i32*)* slot = &first.alias;
    first.alias = &replacement;
    return **slot;
}
