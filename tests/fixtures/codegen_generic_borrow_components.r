module test.codegen.generic_borrow_components;

struct Owner { own i32* value; };
drop(Owner* self) { *(self->value) = 9; }
@generic<T>
struct Pair { T first; T second; };

@generic<T: copy>
Pair<T> views(T first, T second) {
    return Pair<T> {.first = first, .second = second};
}

i32 main() {
    Owner first = {.value = new i32(20)};
    Owner second = {.value = new i32(22)};
    const i32* first_view = &*(first.value);
    const i32* second_view = &*(second.value);
    Pair<const i32*> pair = views(first_view, second_view);
    i32 first_value = *pair.first;
    const i32* remaining = pair.second;
    drop first;
    i32 result = first_value + *remaining;
    drop second;
    i32 selected = result == 42 ? 0 : 1;
    return selected;
}
