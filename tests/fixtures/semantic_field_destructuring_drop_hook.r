module test.semantic.field_destructuring_drop_hook;

/* R-STMT-0022: a value with a user drop hook is not taken apart, since the hook would not run. */
struct Guarded { i32 value; };
drop(Guarded* self) { self->value = 0; }

i32 main() {
    Guarded guarded = {.value = 3};
    auto {.value} = move guarded;
    return 0;
}
