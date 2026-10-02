module test.codegen.async_borrow_components;

struct Owner { own i32* value; };
drop(Owner* self) { *(self->value) = 9; }
struct Pair { const i32* first; const i32* second; };

Pair views(const i32* first, const i32* second) {
    return Pair {.first = first, .second = second};
}

struct Envelope { Pair pair; };
Pair extract(Envelope envelope) { return envelope.pair; }
const i32* choose(Pair pair) { return pair.second; }
(const i32*)[2] array_views(const i32* first, const i32* second) {
    (const i32*)[2] values = {first, second};
    return values;
}

async i32 ready() { return 0; }
async i32 main() {
    try {
        if (await ready() != 0) { return 3; }
    } catch (std.async::start_error failure) { return 4; }
    Owner first = {.value = new i32(20)};
    Owner second = {.value = new i32(22)};
    Pair pair = views(&*(first.value), &*(second.value));
    i32 first_value = *pair.first;
    const i32* remaining = pair.second;
    Envelope envelope = {.pair = pair};
    Pair extracted = extract(envelope);
    const i32* selected = choose(extracted);
    (const i32*)[2] indexed = array_views(&*(first.value), &*(second.value));
    const i32* last = indexed[1];
    drop first;
    i32 result = first_value + *remaining;
    if (*selected != 22 || *last != 22 || *envelope.pair.second != 22 || *indexed[1] != 22) { return 2; }
    drop second;
    i32 chosen = result == 42 ? 0 : 1;
    return chosen;
}
