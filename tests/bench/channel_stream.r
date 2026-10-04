module bench.channel_stream;

/* A producer task sends 2 000 000 values through an unbounded std.sync channel to the awaiting
   consumer, which sums them. */
async void produce(std.sync::sender<u32> sender, u32 count) {
    for (u32 value = 0u32; value < count; value += 1u32) {
        std.sync::send_result<u32> result = std.sync::send(&sender, value);
        switch (move result) {
        case variant std.sync::send_result::sent: break;
        case variant std.sync::send_result::disconnected(move lost): lost as void;
        case variant std.sync::send_result::allocation_failed(move lost): lost as void;
        }
    }
}

async i32 main() {
    std.sync::channel<u32> factory = std.sync::channel::<u32>();
    std.sync::sender<u32> sender = std.sync::sender(&factory);
    task<void> producer = produce(move sender, 2_000_000u32);
    std.async::detach(move producer);
    std.sync::receiver<u32> inbox = std.sync::receiver(move factory);
    u32 total = 0u32;
    while (true) {
        o<u32> next = await inbox.receive();
        bool finished = false;
        switch (next) {
        case variant o::some(value): total += *value; break;
        case variant o::none: finished = true; break;
        }
        if (finished == true) { break; }
    }
    return (total % 109u32) as i32;
}
