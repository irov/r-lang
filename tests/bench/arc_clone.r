module bench.arc_clone;

/* Clone and drop an arc 50 000 000 times, reading through each clone. */
struct Counter { u64 value; };

u64 read_twice(arc Counter shared) { return shared->value * 2u64; }

i32 main() {
    arc Counter shared = new arc Counter { .value = 21u64 };
    u64 sum = 0u64;
    for (u32 i = 0u32; i < 50_000_000u32; i += 1u32) {
        arc Counter copy = core::clone(&shared);
        sum += read_twice(move copy) + (i as u64);
    }
    return (sum % 109u64) as i32;
}
