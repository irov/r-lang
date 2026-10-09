module test.codegen.async_alloc_into_value;

/* std.alloc::into_value in async bodies, where every value of a step lives in its frame: a scalar
   T read back after an await is the value, not the address of a temporary (B6-1). */

struct Pair {
    i64 left;
    i64 right;
};

async i32 later(i32 value) throws std.error::fault {
    await std.time::sleep_for(std.time::duration_from_parts(0i64, 1000000u32));
    return value;
}

protected async i32 scalars() throws std.error::fault, std.alloc::new_error<i32>,
    std.alloc::new_error<bool>, std.alloc::new_error<f64> {
    own i32* number = std.alloc::try_new(-17);
    i32 value = std.alloc::into_value(move number);
    own bool* flag = std.alloc::try_new(true);
    bool set = std.alloc::into_value(move flag);
    own f64* real = std.alloc::try_new(2.5);
    f64 half = std.alloc::into_value(move real);
    i32 step = await later(17);
    i32 failures = 0;
    if (value + step != 0) { failures += 1; }
    if (set != true) { failures += 2; }
    if (half != 2.5) { failures += 4; }
    return failures;
}

protected async i64 pair() throws std.error::fault, std.alloc::new_error<Pair> {
    own Pair* owner = std.alloc::try_new(Pair {.left = 40, .right = 2});
    Pair value = std.alloc::into_value(move owner);
    i32 step = await later(0);
    return value.left + value.right + step as i64;
}

async i32 main() {
    try {
        i32 status = await scalars();
        if (status != 0) { return status; }
        if (await pair() != 42i64) { return 5; }
        return 0;
    } catch (std.error::fault failure) {
        failure as void;
        return 6;
    } catch (std.alloc::new_error<i32> failure) {
        failure as void;
        return 7;
    } catch (std.alloc::new_error<bool> failure) {
        failure as void;
        return 7;
    } catch (std.alloc::new_error<f64> failure) {
        failure as void;
        return 7;
    } catch (std.alloc::new_error<Pair> failure) {
        failure as void;
        return 7;
    }
}
