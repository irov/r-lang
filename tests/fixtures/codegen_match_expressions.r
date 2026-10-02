module test.matching;
enum Packet { Empty, Data { i32 amount; o<i32> extra; } };
i32 score(Packet packet) {
    return match (packet) {
        case variant Packet::Empty: 0;
        case variant Packet::Data { .amount = n, .extra = variant o::some(x) } if (x > 3): n + x;
        case variant Packet::Data { .amount = n }: n;
    };
}
i32 main() {
    if (score(Packet::Empty) != 0) { return 1; }
    if (score(Packet::Data {.amount=7,.extra=o::none}) != 7) { return 2; }
    if (score(Packet::Data {.amount=40,.extra=o::some(2)}) != 40) { return 3; }
    if (score(Packet::Data {.amount=38,.extra=o::some(4)}) != 42) { return 4; }
    return 0;
}
