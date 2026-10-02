module test.semantic.stack_recursive_json;

/* R-FUNC-0004: derived JSON encoding of a self-nesting type recurses with the data. */
struct Node {
    i64 value;
    array<Node> children;
};

i32 main() {
    try {
        Node root = { .value = 1, .children = std.array::with_capacity::<Node>(0) };
        std.string::string encoded = std.json::marshal(&root);
        drop encoded;
        return 0;
    } catch (std.json::error failure) {
        return 2;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 3;
    }
}
