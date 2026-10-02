module test.regression.loop_exit_states;

i32 main() {
    own i32* skipped = new i32(11);
    while (false) {
        own i32* unreachable = move skipped;
    }
    if (*skipped != 11) {
        return 1;
    }

    own i32* exit_owner = new i32(17);
    while (true) {
        own i32* consumed = move exit_owner;
        if (*consumed != 17) {
            return 2;
        }
        break;
    }

    try {
        array<u8> values = std.array::with_capacity::<u8>(1);
        std.array::push(&values, 7);
        const u8[] view = std.array::as_slice(&values);
        while (true) {
            if (view[0] != 7) {
                return 3;
            }
            std.array::push(&values, 8);
            break;
        }
        if (len(values) != 2) {
            return 4;
        }
    } catch (std.array::push_error<u8> failure) {
        failure as void;
        return 5;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 6;
    }
    return 0;
}
