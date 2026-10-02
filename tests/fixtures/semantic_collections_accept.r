module test.semantic.collections_accept;

// Retain values whose purpose here is type or lifetime coverage.
@generic<T> @noalloc @nonblocking
protected void test_observe(const T* value) { value as void; }


/* R-EXPR-0030: literal and comprehension collection expressions; the standard insertion
   effects are handled by the enclosing try. */
i32 main() {
    i32 total = 0;
    try {
        array<i32> small = [1, 2, 3];
        array<i32> squares = [x * x for (i32 x in 0..4) if (x % 2 == 0)];
        dict<i32, i32> doubles = {*x: *x * 2 for (const i32* x in &small)};
        dict<i32, i32> fixed = {1: 10, 2: 20};
        test_observe(&fixed);
        for (const i32* x in &squares) { total += *x; }
        i32 two = 2;
        if (two not in doubles) { return 1; }
        if (two not in fixed) { return 2; }
    } catch (std.array::push_error<i32> push_failure) {
        push_failure as void;
        return 3;
    } catch (std.dict::insert_error<i32, i32> insert_failure) {
        insert_failure as void;
        return 4;
    }
    return total;
}
