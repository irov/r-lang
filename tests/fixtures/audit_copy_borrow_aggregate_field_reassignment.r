module audit.copy_borrow_aggregate_field_reassignment;

struct View {
    const i32* alias;
};

i32 main() {
    i32 value = 7;
    i32 replacement = 11;
    View first = View { .alias = &value };
    View second = first;
    first.alias = &replacement;
    return *second.alias - 7;
}
