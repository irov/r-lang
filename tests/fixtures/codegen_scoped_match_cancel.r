module test.codegen.scoped_match_cancel;
struct Owner { own i32* value; };
drop(Owner* self) {
    if (*(self->value) != 43) { panic("child cleanup did not precede owner cleanup"); }
    *(self->value) = 9;
}
struct Cargo { own i32* value; };
drop(Cargo* self) { *(self->value) = 9; }
enum Packet { Empty, Data { Cargo cargo; i32 amount; } };
async i32 pause(i32 value) { return value; }
@scoped
async void child(i32* value, Packet packet) throws std.async::start_error {
    try {
        i32 total = match(move packet) {
            case variant Packet::Empty: 0;
            case variant Packet::Data {.cargo = move cargo, .amount = amount}:
                await pause(*cargo.value + amount);
        };
        if (total != 42) { panic("incorrect matched payload"); }
    } finally { *value = 43; }
}
async i32 main() {
    Owner owner = Owner {.value = new i32(42)};
    Packet packet = Packet::Data {.cargo = Cargo {.value = new i32(20)}, .amount = 22};
    try {
        task_scope(1) group {
            auto operation = child(&*owner.value, move packet);
            await group.all();
            await move operation;
        }
    } catch (std.async::start_error failure) { return 1; }
    return 0;
}
