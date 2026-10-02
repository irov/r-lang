module regression.nullable_proof_invalidated_by_assignment;

i32 read(const i32*? pointer) {
    if (pointer != null) {
        pointer = null;
        return *pointer;
    }
    return 0;
}
