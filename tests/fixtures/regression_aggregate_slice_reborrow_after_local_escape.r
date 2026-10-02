module regression.aggregate_slice_reborrow_after_local_escape;

struct Holder {
    const i32[] values;
};

i32 main() {
    i32[1] outer = {1};
    Holder holder = { .values = &outer };
    {
        i32[1] inner = {7};
        holder = Holder { .values = &inner };
    }
    const Holder* borrowed = &holder;
    return borrowed->values[0];
}
