module test.codegen.index_proofs;

/* R-EXPR-0021 with index proofs: an index proven below its bound is emitted without its check,
   every other index keeps it. tests/check_index_proofs.py compares the checks left in the
   generated C with the marks: a line marked proven has no bounds check, a line marked checked
   has one. Every function computes a value that main compares, so the proven paths run. */

error too_far { usize index; };

u32 sum_for(const u8[] data) {
    u32 total = 0u32;
    for (usize i = 0usize; i < len(data); i += 1usize) {
        total += data[i] as u32; /* proven */
    }
    return total;
}

u32 sum_while_alias(const u8[] data) {
    usize n = len(data);
    usize i = 0usize;
    u32 total = 0u32;
    while (i < n) {
        total += data[i] as u32; /* proven */
        i += 1usize;
    }
    return total;
}

u8 at_or_zero(const u8[] data, usize index) {
    if (index >= len(data)) { return 0u8; }
    return data[index]; /* proven */
}

u8 at_or_throw(const u8[] data, usize index) throws too_far {
    throw (index >= len(data)) too_far { .index = index };
    return data[index]; /* proven */
}

u32 table_lookups(const u8[] data) {
    u32[256] table = {};
    for (usize slot = 0usize; slot < 256usize; slot += 1usize) {
        table[slot] = (slot as u32) * 3u32; /* proven */
    }
    u32 total = 0u32;
    for (usize i = 0usize; i < len(data); i += 1usize) {
        u8 byte = data[i]; /* proven */
        total += table[byte as usize]; /* proven */
        total += table[((total ^ 0x5au32) & 0xffu32) as usize]; /* proven */
    }
    return total;
}

u32 guarded_operands(const u8[] data, usize i) {
    u32 total = 0u32;
    if ((i < len(data)) && (data[i] != 0u8)) { total += 1u32; } /* proven */
    u8 picked = (i < len(data)) ? data[i] : 7u8; /* proven */
    return total + (picked as u32);
}

u32 small_ranges(u32 value, usize position, usize k) {
    u8[16] ring = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    u32 total = ring[position % 16usize] as u32; /* proven */
    total += ring[(value >> 28usize) as usize] as u32; /* proven */
    total += ring[3usize] as u32; /* proven */
    if (k < 8usize) {
        total += ring[k + 8usize] as u32; /* proven */
    }
    for (usize bits = 1usize; bits <= 15usize; bits += 1usize) {
        total += ring[bits] as u32; /* proven */
    }
    return total;
}

u32 minimum_length(const u8[] data) {
    if (len(data) >= 4usize) {
        return (data[3usize] as u32) + (data[0usize] as u32); /* proven */
    }
    return 0u32;
}

u32 blocks(const u8[] data, usize width) {
    u32 total = 0u32;
    usize start = 0usize;
    while (start < len(data)) {
        usize stop = (len(data) - start > width) ? start + width : len(data);
        const u8[] block = data[start..stop];
        for (usize j = 0usize; j < len(block); j += 1usize) {
            usize copy = j;
            total += block[copy] as u32; /* proven */
        }
        start = stop;
    }
    return total;
}

void bump(usize* value) { *value += 1usize; }

u32 kept_checks(const u8[] data, u8[] out, u64 big) {
    u32 total = 0u32;
    usize i = 0usize;
    while ((i + 1usize) < len(data)) {
        i += 1usize;
        total += data[i] as u32; /* checked */
    }
    usize k = 0usize;
    bump(&k);
    total += data[k] as u32; /* checked */
    usize index = len(data);
    if (index >= len(data)) { index = 0usize; }
    total += data[index] as u32; /* checked */
    usize after = 0usize;
    if (after < len(data)) {
        usize next = after + 1usize;
        total += data[next] as u32; /* checked */
    }
    usize limit = 2usize;
    usize below = 1usize;
    if (below < limit) {
        total += data[below + 0usize] as u32; /* checked */
    }
    for (usize outer = 0usize; outer < len(data); outer += 1usize) {
        usize inner = 0usize;
        while (inner < 1usize) {
            inner += 1usize;
            outer += 0usize;
        }
        total += data[outer] as u32; /* checked */
    }
    for (usize n = 1usize; n <= len(data); n += 1usize) {
        total += data[n - 1usize] as u32; /* checked */
    }
    if (((big as u8) as usize) < len(data)) {
        total += data[big as usize] as u32; /* checked */
    }
    bool flag = true;
    if ((after < len(data)) || (flag == true)) {
        total += data[after] as u32; /* checked */
    }
    const u8[] view = data;
    for (usize v = 0usize; v < len(view); v += 1usize) {
        view = data[v..len(data)];
        total += view[0usize] as u32; /* checked */
    }
    for (usize w = 0usize; w < len(out); w += 1usize) {
        out[w] = 1u8; /* proven */
    }
    return total;
}

u32 bounded_forms(const u8[] data, const u32[] table, usize seed) {
    u32 total = 0u32;
    usize start = 2usize;
    const u8[] pair = data[start..start + 2usize];
    total += (pair[0usize] as u32) + (pair[1usize] as u32); /* proven */
    usize stop = 3usize;
    if (stop > len(data)) { return 0u32; }
    for (usize i = 0usize; i < stop; i += 1usize) {
        total += data[i] as u32; /* proven */
    }
    usize mask = len(table) - 1usize;
    if (mask >= len(table)) { return 0u32; }
    total += table[seed & mask]; /* proven */
    total += table[(seed >> 1usize) & mask]; /* proven */
    total += table[seed % len(table)]; /* proven */
    total += table[(seed as u8) as usize % len(table)]; /* proven */
    total += table[seed % (len(table) + 1usize)]; /* checked */
    return total;
}

u32 branch_deaths(const u8[] data, usize start, bool flag) {
    usize i = start;
    if (i >= len(data)) { return 0u32; }
    u32 total = 0u32;
    if (flag == true) {
        total += 1u32;
        i += 1usize;
    } else {
        total += data[i] as u32; /* proven */
    }
    total += data[i] as u32; /* checked */
    usize j = start;
    if (j >= len(data)) { return total; }
    if (flag == false) {
        j += 1usize;
        return total + (j as u32);
    }
    total += data[j] as u32; /* proven */
    usize k = start;
    if (k >= len(data)) { return total; }
    if (flag == true) {
        total += 2u32;
    } else {
        k += 1usize;
        return total + (k as u32);
    }
    total += data[k] as u32; /* proven */
    if (flag == false) {
        return total;
    } else {
        k += 1usize;
    }
    total += data[k] as u32; /* checked */
    return total;
}

u32 conversions(u32 value) {
    u8 low = (value & 0xffu32) as u8; /* proven conversion */
    u8 half = (value / 2u32) as u8; /* checked conversion */
    u32 total = (low as u32) + (half as u32);
    if (value < 256u32) {
        u8 small = value as u8; /* proven conversion */
        total += small as u32;
    }
    u16 shifted = ((value as u16) >> 4usize) as u16; /* checked conversion */
    u16 narrow = (((value & 0xfffu32) as u16) >> 4usize) as u16; /* proven conversion */
    return total + (shifted as u32) + (narrow as u32);
}

u32 array_lengths() throws std.alloc::alloc_error {
    array<u32> values = std.array::filled(4usize, 5u32);
    u32 total = 0u32;
    for (usize i = 0usize; i < len(values); i += 1usize) {
        total += values[i]; /* proven */
    }
    array<u32> growing = std.array::filled(2usize, 1u32);
    for (usize i = 0usize; i < len(growing); i += 1usize) {
        total += growing[i]; /* checked */
        if (i == 0usize) {
            try { std.array::push(&growing, 2u32); }
            catch (std.array::push_error<u32> failure) { move failure as void; }
        }
    }
    return total;
}

/* P4.4-8: the length of a row of a two-dimensional fixed array is a constant; the proven index
   of the row leaves its temporary otherwise unused. */
usize row_width() {
    u8[2usize][3usize] rows = {};
    usize at = 0usize;
    usize width = len(rows[at]); /* proven */
    return width;
}

i32 main() {
    u8[6] storage = {1, 2, 3, 4, 5, 6};
    u8[2] target = {0, 0};
    const u8[] data = storage[..];
    if (sum_for(data) != 21u32) { return 1; }
    if (sum_while_alias(data) != 21u32) { return 2; }
    if ((at_or_zero(data, 2usize) != 3u8) || (at_or_zero(data, 6usize) != 0u8)) { return 3; }
    u8 thrown = 0u8;
    try { thrown += at_or_throw(data, 7usize); }
    catch (too_far failure) { thrown += failure.index as u8; }
    if (thrown != 7u8) { return 4; }
    if (table_lookups(data) == 0u32) { return 5; }
    if (guarded_operands(data, 1usize) != 3u32) { return 6; }
    if (guarded_operands(data, 9usize) != 7u32) { return 7; }
    if (small_ranges(0xf0000000u32, 18usize, 3usize) != 2u32 + 15u32 + 3u32 + 11u32 + 120u32) {
        return 8;
    }
    if (minimum_length(data) != 5u32) { return 9; }
    if (blocks(data, 4usize) != 21u32) { return 10; }
    u32 kept = kept_checks(data, target[..], 3u64);
    if (kept != 20u32 + 2u32 + 1u32 + 2u32 + 2u32 + 21u32 + 21u32 + 4u32 + 1u32 + 10u32) {
        return 11;
    }
    u32[8] table = {10, 20, 30, 40, 50, 60, 70, 80};
    if (bounded_forms(data, table[..], 13usize) != 7u32 + 6u32 + 60u32 + 70u32 + 60u32 + 60u32 + 50u32) {
        return 14;
    }
    if ((target[0usize] != 1u8) || (target[1usize] != 1u8)) { return 12; }
    if (array_lengths() != 20u32 + 1u32 + 1u32 + 2u32) { return 13; }
    if (branch_deaths(data, 1usize, true) != 1u32 + 3u32 + 2u32 + 2u32 + 2u32 + 3u32) { return 15; }
    if (conversions(100u32) != 100u32 + 50u32 + 100u32 + 6u32 + 6u32) { return 16; }
    if (row_width() != 3usize) { return 17; }
    return 0;
}
