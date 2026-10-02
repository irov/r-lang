module regression.json_saved_exclusive_mutation_uaf;

i32 main() {
    try {
        std.json::value tree = std.json::parse("{\"items\":[1,2,3]}");
        std.json::value* target = &tree;
        o<const std.json::value*> found = std.json::find(target, "items");
        switch (found) {
        case variant o::some(move items):
            std.json::value taken = std.json::take_field(target, "items");
            drop taken;
            usize count = std.json::len(items);
            if (count == 3) {
                return 0;
            }
            return 4;
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
