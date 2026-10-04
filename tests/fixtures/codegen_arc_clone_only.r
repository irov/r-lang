module test.codegen.arc_clone_only;

/* P4.4-1, R-OWN-0020: when the only core::clone of a program is that of an arc, its clone glue
   cannot fail and the failure helper of the glue is left out, so the generated C has no unused
   function. */

struct Counter { u64 value; };

u64 read_twice(arc Counter shared) { return shared->value * 2u64; }

i32 main() {
    arc Counter shared = new arc Counter {.value = 21u64};
    arc Counter copy = core::clone(&shared);
    i32 status = 0;
    if (std.arc::strong_count(&shared) != 2usize) { status += 1; }
    if (read_twice(move copy) != 42u64) { status += 2; }
    if (std.arc::strong_count(&shared) != 1usize) { status += 4; }
    drop shared;
    return status;
}
