module test.audit.list_returned_node_identity;

protected const i32* identity(const i32* value) {
    return value;
}

protected i32 probe() throws std.list::push_error<i32> {
    list<i32> values = std.list::create::<i32>();
    i32* retained_node = std.list::push_back(&values, 8);
    i32* removed_node = std.list::insert_before(&values, retained_node, 7);
    const i32* alias = identity(retained_node);
    i32 removed = std.list::remove(&values, move removed_node);
    return removed + *alias - 15;
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
