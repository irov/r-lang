module test.semantic.library_import_accept;

/* R-MOD-0002: a standard module written in R is imported like any module and its traits,
   types and functions resolve through the import. */
import std.cmp;

struct Pair {
    i32 first;
    i32 second;
};

impl std.cmp::Equal for Pair {
    bool eq(const Pair* this, const Pair* other) {
        bool same_first = this->first.eq(&other->first);
        if (same_first == false) { return false; }
        bool same_second = this->second.eq(&other->second);
        return same_second;
    }
};

i32 main() {
    i32 a = 1;
    i32 b = 2;
    const i32* larger = std.cmp::max(&a, &b);
    larger as void;
    Pair p = Pair { .first = 1, .second = 2 };
    Pair q = Pair { .first = 1, .second = 2 };
    bool same = std.cmp::is_equal(&p, &q);
    if (same == false) { return 1; }
    return *larger - 2;
}
