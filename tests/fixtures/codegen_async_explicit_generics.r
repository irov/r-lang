module test.codegen.async_explicit_generics;

@generic<T: copy & send & unborrowed>
async T echo(T value) { return value; }

@generic<T: copy & send & unborrowed>
async array<T> fresh() throws std.alloc::alloc_error {
    array<T> values = std.array::with_capacity::<T>(2usize);
    return move values;
}

async i32 main() {
    i32 echoed = await echo::<i32>(4);
    if (echoed != 4) { return 1; }
    array<u32> numbers = await fresh::<u32>();
    if (len(numbers) != 0usize) { return 2; }
    auto starter = fresh::<u8>;
    array<u8> bytes_values = await starter();
    if (len(bytes_values) != 0usize) { return 3; }
    return 0;
}
