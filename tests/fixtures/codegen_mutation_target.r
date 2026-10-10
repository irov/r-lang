module codegen.mutation_target;

/* B8.2: the target of the mutation test (tests/check_mutation_tool.py, tools/mutate_r.py). The
   checks of main kill every mutant of clamp and count_even except the two that keep the meaning
   of the function: a bound compared with `<=` or `>=`, where an equal value gives the same
   result. The values are chosen so that a loop that skips the first or every second one counts
   differently. */
protected i32 clamp(i32 value, i32 low, i32 high) {
    if (value < low) { return low; }
    if (value > high) { return high; }
    return value;
}

protected u32 count_even(const u32[] values) {
    u32 count = 0u32;
    for (usize index = 0usize; index < len(values); index += 1usize) {
        if (values[index] % 2u32 == 0u32) { count += 1u32; }
    }
    return count;
}

i32 main() {
    if (clamp(5, 0, 10) != 5) { return 1; }
    if (clamp(-3, 0, 10) != 0) { return 2; }
    if (clamp(14, 0, 10) != 10) { return 3; }
    u32[5] values = {2u32, 1u32, 4u32, 6u32, 8u32};
    if (count_even(values[..]) != 4u32) { return 4; }
    return 0;
}
