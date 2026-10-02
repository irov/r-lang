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
async i32 main() {
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
