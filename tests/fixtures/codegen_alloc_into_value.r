module test.codegen.alloc_into_value;

/* std.alloc::into_value moves the value out of its owner, whatever the shape of T: a scalar T is
   an ordinary value of the caller (B6-1: the LLVM lowering returned the address of a temporary
   for a scalar result). */

struct Pair {
    i64 left;
    i64 right;
};

protected i32 scalars() throws std.alloc::new_error<i32>, std.alloc::new_error<bool>,
    std.alloc::new_error<f64>, std.alloc::new_error<u8> {
    own i32* number = std.alloc::try_new(-17);
    i32 value = std.alloc::into_value(move number);
    own bool* flag = std.alloc::try_new(true);
    bool set = std.alloc::into_value(move flag);
    own f64* real = std.alloc::try_new(2.5);
    f64 half = std.alloc::into_value(move real);
    own u8* byte = std.alloc::try_new(200u8);
    u8 small = std.alloc::into_value(move byte);
    i32 failures = 0;
    if (value != -17) { failures += 1; }
    if (set != true) { failures += 2; }
    if (half != 2.5) { failures += 4; }
    if (small != 200u8) { failures += 8; }
    return failures + (std.alloc::into_value(std.alloc::try_new(5)) - 5);
}

protected i64 pair() throws std.alloc::new_error<Pair> {
    own Pair* owner = std.alloc::try_new(Pair {.left = 40, .right = 2});
    Pair value = std.alloc::into_value(move owner);
    return value.left + value.right;
}

i32 main() {
    try {
        i32 status = scalars();
        if (status != 0) { return status; }
        if (pair() != 42i64) { return 5; }
        return 0;
    } catch (std.alloc::new_error<i32> failure) {
        failure as void;
        return 6;
    } catch (std.alloc::new_error<bool> failure) {
        failure as void;
        return 6;
    } catch (std.alloc::new_error<f64> failure) {
        failure as void;
        return 6;
    } catch (std.alloc::new_error<u8> failure) {
        failure as void;
        return 6;
    } catch (std.alloc::new_error<Pair> failure) {
        failure as void;
        return 6;
    }
}
