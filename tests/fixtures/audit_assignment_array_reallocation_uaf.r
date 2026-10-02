module audit.assignment_array_reallocation_uaf;

protected i32 grow(array<i32>* target) throws std.array::push_error<i32> {
    std.array::push(target, 2);
    return 3;
}

i32 main() {
    try {
        array<i32> values = std.array::with_capacity::<i32>(1);
        std.array::push(&values, 1);
        i32[] destination = std.array::as_slice_mut(&values);
        destination[0] = grow(&values);
        const i32[] result = std.array::as_slice(&values);
        i32 status = result[0] == 3 && result[1] == 2 ? 0 : 2;
        drop values;
        return status;
    } catch (std.array::push_error<i32> error) {
        error as void;
        return 3;
    } catch (std.alloc::alloc_error error) {
        error as void;
        return 4;
    }
}
