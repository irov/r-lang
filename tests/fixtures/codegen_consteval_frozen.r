module test.codegen.consteval_frozen;

/* L22.4 (R-EXPR-0032): owners computed during translation frozen into the program image as
   const module and static objects: arrays, strings, lists, dictionaries and structs of them. */
struct Service { std.string::string name; array<u16> ports; };

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

std.string::string banner(str name, u32 version) throws std.alloc::alloc_error {
    std.string::string text = std.string::from_str(name);
    std.string::append_str(&text, " v");
    std.string::push_scalar(&text, (48u32 + version) as char);
    return move text;
}

dict<str, u16> default_ports() throws std.alloc::alloc_error, std.dict::insert_error<str, u16> {
    dict<str, u16> ports = std.dict::create::<str, u16>();
    o<u16> first = ports.insert("http", 80u16);
    first as void;
    o<u16> second = ports.insert("https", 443u16);
    second as void;
    o<u16> third = ports.insert("ssh", 22u16);
    third as void;
    for (u16 extra = 0u16; extra < 20u16; extra += 1u16) {
        o<u16> ignored = ports.insert("gopher", (70u32 + extra as u32) as u16);
        ignored as void;
    }
    return move ports;
}

list<u32> countdown(u32 from) throws std.list::push_error<u32> {
    list<u32> values = std.list::create::<u32>();
    for (u32 value = 0u32; value < from; value += 1u32) {
        u32* stored = values.push_front(value);
        stored as void;
    }
    return move values;
}

array<std.string::string> names() throws std.alloc::alloc_error, std.array::push_error<std.string::string> {
    array<std.string::string> result = std.array::create::<std.string::string>();
    result.push(std.string::from_str("ada"));
    result.push(std.string::from_str("grace"));
    return move result;
}

Service service() throws std.alloc::alloc_error, std.array::push_error<u16> {
    array<u16> ports = std.array::create::<u16>();
    ports.push(8080u16);
    ports.push(8443u16);
    Service made = Service {.name = std.string::from_str("web"), .ports = move ports};
    return move made;
}

u32 total(const array<u32>* values) {
    u32 sum = 0u32;
    for (u32 value in values) {
        sum += value;
    }
    return sum;
}

const array<u32> PRIMES = primes(30u32);
const std.string::string BANNER = banner("r", 7u32);
const dict<str, u16> PORTS = default_ports();
const list<u32> COUNTDOWN = countdown(4u32);
const array<std.string::string> NAMES = names();
const Service SERVICE = service();
const u32 PRIME_SUM = total(&PRIMES);

u16 port_of(str name) {
    o<const u16*> found = PORTS.get(&name);
    return match (found) {
        case variant o::some(port): *port;
        case variant o::none: 0u16;
    };
}

enum color : i8 { red = -2, green = 0, blue = 5 };

dict<i32, u8> signed_table() throws std.alloc::alloc_error, std.dict::insert_error<i32, u8> {
    dict<i32, u8> table = std.dict::create::<i32, u8>();
    for (i32 key = -20; key < 20; key += 3) {
        o<u8> old = table.insert(key, (key + 30) as u8);
        old as void;
    }
    return move table;
}

dict<char, u32> char_table() throws std.alloc::alloc_error, std.dict::insert_error<char, u32> {
    dict<char, u32> table = std.dict::create::<char, u32>();
    o<u32> a = table.insert('a', 1u32);
    a as void;
    o<u32> e = table.insert('é', 2u32);
    e as void;
    return move table;
}

dict<color, bool> color_table() throws std.alloc::alloc_error, std.dict::insert_error<color, bool> {
    dict<color, bool> table = std.dict::create::<color, bool>();
    o<bool> r = table.insert(color::red, true);
    r as void;
    o<bool> b = table.insert(color::blue, false);
    b as void;
    return move table;
}

dict<i64, u8> empty_table() {
    dict<i64, u8> table = std.dict::create::<i64, u8>();
    return move table;
}

const dict<i32, u8> SIGNED = signed_table();
const dict<char, u32> CHARS = char_table();
const dict<color, bool> COLORS = color_table();
const dict<i64, u8> EMPTY = empty_table();

u8 lookup(i32 key) {
    o<const u8*> found = SIGNED.get(&key);
    return match (found) {
        case variant o::some(value): *value;
        case variant o::none: 0u8;
    };
}

i32 main() {
    static const array<u32> small = primes(10u32);
    i32 failures = len(PRIMES) == 10usize ? 0 : 1;
    failures += PRIMES[9usize] == 29u32 ? 0 : 1;
    failures += total(&PRIMES) == 129u32 && PRIME_SUM == 129u32 ? 0 : 1;
    str view = std.string::as_str(&BANNER);
    failures += len(view) == 4usize && view[3usize] == 55u8 ? 0 : 1;
    failures += port_of("https") == 443u16 && port_of("ssh") == 22u16 ? 0 : 1;
    failures += port_of("gopher") == 89u16 && port_of("ftp") == 0u16 ? 0 : 1;
    failures += len(PORTS) == 4usize ? 0 : 1;
    u32 order = 0u32;
    for (const u32* value in &COUNTDOWN) {
        order = order * 10u32 + *value;
    }
    failures += order == 3210u32 ? 0 : 1;
    failures += len(NAMES) == 2usize && std.string::len(&NAMES[1usize]) == 5usize ? 0 : 1;
    failures += std.string::len(&SERVICE.name) == 3usize && SERVICE.ports[1usize] == 8443u16 ? 0 : 1;
    failures += len(small) == 4usize && small[3usize] == 7u32 ? 0 : 1;
    u32 keys = 0u32;
    for (std.dict::entry_ref<str, u16> entry in &PORTS) {
        keys += len(*entry.key) as u32;
    }
    failures += keys == 18u32 ? 0 : 1;
    failures += lookup(-20) == 10u8 && lookup(-2) == 28u8 && lookup(19) == 49u8 ? 0 : 1;
    failures += lookup(-1) == 0u8 && lookup(18) == 0u8 ? 0 : 1;
    char accent = 'é';
    failures += CHARS.contains(&accent) == true && len(CHARS) == 2usize ? 0 : 1;
    color red = color::red;
    color green = color::green;
    failures += COLORS.contains(&red) == true && COLORS.contains(&green) == false ? 0 : 1;
    i64 zero = 0i64;
    failures += EMPTY.contains(&zero) == false && len(EMPTY) == 0usize ? 0 : 1;
    return failures;
}
