module std.alloc;

/* R-SLIB-ALLOC-0003: the R part of std.alloc, loaded by `import std.alloc;`: the limits of a budget
   block (Core R-STMT-0020). */

/* R-SLIB-ALLOC-0003: the limits of a budget: the most bytes that the allocations charged to it may
   hold at once and the most tasks that may count under it at once; none sets no limit of that
   kind. */
struct limits {
    o<usize> bytes = o::none;
    o<usize> tasks = o::none;
};

/* R-SLIB-ALLOC-0004: the budget of the calling task, read from the runtime through the native
   provider std.alloc.native; budgets exist in async code only. */
@if (core::profile is hosted-native-async) {
    @link(name = "std.alloc.native", kind = "static")
    @header("r_std_alloc_native.h")
    extern "C" {
        @safety("ALLOC-USAGE", "every output addresses one writable c_uint64")
        c_int32 r_std_alloc_native_usage(raw c_uint64* bytes,
                                         raw c_uint64* byte_limit,
                                         raw c_uint64* tasks,
                                         raw c_uint64* task_limit,
                                         raw c_uint64* bytes_available);
    }

    /* R-SLIB-ALLOC-0004: the counters of a budget now, its limits and the bytes that it and every
       enclosing budget still admit; none where no limit applies. */
    struct usage {
        usize bytes;
        usize tasks;
        o<usize> byte_limit;
        o<usize> task_limit;
        o<usize> bytes_available;
    };

    /* The runtime reports no limit as the largest u64. */
    protected o<usize> limit_of(c_uint64 value) {
        u64 plain = value as u64;
        if (plain == 18446744073709551615u64) { return o::none; }
        return o::some(plain as usize);
    }

    /* R-SLIB-ALLOC-0004: the usage of the budget under which the calling code runs, none outside
       every budget block. */
    o<usage> budget_usage() {
        c_uint64 bytes = 0u64 as c_uint64;
        c_uint64 byte_limit = 0u64 as c_uint64;
        c_uint64 tasks = 0u64 as c_uint64;
        c_uint64 task_limit = 0u64 as c_uint64;
        c_uint64 available = 0u64 as c_uint64;
        unsafe {
            raw c_uint64* bytes_out = &bytes as raw c_uint64*;
            raw c_uint64* byte_limit_out = &byte_limit as raw c_uint64*;
            raw c_uint64* tasks_out = &tasks as raw c_uint64*;
            raw c_uint64* task_limit_out = &task_limit as raw c_uint64*;
            raw c_uint64* available_out = &available as raw c_uint64*;
            c_int32 status = r_std_alloc_native_usage(bytes_out, byte_limit_out, tasks_out,
                                                      task_limit_out, available_out);
            if (status as i32 == 0i32) { return o::none; }
        }
        return o::some(usage {
            .bytes = bytes as u64 as usize,
            .tasks = tasks as u64 as usize,
            .byte_limit = limit_of(byte_limit),
            .task_limit = limit_of(task_limit),
            .bytes_available = limit_of(available)
        });
    }
}
