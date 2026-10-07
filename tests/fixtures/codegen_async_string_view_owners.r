module test.codegen.async_string_view_owners;

/* R-EXPR-0015, R-STMT-0006, R-EXPR-0031 (L43) in async bodies: string and builder places are
   viewed as str and as bytes between awaits, a switch or a match selects by the view, and the
   owners live across awaits. */

usize count_bytes(const u8[] data) { return len(data); }

async u32 classify(std.string::string word) throws std.error::fault {
    await std.time::sleep_for(std.time::duration_from_parts(0i64, 1000000u32));
    switch (word) {
    case "alpha": return 1u32;
    case "beta": return 2u32;
    default: return 0u32;
    }
}

async i32 run() throws std.error::fault {
    std.string::string word = std.string::from_str("beta");
    if (count_bytes(word) != 4usize) { return 1; }
    u32 kind = await classify(std.string::from_str("alpha"));
    if (kind != 1u32) { return 2; }
    i32 picked = match (word) {
        case "beta": 1;
        default: 0;
    };
    if (picked != 1) { return 6; }

    std.format::builder builder = std.format::create();
    builder.append(word);
    await std.time::sleep_for(std.time::duration_from_parts(0i64, 1000000u32));
    builder.append("!");
    switch (builder) {
    case "beta!": break;
    default: return 3;
    }
    if (std.bytes::equal(builder, "beta!") == false) { return 4; }
    await std.time::sleep_for(std.time::duration_from_parts(0i64, 1000000u32));
    word.append(builder);
    if (count_bytes(word) != 9usize) { return 5; }
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
