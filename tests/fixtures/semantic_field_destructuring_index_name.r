module test.semantic.field_destructuring_index_name;

/* R-STMT-0022: an element of a tuple is bound with `= name`. */
i32 main() {
    (i32, i32) pair = (1, 2);
    auto {.0} = pair;
    return 0;
}
