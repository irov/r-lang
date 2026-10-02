module test.call_projection;
struct Reading { own i32* storage; i32 value; };
drop(Reading* self) { *(self->storage) = 9; }
Reading create(i32 value) { return Reading {.storage = new i32(0), .value = value}; }
i32 selected(bool flag) {
    i32 value = flag == true ? 42 : 7;
    return create(value).value;
}
i32 main() { i32 chosen = selected(true) == 42 && selected(false) == 7 ? 0 : 1; return chosen; }
