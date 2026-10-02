module regression.branch_merged_borrow_drop_uaf;

i32 main() {
    try {
        std.json::value left = std.json::parse("\"left-owned-text\"");
        std.json::value right = std.json::parse("\"right-owned-text\"");
        bool choose_right = false;
        str selected = std.json::text(&left);
        if (choose_right == true) {
            selected = std.json::text(&right);
        }
        drop left;
        std.string::string copy = std.string::from_str(selected);
        drop copy;
        return 0;
    } catch (std.json::error failure) {
        drop failure;
        return 2;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 1;
    }
}
