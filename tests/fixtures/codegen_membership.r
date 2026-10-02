module test.codegen.membership;

/* R-EXPR-0029: membership tests over ranges, dicts and core::Contains implementations. */
struct Digits {
    i32[3] values;
};

impl core::Contains for Digits {
    type Item = i32;
    bool contains(const Digits* this, const i32* value) {
        for (const i32* digit in &this->values) {
            if (*digit == *value) { return true; }
        }
        return false;
    }
};

@generic<C: core::Contains>
bool has_item(const C* container, const C::Item* item) {
    if (*item in *container) { return true; }
    return false;
}

i32 main() {
    i32 seven = 7;
    if (seven not in 0..10) { return 1; }
    if (seven in 0..7) { return 2; }
    bool inside = seven + 1 in 8..9;
    if (inside == false) { return 3; }
    char letter = 'q';
    if (letter not in 'a'..'z') { return 4; }

    try {
        dict<i32, i32> table = std.dict::create::<i32, i32>();
        o<i32> old = std.dict::insert(&table, 7, 70);
        old as void;
        if (seven not in table) { return 6; }
        i32 eight = 8;
        if (eight in table) { return 7; }
        const dict<i32, i32>* view = &table;
        if (seven not in view) { return 8; }
    } catch (std.dict::insert_error<i32, i32> failure) {
        failure as void;
        return 5;
    }

    Digits digits = Digits { .values = {1, 2, 3} };
    i32 two = 2;
    if (two not in digits) { return 9; }
    if (seven in digits) { return 10; }
    i32 three = 3;
    bool found = has_item(&digits, &three);
    if (found == false) { return 11; }
    if (seven in digits && two in digits) { return 12; }
    return 0;
}
