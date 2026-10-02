module test.codegen.async_expected_types;

/* R-TYPE-0036 (L17.1): an awaited generic async call expects the result of its await, and an
   async call that is not awaited expects its task. */
@generic<T: send & unborrowed>
protected async o<T> later_none() {
    return o::none;
}

@generic<T: copy & send & unborrowed>
protected async array<T> later_array(T value) throws std.alloc::alloc_error,
                                                     std.array::push_error<T> {
    array<T> items = std.array::create();
    std.array::push(&items, value);
    return move items;
}

protected async i32 tick(i32 value) {
    return value + 1;
}

protected async i32 work() throws std.async::start_error,
                                  std.alloc::alloc_error,
                                  std.array::push_error<i32> {
    o<i32> awaited = await later_none();
    task<o<u16>> pending = later_none();
    o<u16> second = await move pending;
    i32 step = await tick(0);
    array<i32> built = await later_array(9);
    array<u8> empty = std.array::create();
    i32 status = 0;
    switch (move awaited) {
    case variant o::some(move v): status = 1; v as void; break;
    case variant o::none: break;
    }
    switch (move second) {
    case variant o::some(move v): status = 2; v as void; break;
    case variant o::none: break;
    }
    if (len(built) != 1usize) { status = 3; }
    if (len(empty) != 0usize) { status = 4; }
    move built as void;
    move empty as void;
    return status + step - 1;
}

async i32 main() {
    try {
        return await work();
    } catch (std.async::start_error failure) {
        failure as void;
        return 90;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 91;
    } catch (std.array::push_error<i32> failure) {
        failure as void;
        return 92;
    }
}
