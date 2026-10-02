module test.semantic.variadic_accept;

/* R-FUNC-0018: trailing arguments are packed into a hidden array of the caller; a spread
   forwards an existing slice. */
i32 sum(i32... values) {
    i32 total = 0;
    for (const i32* v in &values) { total += *v; }
    return total;
}

i32 main() {
    i32[2] pair = {4, 5};
    const i32[] view = &pair;
    i32 a = sum(1, 2, 3);
    i32 b = sum();
    i32 c = sum(...view);
    return a + b + c;
}
