module test.codegen.error_borrow_components;

struct Owner { own i32* value; };
drop(Owner* self) { *(self->value) = 9; }
error Pair { const i32* first; const i32* second; };

void reject(const i32* first, const i32* second) throws Pair {
    throw Pair {.first = first, .second = second};
}

void forward(const i32* first, const i32* second) throws Pair {
    try { reject(first, second); }
    catch (Pair failure) { throw; }
}

i32 main() {
    Owner first = {.value = new i32(20)};
    Owner second = {.value = new i32(22)};
    try { forward(&*(first.value), &*(second.value)); }
    catch (Pair pair) {
        i32 first_value = *pair.first;
        const i32* remaining = pair.second;
        drop first;
        i32 result = first_value + *remaining;
        drop second;
        i32 selected = result == 42 ? 0 : 1;
        return selected;
    }
    return 2;
}
