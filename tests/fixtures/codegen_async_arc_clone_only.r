module test.codegen.async_arc_clone_only;

/* P4.4-1, R-OWN-0020: the arc-only clone glue of an asynchronous program has no unused failure
   helper either. */

struct Counter { u64 value; };

async u64 read_twice(arc Counter shared) { return shared->value * 2u64; }

async i32 main() {
    arc Counter shared = new arc Counter {.value = 21u64};
    arc Counter copy = core::clone(&shared);
    i32 status = 0;
    if (std.arc::strong_count(&shared) != 2usize) { status += 1; }
    if ((await read_twice(move copy)) != 42u64) { status += 2; }
    if (std.arc::strong_count(&shared) != 1usize) { status += 4; }
    drop shared;
    return status;
}
