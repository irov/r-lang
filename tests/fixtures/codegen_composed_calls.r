module test.codegen.composed_calls;

error Rejected {};
struct Owner { own i32* value; };
drop(Owner* self) { *(self->value) = 9; }

Owner create(i32 value) { return Owner {.value = new i32(value)}; }
i32 checked(bool reject) throws Rejected {
    throw (reject == true) Rejected {};
    return 20;
}
i32 consume(Owner owner, i32 value) { return *owner.value + value; }
i32 next(i32* sequence) { *sequence += 1; return *sequence; }
i32 pair(i32 first, i32 second) { return first * 10 + second; }

i32 main() {
    i32 sequence = 0;
    if (pair(next(&sequence), next(&sequence)) != 12) { return 1; }
    try {
        if (consume(create(22), checked(false)) != 42) { return 2; }
        return consume(create(1), checked(true));
    } catch (Rejected failure) {
        i32 selected = sequence == 2 ? 0 : 3;
        return selected;
    }
}
