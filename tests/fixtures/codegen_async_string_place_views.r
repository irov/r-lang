module test.codegen.async_string_place_views;

/* R-EXPR-0015 (L41) in async bodies: the view is formed and used between awaits, and the owner
   it borrows lives across them. */

usize measure(str text) { return len(text); }

async usize pause_and_measure(std.string::string text) throws std.error::fault {
    usize before = measure(text);
    await std.time::sleep_for(std.time::duration_from_parts(0i64, 1000000u32));
    text.append("!");
    return before + measure(text);
}

async i32 run() throws std.error::fault {
    std.string::string word = std.string::from_str("async");
    if (measure(word) != 5usize) { return 1; }
    usize total = await pause_and_measure(std.string::from_str("abc"));
    if (total != 7usize) { return 2; }
    await std.time::sleep_for(std.time::duration_from_parts(0i64, 1000000u32));
    str view = word;
    if (len(view) != 5usize) { return 3; }
    word.append("!");
    if (measure(word) != 6usize) { return 4; }
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
