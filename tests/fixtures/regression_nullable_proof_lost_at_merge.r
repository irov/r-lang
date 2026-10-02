module regression.nullable_proof_lost_at_merge;

i32 read(const i32*? pointer, bool inspect) {
    if (inspect == true) {
        if (pointer != null) {
            inspect = false;
        }
    }
    return *pointer;
}
