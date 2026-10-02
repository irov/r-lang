@deny_panic_alloc
module test.semantic.deny_panic_alloc_try_new;

i32 main() {
    try {
        own i32* boxed = std.alloc::try_new(41);
        array<i32> values = std.array::with_capacity::<i32>(2usize);
        std.array::push(&values, *boxed);
        std.array::push(&values, 1);
        i32 total = *boxed + 1;
        drop boxed;
        if (len(values) != 2usize) {
            return 5;
        }
        return total - 42;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 3;
    } catch (std.alloc::new_error<i32> failure) {
        failure as void;
        return 6;
    } catch (std.array::push_error<i32> failure) {
        failure as void;
        return 4;
    }
}
