module test.audit.std_string_mutation_read_distinct_origin;

i32 main() {
    try {
        std.string::string target = std.string::from_str("abcdef");
        std.string::string source = std.string::from_str("abc");
        std.string::reserve(&target, std.string::len(&source));
        std.string::truncate(&target, std.string::len(&source));
        if (std.string::len(&target) != 3 || std.string::len(&source) != 3) {
            return 1;
        }
        return 0;
    } catch (std.alloc::alloc_error failure) {
        return 2;
    } catch (std.string::boundary_error failure) {
        return 3;
    }
}
