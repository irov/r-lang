module regression.branch_merged_json_mutation_uaf;

i32 main() {
    try {
        std.json::value left = std.json::parse("{\"items\":[1,2,3]}");
        std.json::value right = std.json::parse("{\"items\":[4,5,6]}");
        bool choose_right = false;
        std.json::value* target = &left;
        if (choose_right == true) {
            target = &right;
        }
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
