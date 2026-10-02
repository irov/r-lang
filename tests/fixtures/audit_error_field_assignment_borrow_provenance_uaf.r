module audit.error_field_assignment_borrow_provenance_uaf;

error Failure {
    const i32* alias;
};

i32 main() {
    i32 initial = 0;
    Failure failure = Failure { .alias = &initial };
    own i32* owner = new i32(7);
    failure.alias = &*owner;
    drop owner;
    return *failure.alias;
}
