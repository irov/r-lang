module regression.list_conditional_same_referent;

protected i32 probe(bool first) throws std.list::push_error<i32> {
    list<i32> values = std.list::create::<i32>();
    i32* retained_node = std.list::push_back(&values, 8);
    i32* removed_node = std.list::insert_before(&values, retained_node, 7);
    const i32* alias = retained_node;
    const i32* selected = (first == true) ? alias : alias;
    i32 removed = std.list::remove(&values, move removed_node);
    return removed + *selected - 15;
}

i32 main() {
    try {
        i32 result = probe(true);
        return result;
    } catch (std.list::push_error<i32> failure) {
        failure as void;
        return 90;
    }
}
