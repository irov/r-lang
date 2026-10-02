module test.semantic.for_in_not_iterable;

/* R-STMT-0014: an integer is neither a range, a sequence, a cursor nor an iterator. */
i32 main() {
    i32 count = 3;
    for (i32 v in count) { v as void; }
    return 0;
}
