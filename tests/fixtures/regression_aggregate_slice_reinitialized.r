module regression.aggregate_slice_reinitialized;

struct Holder {
    const i32[] values;
};

protected i32 reinitialize(bool update) {
    i32[1] outer = {1};
    Holder holder = { .values = &outer };
    if (update == true) {
        {
            i32[1] inner = {7};
            holder = Holder { .values = &inner };
        }
        holder = Holder { .values = &outer };
    }
    return holder.values[0] - 1;
}

i32 main() {
    return reinitialize(true) + reinitialize(false);
}
