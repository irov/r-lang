module test.codegen.deep_nesting;

/* Deeply nested control flow: every statement form the emitter wraps (calls with many
   arguments, struct literals, casts, subscripts, arithmetic, conditionals, method calls,
   format strings, continue inside for) at nesting depths where the C17 layout pass has to
   re-lay the lines the emitter wrote. */
struct Accumulator {
    u64 total;
    u32 rounds;
    i64 signed_total;
};

struct Pair {
    u32 left;
    u32 right;
};

u32 Accumulator::mix(const Accumulator* this, u32 salt, u32 shift) {
    u64 folded = this->total ^ ((this->rounds as u64) << (shift & 31u32));
    return ((folded & 0xffffffffu64) as u32) ^ salt;
}

u64 combine_many(u64 first, u64 second, u64 third, u64 fourth, u64 fifth, u64 sixth) {
    return first + second * 3u64 + third * 5u64 + fourth * 7u64 + fifth * 11u64 + sixth * 13u64;
}

Pair make_pair(u32 left, u32 right) {
    return Pair { .left = left ^ 0x5a5au32, .right = right + 1u32 };
}

i64 widen_negative(i32 value) {
    return -((value as i64) + 1i64);
}

i32 deep(u32 limit) throws std.alloc::alloc_error {
    Accumulator accumulator = Accumulator { .total = 1u64, .rounds = 0u32, .signed_total = 0i64 };
    u32[8] table = {1u32, 2u32, 3u32, 5u32, 8u32, 13u32, 21u32, 34u32};
    u32 checksum = 0u32;
    for (u32 outer = 0u32; outer < limit; outer += 1u32) {
        if ((outer % 2u32) == 1u32) { continue; }
        while (accumulator.rounds < outer + 2u32) {
            accumulator.rounds += 1u32;
            for (u32 inner = 0u32; inner < 3u32; inner += 1u32) {
                if (((inner + outer) % 3u32) == 2u32) { continue; }
                switch (inner) {
                case 0u32:
                    if (accumulator.total < 1000000u64) {
                        while (checksum < 40u32) {
                            checksum += 1u32;
                            for (usize slot = 0usize; slot < 8usize; slot += 1usize) {
                                if ((table[slot] % 2u32) == 0u32) { continue; }
                                if (table[slot] > 4u32) {
                                    switch (slot % 2usize) {
                                    case 0usize:
                                        for (u32 depth = 0u32; depth < 2u32; depth += 1u32) {
                                            if (depth == 1u32) { continue; }
                                            while (accumulator.signed_total < 50i64) {
                                                accumulator.signed_total -= widen_negative((depth as i32) + 2);
                                                if (accumulator.signed_total > 0i64) {
                                                    Pair pair = make_pair(table[slot] + checksum * 3u32 + depth, accumulator.mix(checksum ^ table[(slot + 1usize) % 8usize], depth + inner));
                                                    accumulator.total = combine_many(accumulator.total, pair.left as u64, pair.right as u64, (checksum as u64) * (depth as u64 + 1u64), table[slot] as u64, (accumulator.signed_total as u64) & 0xffffu64);
                                                    u32 selected = pair.left > pair.right ? pair.left - pair.right : pair.right - pair.left + (accumulator.rounds as u32);
                                                    checksum = (checksum ^ selected ^ accumulator.mix(selected, depth)) & 0x7fffffffu32;
                                                    std.string::string note = f"depth {depth} slot {slot} checksum {checksum} total {accumulator.total} signed {accumulator.signed_total}";
                                                    const u8[] note_bytes = std.string::as_bytes(&note);
                                                    if (len(note_bytes) == 0usize) { return 9; }
                                                    for (u32 last = 0u32; last < 2u32; last += 1u32) {
                                                        if (last == 0u32) { continue; }
                                                        Accumulator copy = Accumulator { .total = accumulator.total ^ (last as u64), .rounds = accumulator.rounds + last, .signed_total = accumulator.signed_total - (last as i64) };
                                                        checksum = (checksum + copy.mix(table[(slot + last as usize) % 8usize], last + depth + inner + outer)) % 1000003u32;
                                                    }
                                                }
                                            }
                                        }
                                        break;
                                    default:
                                        checksum += table[slot] * (slot as u32 + 1u32);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    break;
                case 1u32:
                    checksum = (checksum * 3u32 + inner) % 65521u32;
                    break;
                default:
                    checksum += 1u32;
                    break;
                }
            }
        }
    }
    i32 chosen = checksum == 0u32 ? 1 : 0;
    return chosen;
}

i32 main() {
    try {
        return deep(6u32);
    } catch (std.alloc::alloc_error failure) {
        return 8;
    }
}
