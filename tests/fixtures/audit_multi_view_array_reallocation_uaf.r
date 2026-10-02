module audit.multi_view_array_reallocation_uaf;

struct Holder {
    array<i32> left;
    array<i32> right;
    const i32[] left_view;
    const i32[] right_view;
};

i32 main() {
    try {
        i32[1] initial_left = { 0 };
        i32[1] initial_right = { 0 };
        array<i32> left = std.array::with_capacity::<i32>(1);
        array<i32> right = std.array::with_capacity::<i32>(1);
        std.array::push(&left, 7);
        std.array::push(&right, 9);
        Holder holder = Holder {
            .left = move left,
            .right = move right,
            .left_view = initial_left[0..1],
            .right_view = initial_right[0..1],
        };
        holder.left_view = std.array::as_slice(&holder.left);
        holder.right_view = std.array::as_slice(&holder.right);
        std.array::reserve(&holder.left, 128usize);
        return holder.left_view[0];
    } catch (std.array::push_error<i32> failure) {
        failure as void;
        return 2;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 3;
    }
}
