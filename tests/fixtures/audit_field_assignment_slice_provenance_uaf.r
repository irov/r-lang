module audit.field_assignment_slice_provenance_uaf;

struct Holder {
    array<i32> values;
    const i32[] view;
};

i32 main() {
    try {
        i32[1] initial = {0};
        array<i32> values = std.array::with_capacity::<i32>(1);
        std.array::push(&values, 7);
        Holder holder = Holder { .values = move values, .view = &initial };
        holder.view = std.array::as_slice(&holder.values);
        std.array::push(&holder.values, 9);
        return holder.view[0];
    } catch (std.array::push_error<i32> failure) {
        failure as void;
        return 1;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 2;
    }
}
