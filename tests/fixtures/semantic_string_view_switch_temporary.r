module test.semantic.string_view_switch_temporary;

/* R-STMT-0006: a switch selects by the str view of a string place; a temporary string is no
   place. */
i32 run() throws std.alloc::alloc_error {
    switch (std.string::from_str("word")) {
    case "word": return 1;
    default: break;
    }
    return 2;
}

i32 main() {
    try {
        return run();
    } catch (std.alloc::alloc_error failure) {
        failure as void;
    }
    return 99;
}
