module test.codegen.async_user_attributes;

/* R-REFL-0005 (L42) in async bodies: attribute values read between awaits and in an async generic. */

@attribute(type) struct table { str name; };
@attribute(field) struct column { str name = ""; u16 width = 0u16; };

@table("orders")
struct Order {
    @column(name = "order_id", width = 12u16) i64 id;
    i64 total;
};

usize text_length(str text) { return len(text); }

async usize pause() throws std.error::fault {
    await std.time::sleep_for(std.time::duration_from_parts(0i64, 1000000u32));
    return 1usize;
}

@generic<T>
async usize widths() throws std.error::fault {
    usize total = 0usize;
    for (usize index = 0usize; index < core::field_count::<T>(); index += 1usize) {
        total += await pause();
        o<column> named = core::field_attribute::<column, T>(index);
        switch (named) {
            case variant o::some(c): { total += c->width as usize; }
            case variant o::none: { total += 100usize; }
        }
    }
    return total;
}

async i32 run() throws std.error::fault {
    usize waited = await pause();
    if (waited != 1usize) { return 5; }
    o<table> where = core::type_attribute::<table, Order>();
    switch (where) {
        case variant o::some(t): {
            if (text_length(t->name) != 6usize) { return 1; }
        }
        case variant o::none: { return 2; }
    }
    waited += await pause();
    usize index = waited - 2usize;
    if (text_length(core::field_name::<Order>(index)) != 2usize) { return 3; }
    /* id: width 12 + one pause; total: no column (100) + one pause */
    if (await widths::<Order>() != 114usize) { return 4; }
    return 0;
}

async i32 main() {
    try {
        return await run();
    } catch (std.error::fault failure) {
        failure as void;
    }
    return 9;
}
