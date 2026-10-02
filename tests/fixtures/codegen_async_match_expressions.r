module test.async_matching;
enum Packet { Empty, Data { i32 amount; o<i32> extra; } };
async i32 identity(i32 value) { return value; }
async i32 score(Packet packet) throws std.async::start_error {
    return match (packet) {
        case variant Packet::Empty: 0;
        case variant Packet::Data { .amount = n, .extra = variant o::some(x) } if (x > 3): await identity(n + x);
        case variant Packet::Data { .amount = n }: await identity(n);
    };
}
async i32 main() {
    try {
    if (await score(Packet::Empty) != 0) { return 1; }
    if (await score(Packet::Data {.amount=7,.extra=o::none}) != 7) { return 2; }
    if (await score(Packet::Data {.amount=40,.extra=o::some(2)}) != 40) { return 3; }
    if (await score(Packet::Data {.amount=38,.extra=o::some(4)}) != 42) { return 4; }
    return 0;
    } catch (std.async::start_error failure) { return 90; }
}
