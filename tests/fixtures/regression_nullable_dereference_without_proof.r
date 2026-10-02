module regression.nullable_dereference_without_proof;

i32 read(const i32*? pointer) {
    return *pointer;
}
