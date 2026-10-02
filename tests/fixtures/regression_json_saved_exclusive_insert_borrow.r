module regression.json_saved_exclusive_insert_borrow;

i32 main() {
    try {
        std.json::value tree = std.json::parse("{\"item\":1}");
        std.json::value* target = &tree;
        o<const std.json::value*> found = std.json::find(target, "item");
        switch (found) {
        case variant o::some(move item):
            std.json::value added = std.json::null();
            std.json::insert(target, "other", move added);
            usize length = std.json::len(item);
            return length as i32;
        case variant o::none:
            return 3;
        }
    } catch (std.json::error failure) {
        drop failure;
        return 2;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 1;
    }
}
