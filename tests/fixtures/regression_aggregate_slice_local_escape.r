module regression.aggregate_slice_local_escape;

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
    return holder.values[0];
}
