module test.codegen.constant_contexts;

/* R-EXPR-0032, R-META-0003, R-NAME-0008: translation-time calls in required constants take a
   method of a const object, a const array as a slice, reflection over a type parameter and a
   static initializer of a generic definition, which each instance evaluates. */
struct Limits { u32 low; u32 high; };
u32 Limits::span(const Limits* this) { return this->high - this->low; }
const Limits LIMITS = Limits { .low = 10u32, .high = 250u32 };
const u32 SPAN = LIMITS.span();

u32 sum(const u32[] values) {
    u32 total = 0u32;
    for (usize i = 0usize; i < len(values); i++) { total += values[i]; }
    return total;
}
const u32[4] TABLE = {1u32, 2u32, 3u32, 4u32};
const u32 TOTAL = sum(&TABLE);

struct Pair { i32 a; i32 b; };
struct Triple { i32 a; i32 b; i32 c; };
enum Color { red, green, blue };

@generic<T>
struct Slots { u8[core::field_count::<T>()] slots; };
@generic<T>
struct Tags { u8[core::enum_count::<T>() * 2usize] tags; };

@generic<T>
usize classify() {
    @if (core::field_count::<T>() > 2usize) { return 3usize; } @else { return 1usize; }
}

@generic<T>
usize size_of() { return sizeof(T); }

@generic<T>
usize padded() {
    static const usize SIZE = size_of::<T>() + 1usize;
    return SIZE;
}

@generic<T>
usize doubled() {
    thread_local const usize SIZE = size_of::<T>() * 2usize;
    return SIZE;
}

@generic<T>
usize second_name_length() {
    static const str NAME = core::field_name::<T>(1usize);
    return len(NAME);
}

i32 main() {
    u8[LIMITS.span() as usize] spanned = {};
    u8[sum(&TABLE) as usize] summed = {};
    if (SPAN != 240u32 || len(spanned) != 240usize) { return 1; }
    if (TOTAL != 10u32 || len(summed) != 10usize) { return 2; }
    Slots<Pair> pair = {};
    Slots<Triple> triple = {};
    Tags<Color> tags = {};
    if (len(pair.slots) != 2usize || len(triple.slots) != 3usize) { return 3; }
    if (len(tags.tags) != 6usize) { return 4; }
    if (classify::<Triple>() != 3usize || classify::<Pair>() != 1usize) { return 5; }
    if (padded::<u32>() != 5usize || padded::<u64>() != 9usize) { return 6; }
    if (doubled::<u16>() != 4usize) { return 7; }
    if (second_name_length::<Triple>() != 1usize) { return 8; }
    return 0;
}
