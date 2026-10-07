module test.codegen.consteval_owners;

/* L22.3 (R-FUNC-0023, Library R-LIB-0019..0023, R-SLIB-STRING-0001..0003): tagged values and
   standard owners as working values of translation-time evaluation. */
enum shape { circle(u32), square { u32 side; u32 depth; }, dot };

u32 area(shape value) {
    switch (value) {
    case variant shape::circle(radius):
        return *radius * 3u32;
    case variant shape::square(item):
        return item->side * item->depth;
    case variant shape::dot:
        return 0u32;
    }
}

u32 via_match(shape value) {
    u32 result = match (value) {
        case variant shape::circle(radius): radius;
        case variant shape::square {.side = side}: side;
        case variant shape::dot: 1u32;
    };
    return result;
}

u32 unwrap_or(o<u32> value, u32 fallback) {
    return match (value) {
        case variant o::some(inner): inner;
        case variant o::none: fallback;
    };
}

o<u32> find(u32[4] values, u32 wanted) {
    for (usize index = 0usize; index < 4usize; index += 1usize) {
        if (values[index] == wanted) {
            o<u32> found = o::some(index as u32);
            return found;
        }
    }
    o<u32> missing = o::none;
    return missing;
}

shape biggest(u32 limit) {
    if (limit > 10u32) {
        shape big = shape::square {.side = limit, .depth = 2u32};
        return big;
    }
    shape small = shape::circle(limit);
    return small;
}

const u32 AREA = area(shape::square {.side = 3u32, .depth = 5u32});
const u32 CIRCLE = area(shape::circle(4u32));
const u32 MATCHED = via_match(shape::square {.side = 7u32, .depth = 1u32});
const u32 UNWRAPPED = unwrap_or(o::none, 9u32);
const u32[4] FIRST = {5u32, 6u32, 7u32, 8u32};
const u32[4] SECOND = {1u32, 2u32, 3u32, 4u32};
const u32 FOUND = unwrap_or(find(FIRST, 7u32), 100u32);
const o<u32> OPTION = find(SECOND, 4u32);
const shape BIG = biggest(12u32);

array<u32> primes(u32 limit) throws std.alloc::alloc_error, std.array::push_error<u32> {
    array<u32> found = std.array::create::<u32>();
    for (u32 candidate = 2u32; candidate < limit; candidate += 1u32) {
        bool prime = true;
        for (usize index = 0usize; index < len(found); index += 1usize) {
            if (candidate % found[index] == 0u32) {
                prime = false;
                break;
            }
        }
        if (prime == true) {
            found.push(candidate);
        }
    }
    return move found;
}

u32 count_primes(u32 limit) {
    try {
        array<u32> found = primes(limit);
        u32 total = 0u32;
        for (u32 value in &found) {
            total += value;
        }
        o<u32> last = found.pop();
        u32 largest = match (last) {
            case variant o::some(value): value;
            case variant o::none: 0u32;
        };
        return (len(found) as u32) * 1000000u32 + total * 100u32 + largest;
    } catch (std.array::push_error<u32> failure) {
        return 0u32;
    } catch (std.alloc::alloc_error failure) {
        return 1u32;
    }
}

usize text_length(str first, str second) {
    try {
        std.string::string text = std.string::from_str(first);
        std.string::append_str(&text, second);
        std.string::push_scalar(&text, 'é');
        str view = text;
        usize size = len(view) * 10usize + std.string::len(&text);
        return size;
    } catch (std.alloc::alloc_error failure) {
        return 0usize;
    }
}

u16 port_of(str name) {
    try {
        dict<str, u16> ports = std.dict::create::<str, u16>();
        o<u16> replaced = ports.insert("http", 80u16);
        replaced as void;
        o<u16> again = ports.insert("https", 443u16);
        again as void;
        o<u16> twice = ports.insert("http", 8080u16);
        u16 old = match (twice) {
            case variant o::some(value): value;
            case variant o::none: 0u16;
        };
        o<const u16*> found = ports.get(&name);
        u16 port = match (found) {
            case variant o::some(value): *value;
            case variant o::none: 1u16;
        };
        u16 sum = (port as u32 + old as u32) as u16;
        return sum;
    } catch (std.dict::insert_error<str, u16> failure) {
        return 0u16;
    }
}

const u32 PRIME_DIGEST = count_primes(30u32);
const usize TEXT = text_length("ab", "cd");
const u16 HTTP = port_of("http");
const u16 HTTPS = port_of("https");
const u16 NONE = port_of("gopher");

u32 chain_sum(u32 count) {
    try {
        list<u32> chain = std.list::create::<u32>();
        for (u32 index = 0u32; index < count; index += 1u32) {
            u32* pushed = chain.push_back(index);
            *pushed += 1u32;
        }
        u32* front = chain.push_front(100u32);
        *front += 1u32;
        u32 total = 0u32;
        for (const u32* element in &chain) {
            total += *element;
        }
        o<u32> first = chain.pop_front();
        u32 popped = match (first) {
            case variant o::some(value): value;
            case variant o::none: 0u32;
        };
        return total * 1000u32 + popped;
    } catch (std.list::push_error<u32> failure) {
        return 0u32;
    }
}

u32 walk_dict() {
    try {
        dict<str, u32> table = std.dict::create::<str, u32>();
        o<u32> first = table.insert("one", 1u32);
        first as void;
        o<u32> second = table.insert("three", 3u32);
        second as void;
        u32 total = 0u32;
        for (std.dict::entry_ref<str, u32> item in &table) {
            total += *item.value * 100u32 + (len(*item.key) as u32);
        }
        str key = "one";
        o<u32> removed = table.remove(&key);
        u32 gone = match (removed) {
            case variant o::some(value): value;
            case variant o::none: 0u32;
        };
        return total * 10u32 + gone + (len(table) as u32) * 1000000u32;
    } catch (std.dict::insert_error<str, u32> failure) {
        return 0u32;
    }
}

usize words(str text) {
    try {
        array<std.string::string> parts = std.array::create::<std.string::string>();
        std.string::string current = std.string::create();
        for (usize index = 0usize; index < len(text); index += 1usize) {
            u8 byte = text[index];
            if (byte == 32u8) {
                parts.push(move current);
                current = std.string::create();
            } else {
                std.string::push_scalar(&current, byte as char);
            }
        }
        parts.push(move current);
        usize total = 0usize;
        for (usize index = 0usize; index < len(parts); index += 1usize) {
            total += std.string::len(&parts[index]);
        }
        return len(parts) * 100usize + total;
    } catch (std.array::push_error<std.string::string> failure) {
        return 0usize;
    } catch (std.alloc::alloc_error failure) {
        return 1usize;
    }
}

i32 cut(usize at) {
    try {
        std.string::string text = std.string::from_str("aé");
        std.string::truncate(&text, at);
        return std.string::len(&text) as i32;
    } catch (std.string::boundary_error failure) {
        return -1;
    } catch (std.alloc::alloc_error failure) {
        return -2;
    }
}

const u32 CHAIN = chain_sum(4u32);
const u32 WALK = walk_dict();
const usize WORDS = words("ab cde f");
const i32 CUT_OK = cut(1usize);
const i32 CUT_MIDDLE = cut(2usize);
const i32 CUT_FAR = cut(9usize);


u32 runtime_primes(u32 limit) {
    return count_primes(limit);
}

i32 main() {
    i32 failures = AREA == 15u32 ? 0 : 1;
    failures += CIRCLE == 12u32 ? 0 : 1;
    failures += MATCHED == 7u32 ? 0 : 1;
    failures += UNWRAPPED == 9u32 ? 0 : 1;
    failures += FOUND == 2u32 ? 0 : 1;
    failures += unwrap_or(OPTION, 0u32) == 3u32 ? 0 : 1;
    failures += area(BIG) == 24u32 ? 0 : 1;
    failures += PRIME_DIGEST == 9012929u32 ? 0 : 1;
    failures += runtime_primes(30u32) == PRIME_DIGEST ? 0 : 1;
    failures += TEXT == 66usize ? 0 : 1;
    failures += HTTP == 8160u16 ? 0 : 1;
    failures += HTTPS == 523u16 ? 0 : 1;
    failures += NONE == 81u16 ? 0 : 1;
    failures += CHAIN == 111101u32 ? 0 : 1;
    failures += WALK == 4081u32 + 1000000u32 ? 0 : 1;
    failures += WORDS == 306usize ? 0 : 1;
    failures += CUT_OK == 1 ? 0 : 1;
    failures += CUT_MIDDLE == -1 ? 0 : 1;
    failures += CUT_FAR == -1 ? 0 : 1;
    return failures;
}
