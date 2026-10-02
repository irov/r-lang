module test.codegen.async_array_slice;

async i32 main() {
    try {
        array<u8> values = std.array::with_capacity::<u8>(1);
        std.array::push(&values, 7);
        {
            u8[] mutable_view = std.array::as_slice_mut(&values);
            if ((len(mutable_view) != 1) || (mutable_view[0] != 7)) {
                return 1;
            }
        }
        {
            const u8[] shared_view = std.array::as_slice(&values);
            if ((len(shared_view) != 1) || (shared_view[0] != 7)) {
                return 2;
            }
        }
        {
            const u8[] ranged_view = values[0i32..1i32];
            if ((len(ranged_view) != 1) || (ranged_view[0] != 7)) {
                return 4;
            }
        }
        return 0;
    } catch (std.array::push_error<u8> error) {
        error as void;
        return 3;
    } catch (std.alloc::alloc_error error) {
        error as void;
        return 5;
    }
}
