module test.codegen.scoped_cancel;
struct Owner { own i32* value; };
drop(Owner* self) {
    if (*(self->value) != 43) { panic("child cleanup did not precede owner cleanup"); }
    *(self->value) = 9;
}
async void pause() {}
@scoped
async void child(i32* value) throws std.async::start_error {
    try { await pause(); }
    finally { *value = 43; }
}
// The task that the C wrapper sees cancelled while it waits in group.all() and its child
// still borrows the owner's storage (R-STMT-0017).
async i32 scenario() {
    Owner owner = Owner {.value = new i32(42)};
    try {
        task_scope(1) group {
            auto operation = child(&*owner.value);
            await group.all();
            await move operation;
        }
    } catch (std.async::start_error failure) { return 1; }
    return 0;
}
async i32 main() {
    try {
        task_scope(1) outer {
            auto operation = scenario();
            // The wrapper holds this cancel until the child has entered its await and scenario
            // is suspended; outer.all() then waits for the whole cancellation to finish.
            std.async::cancel(move operation);
            await outer.all();
        }
    } catch (std.async::start_error failure) { return 2; }
    return 0;
}
