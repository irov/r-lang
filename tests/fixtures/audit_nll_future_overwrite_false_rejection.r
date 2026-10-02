module audit.nll_future_overwrite_false_rejection;

// Shared mutable storage preserves the update and cleanup scenario under test.
struct TestStorage1 { const i32* value; };

i32 main() {
    i32 source = 7;
    i32 replacement = 11;
    TestStorage1 storage_alias = {.value = &source};
    source += 1;
    storage_alias.value = &replacement;
    return *storage_alias.value - 11;
}
