module regression.json_saved_exclusive_append_borrow;

i32 main() {
    try {
        std.json::value tree = std.json::parse("[1]");
        std.json::value* target = &tree;
        o<const std.json::value*> found = std.json::get(target, 0);
        switch (found) {
        case variant o::some(move item):
            std.json::value added = std.json::null();
            std.json::append(target, move added);
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
