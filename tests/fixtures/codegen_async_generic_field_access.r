module test.codegen.async_generic_field_access;

/* R-STMT-0023, R-REFL-0006, R-TYPE-0043 (L44) in async bodies: a generic async function awaits
   in every repetition and calls a generic function whose fields constraint its own proves. */

trait Count {
    u64 count(const Self* this);
};

impl Count for u32 { u64 count(const u32* this) { return *this as u64; } };
impl Count for u64 { u64 count(const u64* this) { return *this; } };

struct Totals {
    u32 small;
    u64 large;
};

@generic<T: fields(Count)>
u64 sum(const T* value) {
    u64 total = 0u64;
    for (constexpr usize index in 0usize..core::field_count::<T>()) {
        total += core::field(value, index)->count();
    }
    return total;
}

@generic<T: fields(Count) & send & unborrowed>
async u64 sum_slowly(T value) throws std.error::fault {
    u64 total = 0u64;
    for (constexpr usize index in 0usize..core::field_count::<T>()) {
        await std.time::sleep_for(std.time::duration_from_parts(0i64, 1000000u32));
        total += core::field(&value, index)->count();
    }
    /* A generic call whose fields constraint the parameter's own proves. */
    return total + sum(&value);
}

async i32 main() {
    try {
        Totals totals = {.small = 2u32, .large = 40u64};
        u64 total = await sum_slowly(totals);
        if (total != 84u64) { return 1; }
        return 0;
    } catch (std.error::fault failure) {
        failure as void;
    }
    return 9;
}
