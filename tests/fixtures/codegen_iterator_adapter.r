module test.codegen.iterator_adapter;

struct Counter { i32 next_value; i32 limit; };
impl core::Iterator for Counter {
    type Item = i32;
    o<i32> next(Counter* this) {
        if (this->next_value >= this->limit) { return o::none; }
        i32 v = this->next_value;
        this->next_value += 1;
        return o::some(v);
    }
};

@generic<I: core::Iterator, U, F: fn(I::Item) -> U>
struct map_iter { I inner; const F* function; };

@generic<I: core::Iterator, U, F: fn(I::Item) -> U>
impl core::Iterator for map_iter<I, U, F> {
    type Item = U;
    o<U> next(map_iter<I, U, F>* this) {
        o<I::Item> item = this->inner.next();
        switch (move item) {
        case variant o::some(move v):
            U mapped = this->function(move v);
            return o::some(move mapped);
        case variant o::none:
            return o::none;
        }
    }
};

@generic<I: core::Iterator, U, F: fn(I::Item) -> U>
map_iter<I, U, F> map(I inner, const F* function) {
    return map_iter<I, U, F> { .inner = move inner, .function = function };
}

i32 main() {
    Counter c = Counter { .next_value = 0, .limit = 4 };
    fn i32 twice(i32 x) { return x * 2; }
    auto it = map(move c, &twice);
    i32 total = 0;
    for (i32 v in &it) { total += v; }
    return total - 12;
}
