module test.codegen.constexpr_string_pool;

import test.codegen.constexpr_string_pool_support::{support_use};

/* Reading thread-local state keeps sync_use a run-time function (R-FUNC-0023), so its string
   literals stay in the generated program. */
thread_local i32 pool_status = 0;

protected i32 sync_use() {
    constexpr str first = "pool-shared";
    constexpr str second = "pool-shared";
    usize first_length = len(first);
    usize second_length = len(second);
    if (first_length == second_length) {
        return pool_status;
    } else {
        return 1;
    }
}

protected async i32 async_use() {
    constexpr str value = "pool-shared";
    str view = value;
    usize length = len(view);
    if (length == 11) {
        return 0;
    } else {
        return 1;
    }
}

protected i32 unreachable_use() {
    constexpr str value = "pool-dead";
    usize length = len(value);
    if (length == 9) {
        return 0;
    } else {
        return 1;
    }
}

async i32 main() {
    i32 sync_status = sync_use();
    if (sync_status != 0) {
        return 1;
    }
    i32 support_status = support_use();
    if (support_status != 0) {
        return 3;
    }
    try {
        task<i32> operation = async_use();
        i32 async_status = await move operation;
        return async_status;
    } catch (std.async::start_error error) {
        error as void;
        return 2;
    }
}
