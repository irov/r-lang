module test.regression.list_remove_wrong_origin_rule;

protected void reject() throws std.list::push_error<i32> {
    list<i32> source = std.list::create::<i32>();
    list<i32> target = std.list::create::<i32>();
    i32* node = std.list::push_back(&source, 7);
    i32 removed = std.list::remove(&target, move node);
    removed as void;
}

i32 main() {
    return 0;
}
