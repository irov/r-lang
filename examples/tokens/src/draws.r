module example.tokens.draws;
import std.encoding;
import std.random;

/* tokens bytes COUNT: COUNT bytes of the operating-system generator in hexadecimal. */
std.string::string fresh_bytes(usize count) throws std.alloc::alloc_error {
    bytes buffer = std.alloc::bytes(count, 0u8);
    std.random::fill(buffer.as_slice_mut());
    std.string::string hex = std.encoding::encode_hex(buffer.as_slice());
    return f"{hex}\n";
}

/* tokens pick LOW HIGH: one number of the operating-system generator in [LOW, HIGH), a number
   below 6 and the two raw words it draws from. */
std.string::string picked(u64 low, u64 high) throws std.alloc::alloc_error {
    u64 value = std.random::range(low, high);
    u64 die = std.random::below(6u64) + 1u64;
    u64 word = std.random::next_u64();
    u32 half = std.random::next_u32();
    bool wide = (word | (half as u64)) != 0u64;
    return f"value {value}\ndie {die}\nwords drawn {wide}\n";
}

/* tokens dice SEED COUNT: COUNT rolls of a die from a generator with that seed; the same seed
   gives the same rolls on every run and every implementation. */
std.string::string rolled(u64 seed, u32 count) throws std.alloc::alloc_error {
    std.random::generator dice = std.random::generator::seeded(seed);
    std.string::string out = std.string::from_str("rolls:");
    for (u32 index = 0u32; index < count; index += 1u32) {
        u64 roll = dice.range(1u64, 7u64);
        std.string::string item = f" {roll}";
        std.string::append_str(&out, item);
    }
    u64 word = dice.next_u64();
    u32 half = dice.next_u32();
    u64 tenth = dice.below(10u64);
    u8[4] noise = {};
    dice.fill(&noise);
    std.string::string hex = std.encoding::encode_hex(noise);
    std.string::string tail = f"\nnext {word} {half} {tenth} {hex}\n";
    std.string::append_str(&out, tail);
    return move out;
}

/* tokens shuffle SEED ITEM...: the items in the order of a seeded shuffle, and a shuffle from a
   generator seeded by the operating system that keeps every item. */
std.string::string shuffled(u64 seed, const str[] items) throws std.alloc::alloc_error {
    array<str> order = std.array::create::<str>();
    array<str> other = std.array::create::<str>();
    for (usize index = 0usize; index < len(items); index += 1usize) {
        try {
            std.array::push(&order, items[index]);
            std.array::push(&other, items[index]);
        } catch (std.array::push_error<str> failure) {
            failure as void;
            throw std.alloc::alloc_error::out_of_memory;
        }
    }
    std.random::generator mixer = std.random::generator::seeded(seed);
    str[] order_view = order.as_slice_mut();
    mixer.shuffle(order_view);
    std.random::generator fresh = std.random::generator::from_entropy();
    str[] other_view = other.as_slice_mut();
    fresh.shuffle(other_view);
    std.string::string out = std.string::create();
    for (usize index = 0usize; index < len(order); index += 1usize) {
        if (index != 0usize) { std.string::append_str(&out, " "); }
        std.string::append_str(&out, order[index]);
    }
    usize kept = len(other);
    std.string::string tail = f"\nkept {kept}\n";
    std.string::append_str(&out, tail);
    return move out;
}
