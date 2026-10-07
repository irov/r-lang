module test.codegen.string_keys;
import std.test;
import std.set;

// M24-2 (Library R-LIB-0020): an owned std.string::string is a dictionary key with the
// contract of `str`: exact UTF-8 byte equality and one hash that generated code and the C
// modules that build string-keyed dictionaries (std.env, std.json) share.

protected std.string::string owned(str text) throws std.alloc::alloc_error {
    return std.string::from_str(text);
}

@test
void keys_a_dictionary_it_creates() throws std.test::failure, std.error::fault,
    std.dict::insert_error<std.string::string, u32> {
    dict<std.string::string, u32> counts = std.dict::create();
    o<u32> first = std.dict::insert(&counts, owned("alpha"), 1u32);
    first as void;
    o<u32> second = std.dict::insert(&counts, owned("beta"), 2u32);
    second as void;
    o<u32> replaced = std.dict::insert(&counts, owned("alpha"), 3u32);
    switch (replaced) {
    case variant o::some(value): std.test::equal(*value, 1u32);
    case variant o::none: std.test::fail("alpha was present");
    }
    std.test::equal(len(counts), 2usize);
    std.string::string key = owned("alpha");
    o<const u32*> found = std.dict::get(&counts, &key);
    switch (found) {
    case variant o::some(value): std.test::equal(**value, 3u32);
    case variant o::none: std.test::fail("alpha is a key");
    }
    std.string::string gone = owned("beta");
    o<u32> removed = std.dict::remove(&counts, &gone);
    switch (removed) {
    case variant o::some(value): std.test::equal(*value, 2u32);
    case variant o::none: std.test::fail("beta was a key");
    }
    std.test::check(std.dict::contains(&counts, &gone) == false, "beta is gone");
}

@test
void finds_environment_variables() throws std.test::failure, std.error::fault {
    std.env::set("R_STRING_KEYS_FIXTURE", "value-42");
    dict<std.string::string, std.string::string> variables = std.env::variables();
    std.string::string wanted = owned("R_STRING_KEYS_FIXTURE");
    o<const std.string::string*> found = std.dict::get(&variables, &wanted);
    switch (found) {
    case variant o::some(value): std.test::equal_text(**value, "value-42");
    case variant o::none: std.test::fail("the variable is a key");
    }
    std.string::string absent = owned("R_STRING_KEYS_FIXTURE_ABSENT");
    std.test::check(std.dict::contains(&variables, &absent) == false, "an absent name");
}

@test
void sets_hold_owned_strings() throws std.test::failure, std.error::fault,
    std.dict::insert_error<std.string::string, bool> {
    std.set::set<std.string::string> names = std.set::set<std.string::string>::create();
    std.test::check(names.insert(owned("x")), "x is new");
    std.test::check(names.insert(owned("y")), "y is new");
    std.test::check(names.insert(owned("x")) == false, "x is known");
    std.string::string probe = owned("y");
    std.test::check(names.contains(&probe), "y is a member");
}

@test
void hashes_like_str() throws std.test::failure, std.alloc::alloc_error {
    std.string::string text = owned("same bytes");
    str view = "same bytes";
    std.test::equal(core::hash(&text), core::hash(&view));
    std.string::string other = owned("same bytes");
    std.test::check(core::key_equal(&text, &other), "equal bytes are equal keys");
}
