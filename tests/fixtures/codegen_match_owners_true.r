module test.match_owners_true;
struct Owner { own i32* value; };
drop(Owner* self) { *(self->value) = 9; }
enum Packet { Empty, Data { Owner keep; o<Owner> spare; } };
Owner select(Packet packet) {
    return match (move packet) {
        case variant Packet::Empty: panic("missing packet");
        case variant Packet::Data { .keep = move kept, .spare = variant o::some(move spare) }
            if (*(kept.value) > 50): move spare;
        case variant Packet::Data { .keep = move kept }: move kept;
    };
}
i32 main() {
    Owner first = {.value = new i32(99)};
    Owner second = {.value = new i32(42)};
    Packet packet = Packet::Data {.keep = move first, .spare = o::some(move second)};
    Owner chosen = select(move packet);
    if (*(chosen.value) != 42) { return 1; }
    return 0;
}
