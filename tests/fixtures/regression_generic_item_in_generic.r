module test.regression.generic_item_in_generic;

/* M32-4: a generic function forms the function item of another generic function closed over its
   own parameter, name::<T>; each instance holds the item of the matching instance. */
@generic<T: copy>
protected T keep(T value) { return value; }

@generic<T: copy>
protected fn(T) -> T chosen() { return keep::<T>; }

i32 main() {
    fn(u32) -> u32 narrow = chosen::<u32>();
    fn(u64) -> u64 wide = chosen::<u64>();
    u32 small = narrow(7u32);
    u64 large = wide(9u64);
    u64 total = (small as u64) + large;
    if (total == 16u64) { return 0; }
    return 1;
}
