@deny_panic_alloc
module test.codegen.deny_panic_alloc;

struct Pair {
    i32 left;
    i32 right;
};

protected i32 sum_pair(own Pair* pair) {
    i32 total = pair->left + pair->right;
    drop pair;
    return total;
}

i32 main() {
    try {
        own Pair* pair = std.alloc::try_new(Pair { .left = 20, .right = 22 });
        array<i32> values = std.array::with_capacity::<i32>(2usize);
        std.array::push(&values, 1);
        std.array::push(&values, 2);
        i32 total = sum_pair(move pair);
        if ((total != 42) || (len(values) != 2usize)) {
            return 1;
        }
        return 0;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 3;
    } catch (std.alloc::new_error<Pair> failure) {
        failure as void;
        return 5;
    } catch (std.array::push_error<i32> failure) {
        failure as void;
        return 4;
    }
}
