module test.codegen.async_json_decode;

// Shared mutable storage preserves the update and cleanup scenario under test.
struct TestStorage1 { User value; };
struct TestStorage2 { FailingDefault value; };
struct TestStorage3 { Collections value; };

// Retain values whose purpose here is type or lifetime coverage.
@generic<T> @noalloc @nonblocking
protected void test_observe(const T* value) { value as void; }


struct Address { @json(case = "ignore") std.string::string city; };
struct Identity { @json(string) u64 id; };
struct Extras { @json(embed) dict<std.string::string, i32> extra; };
struct Envelope {
    @json(embed) Address address;
    @json(embed) Identity identity;
    @json(embed) std.json::value extra;
};

struct Collections {
    list<std.string::string> names;
    dict<std.string::string, list<i32>> values;
    @json(optional, omitempty) list<i32> empty_list;
    @json(optional, omitzero) dict<std.string::string, i32> empty_dict;
};

struct User {
    @json(string)
    u64 id;
    @json(name = "display_name", case = "ignore")
    std.string::string name;
    @json(optional, omitempty)
    std.string::string email;
    o<i32> score;
    bool active;
    char initial;
};

struct Omission {
    @json(omitempty) i32 zero;
    @json(omitempty) bool flag;
    @json(omitzero) i32 removed;
    @json(omitnone) o<i32> absent;
    @json(omitnone) o<i32> kept_zero;
    @json(omitnone) o<std.string::string> kept_empty;
    @json(skip) i32 secret;
};

struct Empty {};
struct NestedEmpty { @json(omitempty) Empty empty; };
@generic<T> struct Box { T value; };
enum Signed : i64 { Min = -9223372036854775808, Zero = 0, Max = 9223372036854775807, };
@generic<T> error Problem { Empty, Bad(T), Fields { T value; }, };


protected i32 default_counter(i32 increment) {
    static i32 calls = 0;
    unsafe { calls += increment; return calls; }
}
protected i32 next_default() { i32 result = default_counter(1); return result; }
protected std.string::string owned_default() throws std.alloc::alloc_error {
    std.string::string result = std.string::from_str("factory");
    return move result;
}
protected i32 failing_default() throws std.json::error, std.alloc::alloc_error {
    i32 result = std.json::unmarshal("true");
    return result;
}
struct OrdinaryDefault { bytes data; @json(optional, default = next_default) i32 number; };
struct MissingAggregate { @json(optional) OrdinaryDefault nested; };
struct Defaults {
    @json(optional, default = -3) i32 signed_value;
    @json(optional, default = 18446744073709551615u64) u64 maximum;
    @json(optional, default = true) bool flag;
    @json(optional, default = 'A') char initial;
    @json(optional, default = 1.25) f64 fraction;
    @json(optional, default = "unknown") std.string::string text;
    @json(optional, default = owned_default) std.string::string owned;
    @json(optional, default = next_default) i32 first;
    @json(skip, default = next_default) i32 second;
};
struct RequiredDefault {
    @json(optional, default = next_default) i32 first;
    i32 required;
};
struct FailingDefault {
    std.string::string name;
    @json(optional, default = failing_default) i32 failure;
};


struct CustomNumber { u64 value; };
std.json::value CustomNumber::json_marshal(const CustomNumber* value)
    throws std.json::error, std.alloc::alloc_error {
    i32 calls = default_counter(1);
    calls as void;
    std.string::string encoded = std.json::marshal(&value->value);
    const u8[] source = encoded;
    std.json::value result = std.json::parse(source);
    return move result;
}
CustomNumber CustomNumber::json_unmarshal(const std.json::value* value)
    throws std.json::error, std.alloc::alloc_error {
    std.string::string encoded = std.json::stringify(value);
    const u8[] source = encoded;
    u64 number = std.json::unmarshal(source);
    CustomNumber result = {.value = number};
    return result;
}
bool CustomNumber::json_is_zero(const CustomNumber* value) {
    i32 calls = default_counter(1);
    calls as void;
    return value->value == 42;
}
struct Hooks {
    @json(string) CustomNumber number;
    @json(omitzero, omitempty) CustomNumber skipped;
};
struct EmptyCustom { std.string::string text; };
std.json::value EmptyCustom::json_marshal(const EmptyCustom* value)
    throws std.json::error, std.alloc::alloc_error {
    const u8[] text = value->text;
    std.json::value result = std.json::from_string(text);
    return move result;
}
struct CustomOmission { @json(omitempty) EmptyCustom empty; };
@generic<T> struct GenericHook { T value; };
@generic<T> std.json::value GenericHook<T>::json_marshal(const GenericHook<T>* value) {
    std.json::value result = std.json::null();
    return move result;
}


@generic<T: json_encode>
std.string::string encode_generic(const T* value) throws std.json::error, std.alloc::alloc_error {
    std.string::string result = std.json::marshal(value);
    return move result;
}
@generic<T: json_decode>
void decode_generic(T* destination, const u8[] source) throws std.json::error, std.alloc::alloc_error {
    T result = std.json::unmarshal(source);
    *destination = move result;
}
@generic<T: json_encode>
struct Delegating { T value; };
@generic<T: json_encode>
std.json::value Delegating<T>::json_marshal(const Delegating<T>* value)
    throws std.json::error, std.alloc::alloc_error {
    std.string::string encoded = std.json::marshal(&value->value);
    const u8[] bytes = encoded;
    std.json::value result = std.json::parse(bytes);
    return move result;
}


struct CountedJson { std.string::string name; };
drop(CountedJson* self) {
    i32 calls = default_counter(1);
    calls as void;
}
struct PartialJson { CountedJson child; i32 required; };
drop(PartialJson* self) {
    i32 calls = default_counter(100);
    calls as void;
}


struct DynamicJson {
    std.json::value tree;
    @json(string) std.json::number exact;
    @json(omitzero) std.json::number zero;
};

protected bool matches(const std.string::string* source, const u8[] expected) {
    const u8[] actual = *source;
    usize size = len(actual);
    usize other = len(expected);
    if (size != other) { return false; }
    usize index = 0;
    while (index < size) {
        if (actual[index] != expected[index]) { return false; }
        index += 1;
    }
    return true;
}

protected async i32 exercise() throws std.json::error, std.alloc::alloc_error {
    TestStorage1 storage_user = {.value = std.json::unmarshal("{\"id\":\"18446744073709551615\",\"DISPLAY-NAME\":\"Alice\",\"score\":null,\"active\":true,\"initial\":\"A\"}")};
    if (storage_user.value.id != 18446744073709551615) { return 1; }
    bool correct = matches(&storage_user.value.name, "Alice");
    if (correct == false) { return 2; }
    bool correct_2 = matches(&storage_user.value.email, "");
    if (correct_2 == false) { return 3; }
    if (storage_user.value.active == false) { return 4; }
    char initial = storage_user.value.initial;
    std.string::string initial_text = f"{initial}";
    bool correct_3 = matches(&initial_text, "A");
    if (correct_3 == false) { return 5; }
    std.string::string encoded = std.json::marshal(&storage_user.value);
    bool correct_4 = matches(&encoded, "{\"id\":\"18446744073709551615\",\"display_name\":\"Alice\",\"score\":null,\"active\":true,\"initial\":\"A\"}");
    if (correct_4 == false) { return 16; }
    const u8[] encoded_bytes = encoded;
    User roundtrip = std.json::unmarshal(encoded_bytes);
    if (roundtrip.id != storage_user.value.id) { return 17; }
    std.string::string empty_omission = std.string::from_str("");
    Omission omission = {
        .zero = 0, .flag = false, .removed = 0, .absent = o::none,
        .kept_zero = o::some(0), .kept_empty = o::some(move empty_omission), .secret = 42,
    };
    std.string::string omitted = std.json::marshal(&omission);
    bool correct_5 = matches(&omitted, "{\"zero\":0,\"flag\":false,\"kept_zero\":0,\"kept_empty\":\"\"}");
    if (correct_5 == false) { return 18; }
    i32 caught = 0;
    try { storage_user.value = std.json::unmarshal("{\"id\":\"2\",\"display_name\":\"New\",\"score\":null,\"active\":false,\"initial\":\"too long\"}"); }
    catch (std.json::error error) { caught += 1; }
    finally { caught += 1; }
    if (caught != 2) { return 6; }
    bool correct_6 = matches(&storage_user.value.name, "Alice");
    if (correct_6 == false) { return 7; }
    try { User missing = std.json::unmarshal("{}");
    test_observe(&missing); }
    catch (std.json::error error) { caught += 1; }
    if (caught != 3) { return 8; }
    try { User duplicate = std.json::unmarshal("{\"id\":\"1\",\"display_name\":\"x\",\"DISPLAY_NAME\":\"y\"}");
    test_observe(&duplicate); }
    catch (std.json::error error) { caught += 1; }
    if (caught != 4) { return 9; }
    std.json::options options = {
        .max_depth = 256, .max_value_bytes = 67108864, .indent = 2,
        .reject_unknown_fields = true, .ignore_case = false,
        .mode = std.json::mode::document,
    };
    try { User unknown = std.json::unmarshal_with_options("{\"unknown\":1}", options);
    test_observe(&unknown); }
    catch (std.json::error error) {
        if (error.code != std.json::error_code::unknown_field) { return 12; }
        correct_6 = matches(&error.pointer, "/unknown");
        if (correct_6 == false) { return 13; }
        caught += 1;
    }
    if (caught != 5) { return 14; }
    std.json::value tree = std.json::parse_with_options("[1,2]", options);
    std.string::string pretty = std.json::stringify_with_options(&tree, options);
    bool correct_7 = matches(&pretty, "[\n  1,\n  2\n]");
    if (correct_7 == false) { return 15; }
    i64 minimum = std.json::unmarshal("-9223372036854775808");
    if (minimum != -9223372036854775808) { return 10; }
    f64 decimal = std.json::unmarshal("1.25e2");
    std.string::string decimal_text = f"{decimal:.1}";
    bool correct_8 = matches(&decimal_text, "125.0");
    if (correct_8 == false) { return 11; }
    std.string::string decimal_json = std.json::marshal(&decimal);
    bool correct_9 = matches(&decimal_json, "125");
    if (correct_9 == false) { return 19; }
    c_long_double wide = std.json::unmarshal("1.25e2");
    std.string::string wide_json = std.json::marshal(&wide);
    bool correct_10 = matches(&wide_json, "125");
    if (correct_10 == false) { return 57; }
    Box<i32> boxed = std.json::unmarshal("{\"value\":42}");
    std.string::string boxed_json = std.json::marshal(&boxed);
    bool correct_11 = matches(&boxed_json, "{\"value\":42}");
    if (correct_11 == false) { return 20; }
    Signed signed_enum = std.json::unmarshal("\"Min\"");
    std.string::string signed_json = std.json::marshal(&signed_enum);
    bool correct_12 = matches(&signed_json, "\"Min\"");
    if (correct_12 == false) { return 21; }
    NestedEmpty empty = std.json::unmarshal("{\"empty\":{}}");
    std.string::string empty_json = std.json::marshal(&empty);
    bool correct_13 = matches(&empty_json, "{}");
    if (correct_13 == false) { return 22; }
    Problem<std.string::string> problem = std.json::unmarshal("{\"Bad\":\"owned error\"}");
    i32 observed = 0;
    try { throw move problem; }
    catch (Problem<std.string::string> failure) {
        std.string::string error_json = std.json::marshal(&failure);
        correct_13 = matches(&error_json, "{\"Bad\":\"owned error\"}");
        if (correct_13 == false) { return 23; }
        observed += 1;
    }
    if (observed != 1) { return 24; }
    Problem<std.string::string> fields = std.json::unmarshal("{\"Fields\":{\"value\":\"named payload\"}}");
    std.string::string fields_json = std.json::marshal(&fields);
    bool correct_14 = matches(&fields_json, "{\"Fields\":{\"value\":\"named payload\"}}");
    if (correct_14 == false) { return 25; }
    array<std.string::string> strings = std.json::unmarshal("[\"alpha\",\"beta\"]");
    std.string::string array_json = std.json::marshal(&strings);
    bool correct_15 = matches(&array_json, "[\"alpha\",\"beta\"]");
    if (correct_15 == false) { return 26; }
    bytes binary = std.json::unmarshal("[0,127,255]");
    std.string::string binary_json = std.json::marshal(&binary);
    bool correct_16 = matches(&binary_json, "[0,127,255]");
    if (correct_16 == false) { return 27; }
    return 0;
}

protected async i32 exercise_defaults() throws std.json::error, std.alloc::alloc_error {
    bool correct = false;
    i32 before_ordinary = default_counter(0);
    MissingAggregate missing_aggregate = std.json::unmarshal("{}");
    test_observe(&missing_aggregate);
    i32 after_ordinary = default_counter(0);
    if (after_ordinary != before_ordinary || missing_aggregate.nested.number != 0) { return 66; }
    std.bytes::append_u8(&missing_aggregate.nested.data, 7);
    std.string::string ordinary_text = std.json::marshal(&missing_aggregate);
    bool correct_17 = matches(&ordinary_text, "{\"nested\":{\"data\":[7],\"number\":0}}");
    if (correct_17 == false) { return 67; }
    i32 before_defaults = default_counter(0);
    Defaults defaults = std.json::unmarshal("{}");
    test_observe(&defaults);
    i32 after_defaults = default_counter(0);
    if (after_defaults != before_defaults + 2 || defaults.first != before_defaults + 1 || defaults.second != after_defaults) { return 28; }
    if (defaults.signed_value != -3 || defaults.maximum != 18446744073709551615 || defaults.flag == false) { return 29; }
    bool correct_18 = matches(&defaults.text, "unknown");
    if (correct_18 == false) { return 30; }
    bool correct_19 = matches(&defaults.owned, "factory");
    if (correct_19 == false) { return 31; }
    std.string::string fraction_json = std.json::marshal(&defaults.fraction);
    bool correct_20 = matches(&fraction_json, "1.25");
    if (correct_20 == false) { return 32; }
    Defaults supplied = std.json::unmarshal("{\"first\":100,\"second\":100,\"text\":\"provided\"}");
    test_observe(&supplied);
    i32 after_supplied = default_counter(0);
    if (after_supplied != after_defaults + 1 || supplied.first != 100 || supplied.second != after_supplied) { return 33; }
    bool correct_21 = matches(&supplied.text, "provided");
    if (correct_21 == false) { return 34; }
    try { RequiredDefault missing = std.json::unmarshal("{}");
    missing as void; }
    catch (std.json::error failure) { }
    i32 after_missing = default_counter(0);
    if (after_missing != after_supplied) { return 35; }
    TestStorage2 storage_retained = {.value = std.json::unmarshal("{\"name\":\"original\",\"failure\":7}")};
    try { storage_retained.value = std.json::unmarshal("{\"name\":\"replacement\"}"); }
    catch (std.json::error failure) { }
    bool correct_22 = matches(&storage_retained.value.name, "original");
    if (correct_22 == false || storage_retained.value.failure != 7) { return 36; }
    return 0;
}

protected async i32 exercise_hooks() throws std.json::error, std.alloc::alloc_error {
    bool correct = false;
    i32 caught = 0;
    Hooks hooked = std.json::unmarshal("{\"number\":\"18446744073709551615\",\"skipped\":42}");
    i32 before_hooks = default_counter(0);
    std.string::string hooked_json = std.json::marshal(&hooked);
    test_observe(&hooked_json);
    i32 after_hooks = default_counter(0);
    if (after_hooks != before_hooks + 2) { return 37; }
    bool correct_23 = matches(&hooked_json, "{\"number\":\"18446744073709551615\"}");
    if (correct_23 == false) { return 38; }
    try { Hooks wrong = std.json::unmarshal("{\"number\":\" 42\",\"skipped\":0}");
    wrong as void; }
    catch (std.json::error error) { caught += 1; }
    if (caught != 1) { return 39; }
    std.string::string empty_owned = std.string::from_str("");
    EmptyCustom empty_custom = {.text = move empty_owned};
    CustomOmission custom_omission = {.empty = move empty_custom};
    std.string::string custom_empty_json = std.json::marshal(&custom_omission);
    bool correct_24 = matches(&custom_empty_json, "{}");
    if (correct_24 == false) { return 40; }
    GenericHook<i32> generic_hook = {.value = 1};
    std.string::string generic_hook_json = std.json::marshal(&generic_hook);
    bool correct_25 = matches(&generic_hook_json, "null");
    if (correct_25 == false) { return 41; }
    std.string::string via_generic = encode_generic(&hooked);
    bool correct_26 = matches(&via_generic, "{\"number\":\"18446744073709551615\"}");
    if (correct_26 == false) { return 42; }
    i32 generic_number = 1;
    decode_generic(&generic_number, "43");
    if (generic_number != 43) { return 43; }
    Delegating<i32> delegate = {.value = 44};
    std.string::string delegated_json = std.json::marshal(&delegate);
    bool correct_27 = matches(&delegated_json, "44");
    if (correct_27 == false) { return 44; }
    i32 before_partial = default_counter(0);
    try { PartialJson partial = std.json::unmarshal("{\"child\":{\"name\":\"owned\"},\"required\":true}");
    test_observe(&partial); }
    catch (std.json::error error) { }
    i32 after_partial = default_counter(0);
    if (after_partial != before_partial + 1) { return 45; }
    return 0;
}

protected async i32 exercise_tree() throws std.json::error, std.alloc::alloc_error {
    bool correct = false;
    DynamicJson dynamic = std.json::unmarshal("{\"tree\":{\"items\":[1,null,true]},\"exact\":\"1.2300e+99\",\"zero\":-0.00e-999}");
    std.string::string dynamic_json = std.json::marshal(&dynamic);
    bool correct_28 = matches(&dynamic_json, "{\"tree\":{\"items\":[1,null,true]},\"exact\":\"1.2300e+99\"}");
    if (correct_28 == false) { return 46; }
    {
        o<const std.json::value*> found = std.json::find(&dynamic.tree, "items");
        switch (found) {
        case variant o::some(move items):
            usize count = std.json::len(items);
            if (count != 3) { return 47; }
            o<const std.json::value*> child = std.json::get(items, 1);
            switch (child) {
            case variant o::some(move value):
                std.json::value_kind kind = std.json::kind(value);
                if (kind != std.json::value_kind::null) { return 48; }
                break;
            case variant o::none:
                return 49;
            }
            break;
        case variant o::none:
            return 50;
        }
    }
    std.json::value taken = std.json::take_field(&dynamic.tree, "items");
    std.json::value last = std.json::take_index(&taken, 2);
    bool truth = std.json::boolean(&last);
    if (truth == false) { return 51; }
    std.json::append(&taken, move last);
    std.json::insert(&dynamic.tree, "again", move taken);
    {
        str key = std.json::key_at(&dynamic.tree, 0);
        bool same = std.json::name_equal(key, "AG_A-IN", true);
        if (same == false) { return 52; }
    }
    std.string::string tree_after = std.json::stringify(&dynamic.tree);
    bool correct_29 = matches(&tree_after, "{\"again\":[1,null,true]}");
    if (correct_29 == false) { return 53; }
    std.json::value null_value = std.json::null();
    try { std.json::append(&dynamic.tree, move null_value); }
    catch (std.json::error failure) { }
    TestStorage3 storage_collections = {.value = std.json::unmarshal("{\"names\":[\"first\",\"last\"],\"values\":{\"z\":[3,4],\"a\":[1]}}")};
    std.string::string collection_text = std.json::marshal(&storage_collections.value);
    bool correct_30 = matches(&collection_text, "{\"names\":[\"first\",\"last\"],\"values\":{\"z\":[3,4],\"a\":[1]}}");
    if (correct_30 == false) { return 54; }
    try { storage_collections.value = std.json::unmarshal("{\"names\":[\"built\"],\"values\":{\"x\":[1,true]}}"); return 55; }
    catch (std.json::error failure) { }
    std.string::string collection_after = std.json::marshal(&storage_collections.value);
    bool correct_31 = matches(&collection_after, "{\"names\":[\"first\",\"last\"],\"values\":{\"z\":[3,4],\"a\":[1]}}");
    if (correct_31 == false) { return 56; }
    return 0;
}

protected async i32 exercise_embed() throws std.json::error, std.alloc::alloc_error {
    Address address = std.json::unmarshal("{\"city\":\"Madrid\"}");
    Identity identity = std.json::unmarshal("{\"id\":\"7\"}");
    std.json::value extra = std.json::parse("{\"flag\":true}");
    Envelope envelope = {.address = move address, .identity = identity, .extra = move extra};
    std.string::string encoded = std.json::marshal(&envelope);
    bool correct = matches(&encoded, "{\"city\":\"Madrid\",\"id\":\"7\",\"flag\":true}");
    if (correct == false) { return 58; }
    Envelope decoded = std.json::unmarshal("{\"flag\":true,\"id\":\"7\",\"CITY\":\"Madrid\"}");
    std.string::string roundtrip = std.json::marshal(&decoded);
    bool correct_32 = matches(&roundtrip, "{\"city\":\"Madrid\",\"id\":\"7\",\"flag\":true}");
    if (correct_32 == false) { return 61; }
    Extras extras = std.json::unmarshal("{\"first\":1,\"last\":2}");
    std.string::string extras_text = std.json::marshal(&extras);
    bool correct_33 = matches(&extras_text, "{\"first\":1,\"last\":2}");
    if (correct_33 == false) { return 62; }
    Extras empty = std.json::unmarshal("{}");
    std.string::string empty_text = std.json::marshal(&empty);
    bool correct_34 = matches(&empty_text, "{}");
    if (correct_34 == false) { return 63; }
    std.json::options options = {
        .max_depth = 256, .max_value_bytes = 67108864, .indent = 0,
        .reject_unknown_fields = true, .ignore_case = true,
        .mode = std.json::mode::document,
    };
    try { Extras rejected = std.json::unmarshal_with_options("{\"extra\":1}", options);
    test_observe(&rejected); return 64; }
    catch (std.json::error failure) {
        if (failure.code != std.json::error_code::unknown_field) { return 65; }
    }
    std.json::value collision = std.json::from_bool(false);
    std.json::insert(&envelope.extra, "CI_T-Y", move collision);
    try { std.string::string rejected = std.json::marshal(&envelope);
    test_observe(&rejected); return 59; }
    catch (std.json::error failure) {
        if (failure.code != std.json::error_code::key_conflict) { return 60; }
    }
    return 0;
}

async i32 main() {
    try {
        try {
            i32 status0 = await exercise();
            if (status0 != 0) { return status0; }
            i32 status1 = await exercise_defaults();
            if (status1 != 0) { return status1; }
            i32 status2 = await exercise_hooks();
            if (status2 != 0) { return status2; }
            i32 status3 = await exercise_tree();
            if (status3 != 0) { return status3; }
            i32 status4 = await exercise_embed();
            if (status4 != 0) { return status4; }
            return 0;
        }
        catch (std.alloc::alloc_error error) { throw TestAssertionFailed {.code = 99}; }
        catch (std.json::error error) { throw TestAssertionFailed {.code = 98}; }
        catch (std.async::start_error error) { throw TestAssertionFailed {.code = 97}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
