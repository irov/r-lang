module tests.std.cmp;
import std.test;
import std.cmp;

// The tests of std.cmp (Library R-SLIB-CMP-0001..0002), run in test mode (Core R-FUNC-0025).

/* A key with a tag that the order ignores, so that a test can tell which of two equal arguments
   a helper returned. */
struct keyed { i32 key; i32 tag; };

impl std.cmp::Ordered for keyed {
    std.cmp::ordering cmp(const keyed* this, const keyed* other) {
        return this->key.cmp(&other->key);
    }
};

/* A program type whose traits come from its fields in declaration order (Core R-AGG-0012). */
@derive(equal, ordered)
struct release { u32 major; u32 minor; str channel; };

/* A program function constrained by the trait. */
@generic<T: std.cmp::Ordered>
protected const T* largest(const T* first, const T* second, const T* third) {
    const T* best = std.cmp::max(first, second);
    return std.cmp::max(best, third);
}

/* Appends the number, turning a failed growth into its allocation error. */
protected void push(array<i32>* target, i32 value) throws std.alloc::alloc_error {
    try {
        target->push(value);
    } catch (std.array::push_error<i32> failure) {
        switch (move failure) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

@test
void orders_scalars_by_value() throws std.test::failure, std.alloc::alloc_error {
    i32 negative = -4;
    i32 positive = 9;
    std.test::check(negative.cmp(&positive) == std.cmp::ordering::less, "-4 before 9");
    std.test::check(positive.cmp(&negative) == std.cmp::ordering::greater, "9 after -4");
    std.test::check(positive.cmp(&positive) == std.cmp::ordering::equal, "9 equals 9");
    std.test::check(negative.eq(&positive) == false, "-4 is not 9");
    u8 zero = 0u8;
    u8 top = 255u8;
    std.test::check(top.cmp(&zero) == std.cmp::ordering::greater, "u8 by value");
    i64 far = -9000000000i64;
    i64 near = 1i64;
    std.test::check(far.cmp(&near) == std.cmp::ordering::less, "i64 across zero");
    u64 huge = 18446744073709551615u64;
    u64 one = 1u64;
    std.test::check(huge.cmp(&one) == std.cmp::ordering::greater, "u64 at its upper end");
    usize count = 3usize;
    usize other_count = 3usize;
    std.test::check(count.eq(&other_count), "usize equality");
    char letter = 'a';
    char euro = '€';
    std.test::check(letter.cmp(&euro) == std.cmp::ordering::less, "char by scalar value");
    std.test::check(euro.eq(&euro), "char equality");
    bool no = false;
    bool yes = true;
    std.test::check(no.cmp(&yes) == std.cmp::ordering::less, "false before true");
    std.test::check(yes.cmp(&no) == std.cmp::ordering::greater, "true after false");
    std.test::check(yes.cmp(&yes) == std.cmp::ordering::equal, "true equals true");
    std.test::check(no.eq(&yes) == false, "false differs from true");
}

@test
void orders_floats_with_nan_last() throws std.test::failure, std.alloc::alloc_error {
    f64 nan = 0.0 / 0.0;
    f64 other_nan = -(0.0 / 0.0);
    f64 big = 1.0e300;
    f64 infinity = 1.0 / 0.0;
    f64 minus_infinity = -1.0 / 0.0;
    std.test::check(big.cmp(&nan) == std.cmp::ordering::less, "a number before NaN");
    std.test::check(infinity.cmp(&nan) == std.cmp::ordering::less, "infinity before NaN");
    std.test::check(nan.cmp(&minus_infinity) == std.cmp::ordering::greater,
                    "NaN after every number");
    std.test::check(nan.cmp(&other_nan) == std.cmp::ordering::equal, "NaN equal to every NaN");
    std.test::check(minus_infinity.cmp(&big) == std.cmp::ordering::less, "by value");
    f64 zero = 0.0;
    f64 minus_zero = -0.0;
    std.test::check(zero.cmp(&minus_zero) == std.cmp::ordering::equal, "0.0 and -0.0 order equal");
    std.test::check(zero.eq(&minus_zero), "0.0 and -0.0 are equal values");
    std.test::check(nan.eq(&nan) == false, "equality is value equality: NaN is no value");
    f32 small_nan = 0.0f32 / 0.0f32;
    f32 small = -2.5f32;
    f32 smaller = -3.5f32;
    std.test::check(small_nan.cmp(&small) == std.cmp::ordering::greater, "f32 NaN last");
    std.test::check(smaller.cmp(&small) == std.cmp::ordering::less, "f32 by value");
    std.test::check(small.eq(&small), "f32 equality");
}

@test
void orders_text_by_bytes() throws std.test::failure, std.alloc::alloc_error {
    str apple = "apple";
    str app = "app";
    str banana = "banana";
    str empty = "";
    str zebra = "Zebra";
    std.test::check(apple.cmp(&banana) == std.cmp::ordering::less, "apple before banana");
    std.test::check(app.cmp(&apple) == std.cmp::ordering::less, "a prefix orders first");
    std.test::check(apple.cmp(&app) == std.cmp::ordering::greater, "the longer text last");
    std.test::check(empty.cmp(&app) == std.cmp::ordering::less, "the empty text first");
    std.test::check(empty.cmp(&empty) == std.cmp::ordering::equal, "empty equals empty");
    std.test::check(zebra.cmp(&apple) == std.cmp::ordering::less, "Z (0x5A) before a (0x61)");
    str latin = "z";
    str accented = "é";
    str symbol = "€";
    str emoji = "😀";
    std.test::check(latin.cmp(&accented) == std.cmp::ordering::less, "z before é");
    std.test::check(accented.cmp(&symbol) == std.cmp::ordering::less, "é before €");
    std.test::check(symbol.cmp(&emoji) == std.cmp::ordering::less, "€ before U+1F600");
    str apple_again = "apple";
    std.test::check(apple.eq(&apple_again), "equal bytes");
    std.test::check(apple.eq(&app) == false, "a prefix is not equal");
    std.test::check(std.cmp::is_equal(&apple, &apple_again), "is_equal over text");
}

@test
void orders_options_none_first() throws std.test::failure, std.alloc::alloc_error {
    o<i32> nothing = o::none;
    o<i32> other_nothing = o::none;
    o<i32> small = o::some(-5);
    o<i32> large = o::some(3);
    o<i32> small_again = o::some(-5);
    std.test::check(nothing.cmp(&small) == std.cmp::ordering::less, "none before some");
    std.test::check(small.cmp(&nothing) == std.cmp::ordering::greater, "some after none");
    std.test::check(small.cmp(&large) == std.cmp::ordering::less, "some by its value");
    std.test::check(nothing.cmp(&other_nothing) == std.cmp::ordering::equal, "none equals none");
    std.test::check(nothing.eq(&other_nothing), "none eq none");
    std.test::check(nothing.eq(&small) == false, "none differs from some");
    std.test::check(small.eq(&small_again), "equal payloads");
    std.test::check(small.eq(&large) == false, "different payloads");
    o<str> word = o::some("b");
    o<str> earlier = o::some("a");
    std.test::check(earlier.cmp(&word) == std.cmp::ordering::less, "some text by its bytes");
}

@test
void compares_sequences_element_by_element() throws std.test::failure, std.alloc::alloc_error {
    i32[3] first = {1, 2, 3};
    i32[3] second = {1, 2, 4};
    i32[3] same = {1, 2, 3};
    std.test::check(first.cmp(&second) == std.cmp::ordering::less, "the first difference decides");
    std.test::check(second.cmp(&first) == std.cmp::ordering::greater, "and in reverse");
    std.test::check(first.eq(&same), "equal fixed arrays");
    std.test::check(first.eq(&second) == false, "different fixed arrays");
    const i32[] prefix = first[0usize..2usize];
    const i32[] whole = first[0usize..3usize];
    const i32[] nothing = first[0usize..0usize];
    std.test::check(prefix.cmp(&whole) == std.cmp::ordering::less, "a prefix orders first");
    std.test::check(whole.cmp(&prefix) == std.cmp::ordering::greater, "the longer slice last");
    std.test::check(nothing.cmp(&prefix) == std.cmp::ordering::less, "the empty slice first");
    std.test::check(nothing.eq(&nothing), "empty slices are equal");
    std.test::check(prefix.eq(&whole) == false, "a prefix is not equal");
    const i32[] tail = second[1usize..3usize];
    std.test::check(tail.cmp(&whole) == std.cmp::ordering::greater, "2 after 1 at the start");
    (i32, str) left = (1, "b");
    (i32, str) right = (1, "c");
    (i32, str) bigger = (2, "a");
    std.test::check(left.cmp(&right) == std.cmp::ordering::less, "the second element breaks a tie");
    std.test::check(right.cmp(&bigger) == std.cmp::ordering::less, "the first element decides");
    std.test::check(left.eq(&left), "a tuple equals itself");
    std.test::check(left.eq(&right) == false, "tuples differ");
    (bool, char, u8) triple = (true, 'x', 7u8);
    (bool, char, u8) lower = (true, 'x', 6u8);
    std.test::check(lower.cmp(&triple) == std.cmp::ordering::less, "the last element decides");
}

@test
void helpers_return_the_chosen_argument() throws std.test::failure, std.alloc::alloc_error {
    keyed first = {.key = 5, .tag = 1};
    keyed second = {.key = 5, .tag = 2};
    keyed low = {.key = 0, .tag = 3};
    keyed high = {.key = 10, .tag = 4};
    std.test::equal(std.cmp::min(&first, &second)->tag, 1);
    std.test::equal(std.cmp::max(&first, &second)->tag, 1);
    std.test::equal(std.cmp::min(&high, &low)->tag, 3);
    std.test::equal(std.cmp::max(&low, &high)->tag, 4);
    keyed below = {.key = -3, .tag = 5};
    keyed above = {.key = 30, .tag = 6};
    keyed at_low = {.key = 0, .tag = 7};
    keyed at_high = {.key = 10, .tag = 8};
    std.test::equal(std.cmp::clamp(&below, &low, &high)->tag, 3);
    std.test::equal(std.cmp::clamp(&above, &low, &high)->tag, 4);
    std.test::equal(std.cmp::clamp(&first, &low, &high)->tag, 1);
    std.test::equal(std.cmp::clamp(&at_low, &low, &high)->tag, 7);
    std.test::equal(std.cmp::clamp(&at_high, &low, &high)->tag, 8);
    std.test::check(std.cmp::is_less(&low, &high), "0 is less than 10");
    std.test::check(std.cmp::is_less(&first, &second) == false, "equal keys are not less");
    std.test::check(std.cmp::is_less(&high, &low) == false, "10 is not less than 0");
    i32 three = 3;
    i32 other_three = 3;
    i32 four = 4;
    std.test::check(std.cmp::is_equal(&three, &other_three), "3 equals 3");
    std.test::check(std.cmp::is_equal(&three, &four) == false, "3 is not 4");
    std.test::equal(*largest(&three, &four, &other_three), 4);
}

@test
void derived_orders_follow_the_fields() throws std.test::failure, std.alloc::alloc_error {
    release stable = {.major = 1u32, .minor = 4u32, .channel = "stable"};
    release beta = {.major = 1u32, .minor = 4u32, .channel = "beta"};
    release next = {.major = 1u32, .minor = 10u32, .channel = "alpha"};
    release copy = {.major = 1u32, .minor = 4u32, .channel = "stable"};
    std.test::check(beta.cmp(&stable) == std.cmp::ordering::less, "the text field breaks a tie");
    std.test::check(stable.cmp(&next) == std.cmp::ordering::less, "minor 4 before minor 10");
    std.test::check(stable.eq(&copy), "equal fields");
    std.test::check(stable.eq(&beta) == false, "a different channel");
    const release* newest = largest(&stable, &next, &beta);
    std.test::equal(newest->minor, 10u32);
    const release* oldest = std.cmp::min(&stable, &beta);
    std.test::equal_text(oldest->channel, "beta");
}

@test(allocations)
void compares_owned_strings_and_arrays() throws std.test::failure, std.alloc::alloc_error {
    std.string::string pear = std.string::from_str("pear");
    std.string::string pearl = std.string::from_str("pearl");
    std.string::string again = std.string::from_str("pear");
    std.test::check(pear.cmp(&pearl) == std.cmp::ordering::less, "an owned prefix first");
    std.test::check(pearl.cmp(&pear) == std.cmp::ordering::greater, "the longer string last");
    std.test::check(pear.eq(&again), "equal owned strings");
    std.test::check(pear.eq(&pearl) == false, "different owned strings");
    array<i32> small = std.array::create::<i32>();
    array<i32> large = std.array::create::<i32>();
    array<i32> short = std.array::create::<i32>();
    push(&small, 1);
    push(&small, 2);
    push(&large, 1);
    push(&large, 3);
    push(&short, 1);
    std.test::check(small.cmp(&large) == std.cmp::ordering::less, "arrays element by element");
    std.test::check(short.cmp(&small) == std.cmp::ordering::less, "a shorter prefix array first");
    std.test::check(small.eq(&small), "an array equals itself");
    std.test::check(small.eq(&large) == false, "different arrays");
}
