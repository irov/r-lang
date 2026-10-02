module test.regression.array_push_move_value;

i32 main() {
    try {
        array<own i32*> owners = std.array::with_capacity::<own i32*>(1);
        own i32* owner = new i32(17);
        std.array::push(&owners, move owner);
        if (len(owners) != 1) {
            return 1;
        }
        return 0;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 2;
    } catch (std.array::push_error<own i32*> failure) {
        return 3;
    }
}
