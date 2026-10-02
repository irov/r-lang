module fixture.derive.main;

import std.cmp::{ordering};
import fixture.derive.api;
import fixture.derive.api::{disk_error};

/* The module imports std.cmp only by name; deriving equal imports the module itself. */
@derive(clone, equal, ordered, key)
error quota_error : fixture.derive.api::storage_error { u64 limit; };

/* A parent imported by name: its fields, and those of its parent, come first. */
@derive(equal, ordered)
error bad_sector : disk_error { u32 retries; };

@derive(equal, ordered, key)
struct Entry { fixture.derive.api::Label label; u32 count; };

i32 main() {
    quota_error first = {.code = 1, .limit = 10u64};
    quota_error second = {.code = 1, .limit = 20u64};
    if (first.eq(&second) == true) { return 1; }
    ordering order = first.cmp(&second);
    if (order != ordering::less) { return 2; }
    quota_error copy = core::clone(&first);
    if (core::key_equal(&copy, &first) == false) { return 3; }
    bad_sector left = {.code = 2, .sector = 7u32, .retries = 1u32};
    bad_sector right = {.code = 2, .sector = 8u32, .retries = 0u32};
    if (left.cmp(&right) != ordering::less || left.eq(&left) == false) { return 4; }
    Entry a = {.label = fixture.derive.api::Label {.id = 1u32}, .count = 2u32};
    Entry b = {.label = fixture.derive.api::Label {.id = 1u32}, .count = 3u32};
    if (a.cmp(&b) != ordering::less || core::key_equal(&a, &b) == true) { return 5; }
    return 0;
}
