module test.match_projection;
struct Reading { own i32* storage; i32 value; };
drop(Reading* self) { *(self->storage) = 9; }
Reading create(i32 value) { return Reading {.storage = new i32(0), .value = value}; }
i32 selected(bool flag) {
    return match(flag) {
        case true: create(42);
        case false: create(7);
    }.value;
}
i32 main() { i32 chosen = selected(true) == 42 && selected(false) == 7 ? 0 : 1; return chosen; }
