module regression.nullable_ambiguous_short_circuit;

i32 read(const i32*? pointer, bool fallback) {
    if (pointer == null || fallback == true) {
        return *pointer;
    }
    return 0;
}
