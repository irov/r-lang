module test.regression.dispatcher_without_targets;

/* M27-4: a function type that is called but that no function converts to got a dispatcher
   whose parameters no target reads, which the warnings of the C compile rejected. */

struct hooks { array<fn(u32, u32) -> u32 throws(std.alloc::alloc_error)> items; };

u32 run(const hooks* all) throws std.alloc::alloc_error {
    u32 total = 0u32;
    for (usize index = 0usize; index < len(all->items); index += 1usize) {
        auto hook = all->items[index];
        total += hook(1u32, 2u32);
    }
    return total;
}

i32 main() {
    hooks none = {.items = std.array::create::<fn(u32, u32) -> u32 throws(std.alloc::alloc_error)>()};
    try {
        return run(&none) as i32;
    } catch (std.alloc::alloc_error failed) {
        failed as void;
    }
    return 1;
}
