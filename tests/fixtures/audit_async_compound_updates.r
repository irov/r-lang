module audit.async_compound_updates;

protected i32 overwrite(i32* value) {
    *value = 40;
    return 2;
}

async i32 main() {
    i32 value = 1;
    value += overwrite(&value);
    if (value != 3) { return 1; }

    f64 floating = 1.5;
    floating += 2.25;
    floating -= 0.75;
    floating *= 2.0;
    floating /= 4.0;
    floating += 2;
    i32 floating_status = floating == 3.5f64 ? 0 : 1;
    if (floating_status != 0) { return 2; }

    ++value;
    value++;
    --value;
    value--;
    if (value != 3) { return 3; }

    u8 narrow = 5;
    narrow <<= 2usize;
    if (narrow != 20) { return 5; }

    u64 mixed = 10;
    u32 increment = 7;
    mixed += increment;
    if (mixed != 17u64) { return 6; }

    u8 mixed_wrap = 255;
    mixed_wrap += 1u16;
    if (mixed_wrap != 0) { return 7; }

    u8 wrapped = 255;
    wrapped++;
    i32 selected = wrapped == 0 ? 0 : 4;
    return selected;
}
