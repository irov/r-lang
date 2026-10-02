module test.regression.loop_condition_carried_borrow_uaf;

protected bool grow(array<i32>* values) throws std.array::push_error<i32> {
    std.array::push(values, 29);
    return true;
}

i32 main() {
    try {
        array<i32> first = std.array::with_capacity::<i32>(1);
        std.array::push(&first, 17);
        array<i32> second = std.array::with_capacity::<i32>(1);
        std.array::push(&second, 23);
        const i32[] selected = std.array::as_slice(&first);
        i32 index = 0;
        i32 sum = 0;
        while (index < 2 && grow(&second) == true) {
            sum += selected[0];
            selected = std.array::as_slice(&second);
            index += 1;
        }
        i32 chosen = sum == 40 ? 0 : 3;
        return chosen;
    } catch (std.array::push_error<i32> failure) {
        failure as void;
        return 2;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 1;
    }
}
