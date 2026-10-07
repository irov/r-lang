module test.codegen.async_field_access;

/* R-STMT-0023, R-REFL-0006 (L44) in async bodies: each repetition awaits, and the fields are
   borrowed between the awaits. */

struct Counters {
    u32 hits;
    u64 bytes;
};

u64 widen(const u32* value) { return *value as u64; }
u64 widen(const u64* value) { return *value; }

void grow(u32* value) { *value += 1u32; }
void grow(u64* value) { *value += 100u64; }

async u64 sum_with_pauses(Counters counters) throws std.error::fault {
    u64 total = 0u64;
    for (constexpr usize index in 0usize..core::field_count::<Counters>()) {
        await std.time::sleep_for(std.time::duration_from_parts(0i64, 1000000u32));
        grow(core::field_mut(&counters, index));
        total += widen(core::field(&counters, index));
    }
    return total;
}

/* Defect L44-12: a task group in each repetition is a scope of its own in the frame, in a plain
   loop and in an instance of a generic one. */
async u64 later(u64 value) throws std.error::fault {
    await std.time::sleep_for(std.time::duration_from_parts(0i64, 1000u32));
    return value;
}

async u64 grouped() throws std.error::fault {
    u64 total = 0u64;
    for (constexpr u64 round in 0u64..2u64) {
        task_scope(2) group {
            auto first = later(round);
            auto second = later(round + 10u64);
            total += await move first;
            total += await move second;
        }
    }
    return total;
}

trait Count {
    u64 count(const Self* this);
};

impl Count for u32 {
    u64 count(const u32* this) { return *this as u64; }
};

impl Count for u64 {
    u64 count(const u64* this) { return *this; }
};

@generic<T: fields(Count) & send & unborrowed>
async u64 grouped_fields(T value) throws std.error::fault {
    u64 total = 0u64;
    for (constexpr usize index in 0usize..core::field_count::<T>()) {
        u64 counted = core::field(&value, index)->count();
        task_scope(2) group {
            auto first = later(counted);
            auto second = later(counted + 1u64);
            total += await move first;
            total += await move second;
        }
    }
    return total;
}

async i32 main() {
    try {
        Counters counters = {.hits = 1u32, .bytes = 2u64};
        u64 total = await sum_with_pauses(counters);
        if (total != 104u64) { return 1; }
        if (await grouped() != 22u64) { return 2; }
        if (await grouped_fields(counters) != 8u64) { return 3; }
        return 0;
    } catch (std.error::fault failure) {
        failure as void;
    }
    return 9;
}
