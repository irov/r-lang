module test.regression.list_remove_after_shared_alias_last_use;

protected i32 probe() throws std.list::push_error<i32> {
    list<i32> values = std.list::create::<i32>();
    i32* node = std.list::push_back(&values, 7);
    const i32* alias = node;
    i32 before = *alias;
    i32 removed = std.list::remove(&values, move node);
    return removed - before;
}

i32 main() {
    try {
        i32 result = probe();
        return result;
    } catch (std.list::push_error<i32> failure) {
        failure as void;
        return 90;
    }
}
