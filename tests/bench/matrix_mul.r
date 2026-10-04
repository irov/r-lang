module bench.matrix_mul;

/* Dense 96x96 u32 matrix product over flat slices, indexed i*n+k: 200 products. */
u32 multiply(const u32[] a, const u32[] b, u32[] c, usize n) {
    for (usize i = 0usize; i < n; i += 1usize) {
        for (usize j = 0usize; j < n; j += 1usize) {
            u32 sum = 0u32;
            for (usize k = 0usize; k < n; k += 1usize) {
                sum += a[i * n + k] * b[k * n + j];
            }
            c[i * n + j] = sum;
        }
    }
    return c[n + 1usize];
}

i32 main() {
    usize n = 96usize;
    array<u32> a = std.array::filled(n * n, 0u32);
    array<u32> b = std.array::filled(n * n, 0u32);
    array<u32> c = std.array::filled(n * n, 0u32);
    {
        u32[] fa = std.array::as_slice_mut(&a);
        u32[] fb = std.array::as_slice_mut(&b);
        for (usize i = 0usize; i < len(fa); i += 1usize) {
            fa[i] = ((i * 7usize) % 13usize) as u32;
            fb[i] = ((i * 5usize) % 11usize) as u32;
        }
    }
    u32 check = 0u32;
    for (usize round = 0usize; round < 200usize; round += 1usize) {
        check += multiply(std.array::as_slice(&a), std.array::as_slice(&b), std.array::as_slice_mut(&c), n);
        u32[] fa = std.array::as_slice_mut(&a);
        fa[round] += 1u32;
    }
    drop c;
    return (check % 109u32) as i32;
}
