module std.iter;
import std.cmp;

/* R-SLIB-ITER-0001: sources over slices and integer ranges. Every item leaves an iterator by
   value; a slice iterator yields shared borrows of the elements. */
@generic<T>
struct slice_iter {
    const T[] items;
    usize index;
};

@generic<T>
impl core::Iterator for slice_iter<T> {
    type Item = const T*;
    o<const T*> next(slice_iter<T>* this) {
        usize total = len(this->items);
        if (this->index >= total) { return o::none; }
        usize current = this->index;
        this->index += 1usize;
        const T* item = &this->items[current];
        return o::some(item);
    }
};

@generic<T>
slice_iter<T> of_slice(const T[] items) {
    return slice_iter<T> { .items = items, .index = 0usize };
}

struct range_i32 {
    i32 next_value;
    i32 limit;
};

impl core::Iterator for range_i32 {
    type Item = i32;
    o<i32> next(range_i32* this) {
        if (this->next_value >= this->limit) { return o::none; }
        i32 value = this->next_value;
        this->next_value += 1;
        return o::some(value);
    }
};

range_i32 range(i32 low, i32 high) {
    return range_i32 { .next_value = low, .limit = high };
}

struct range_usize {
    usize next_value;
    usize limit;
};

impl core::Iterator for range_usize {
    type Item = usize;
    o<usize> next(range_usize* this) {
        if (this->next_value >= this->limit) { return o::none; }
        usize value = this->next_value;
        this->next_value += 1usize;
        return o::some(value);
    }
};

range_usize range_of_usize(usize low, usize high) {
    return range_usize { .next_value = low, .limit = high };
}

/* R-SLIB-ITER-0002: lazy adapters. Each adapter owns its inner iterator and borrows the
   closure it applies; the closure receives every item by value. */
@generic<I: core::Iterator, U, F: fn(I::Item) -> U>
struct map_iter {
    I inner;
    const F* function;
};

@generic<I: core::Iterator, U, F: fn(I::Item) -> U>
impl core::Iterator for map_iter<I, U, F> {
    type Item = U;
    o<U> next(map_iter<I, U, F>* this) {
        o<I::Item> item = this->inner.next();
        switch (move item) {
        case variant o::some(move value):
            U mapped = this->function(move value);
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

@generic<I: core::Iterator, U, F: fn(I::Item) -> o<U>>
struct filter_map_iter {
    I inner;
    const F* function;
};

@generic<I: core::Iterator, U, F: fn(I::Item) -> o<U>>
impl core::Iterator for filter_map_iter<I, U, F> {
    type Item = U;
    o<U> next(filter_map_iter<I, U, F>* this) {
        while (true) {
            o<I::Item> item = this->inner.next();
            switch (move item) {
            case variant o::some(move value):
                o<U> kept = this->function(move value);
                switch (move kept) {
                case variant o::some(move mapped):
                    return o::some(move mapped);
                case variant o::none:
                    break;
                }
                break;
            case variant o::none:
                return o::none;
            }
        }
        return o::none;
    }
};

@generic<I: core::Iterator, U, F: fn(I::Item) -> o<U>>
filter_map_iter<I, U, F> filter_map(I inner, const F* function) {
    return filter_map_iter<I, U, F> { .inner = move inner, .function = function };
}

@generic<I: core::Iterator>
struct take_iter {
    I inner;
    usize remaining;
};

@generic<I: core::Iterator>
impl core::Iterator for take_iter<I> {
    type Item = I::Item;
    o<I::Item> next(take_iter<I>* this) {
        if (this->remaining == 0usize) { return o::none; }
        this->remaining -= 1usize;
        o<I::Item> item = this->inner.next();
        return move item;
    }
};

@generic<I: core::Iterator>
take_iter<I> take(I inner, usize limit) {
    return take_iter<I> { .inner = move inner, .remaining = limit };
}

@generic<I: core::Iterator>
struct skip_iter {
    I inner;
    usize pending;
};

@generic<I: core::Iterator>
impl core::Iterator for skip_iter<I> {
    type Item = I::Item;
    o<I::Item> next(skip_iter<I>* this) {
        while (this->pending > 0usize) {
            this->pending -= 1usize;
            o<I::Item> skipped = this->inner.next();
            switch (move skipped) {
            case variant o::some(move value):
                break;
            case variant o::none:
                return o::none;
            }
        }
        o<I::Item> item = this->inner.next();
        return move item;
    }
};

@generic<I: core::Iterator>
skip_iter<I> skip(I inner, usize amount) {
    return skip_iter<I> { .inner = move inner, .pending = amount };
}

@generic<T>
struct indexed {
    usize index;
    T value;
};

@generic<I: core::Iterator>
struct enumerate_iter {
    I inner;
    usize next_index;
};

@generic<I: core::Iterator>
impl core::Iterator for enumerate_iter<I> {
    type Item = indexed<I::Item>;
    o<indexed<I::Item>> next(enumerate_iter<I>* this) {
        o<I::Item> item = this->inner.next();
        switch (move item) {
        case variant o::some(move value):
            usize index = this->next_index;
            this->next_index += 1usize;
            indexed<I::Item> entry = indexed<I::Item> { .index = index, .value = move value };
            return o::some(move entry);
        case variant o::none:
            return o::none;
        }
    }
};

@generic<I: core::Iterator>
enumerate_iter<I> enumerate(I inner) {
    return enumerate_iter<I> { .inner = move inner, .next_index = 0usize };
}

@generic<L, R>
struct pair {
    L left;
    R right;
};

@generic<A: core::Iterator, B: core::Iterator>
struct zip_iter {
    A left;
    B right;
};

@generic<A: core::Iterator, B: core::Iterator>
impl core::Iterator for zip_iter<A, B> {
    type Item = pair<A::Item, B::Item>;
    o<pair<A::Item, B::Item>> next(zip_iter<A, B>* this) {
        o<A::Item> left = this->left.next();
        switch (move left) {
        case variant o::some(move left_value):
            o<B::Item> right = this->right.next();
            switch (move right) {
            case variant o::some(move right_value):
                pair<A::Item, B::Item> both =
                    pair<A::Item, B::Item> { .left = move left_value, .right = move right_value };
                return o::some(move both);
            case variant o::none:
                return o::none;
            }
            return o::none;
        case variant o::none:
            return o::none;
        }
    }
};

@generic<A: core::Iterator, B: core::Iterator>
zip_iter<A, B> zip(A left, B right) {
    return zip_iter<A, B> { .left = move left, .right = move right };
}

@generic<I: core::Iterator>
struct chain_iter {
    I first;
    I second;
    bool on_second;
};

@generic<I: core::Iterator>
impl core::Iterator for chain_iter<I> {
    type Item = I::Item;
    o<I::Item> next(chain_iter<I>* this) {
        if (this->on_second == false) {
            o<I::Item> item = this->first.next();
            switch (move item) {
            case variant o::some(move value):
                return o::some(move value);
            case variant o::none:
                this->on_second = true;
                break;
            }
        }
        o<I::Item> rest = this->second.next();
        return move rest;
    }
};

@generic<I: core::Iterator>
chain_iter<I> chain(I first, I second) {
    return chain_iter<I> { .first = move first, .second = move second, .on_second = false };
}

/* R-SLIB-ITER-0003: consumers drive an iterator to its end or to the first decisive item. */
@generic<I: core::Iterator>
usize count(I inner) {
    usize total = 0usize;
    while (true) {
        o<I::Item> item = inner.next();
        switch (move item) {
        case variant o::some(move value):
            total += 1usize;
            break;
        case variant o::none:
            return total;
        }
    }
    return total;
}

@generic<I: core::Iterator, A, F: fn(A, I::Item) -> A>
A fold(I inner, A initial, const F* function) {
    A accumulator = move initial;
    while (true) {
        o<I::Item> item = inner.next();
        switch (move item) {
        case variant o::some(move value):
            A next = function(move accumulator, move value);
            accumulator = move next;
            break;
        case variant o::none:
            return move accumulator;
        }
    }
    return move accumulator;
}

@generic<I: core::Iterator, F: fn(I::Item) -> bool>
bool any(I inner, const F* predicate) {
    while (true) {
        o<I::Item> item = inner.next();
        switch (move item) {
        case variant o::some(move value):
            bool matched = predicate(move value);
            if (matched == true) { return true; }
            break;
        case variant o::none:
            return false;
        }
    }
    return false;
}

@generic<I: core::Iterator, F: fn(I::Item) -> bool>
bool all(I inner, const F* predicate) {
    while (true) {
        o<I::Item> item = inner.next();
        switch (move item) {
        case variant o::some(move value):
            bool matched = predicate(move value);
            if (matched == false) { return false; }
            break;
        case variant o::none:
            return true;
        }
    }
    return true;
}

@generic<I: core::Iterator, F: fn(I::Item) -> bool>
o<usize> position(I inner, const F* predicate) {
    usize index = 0usize;
    while (true) {
        o<I::Item> item = inner.next();
        switch (move item) {
        case variant o::some(move value):
            bool matched = predicate(move value);
            if (matched == true) { return o::some(index); }
            index += 1usize;
            break;
        case variant o::none:
            return o::none;
        }
    }
    return o::none;
}

@generic<I: core::Iterator, U, F: fn(I::Item) -> o<U>>
o<U> find_map(I inner, const F* function) {
    while (true) {
        o<I::Item> item = inner.next();
        switch (move item) {
        case variant o::some(move value):
            o<U> found = function(move value);
            switch (move found) {
            case variant o::some(move mapped):
                return o::some(move mapped);
            case variant o::none:
                break;
            }
            break;
        case variant o::none:
            return o::none;
        }
    }
    return o::none;
}

@generic<I: core::Iterator>
o<I::Item> last(I inner) {
    o<I::Item> result = o::none;
    while (true) {
        o<I::Item> item = inner.next();
        switch (move item) {
        case variant o::some(move value):
            result = o::some(move value);
            break;
        case variant o::none:
            return move result;
        }
    }
    return move result;
}

@generic<I: core::Iterator>
o<I::Item> nth(I inner, usize index) {
    usize remaining = index;
    while (true) {
        o<I::Item> item = inner.next();
        switch (move item) {
        case variant o::some(move value):
            if (remaining == 0usize) { return o::some(move value); }
            remaining -= 1usize;
            break;
        case variant o::none:
            return o::none;
        }
    }
    return o::none;
}

@generic<I: core::Iterator>
array<I::Item> collect_array(I inner) throws std.array::push_error<I::Item> {
    array<I::Item> result = std.array::create::<I::Item>();
    while (true) {
        o<I::Item> item = inner.next();
        switch (move item) {
        case variant o::some(move value):
            result.push(move value);
            break;
        case variant o::none:
            return move result;
        }
    }
    return move result;
}

@generic<I: core::Iterator>
list<I::Item> collect_list(I inner) throws std.list::push_error<I::Item> {
    list<I::Item> result = std.list::create::<I::Item>();
    while (true) {
        o<I::Item> item = inner.next();
        switch (move item) {
        case variant o::some(move value):
            I::Item* stored = result.push_back(move value);
            const I::Item* observed = stored;
            observed as void;
            break;
        case variant o::none:
            return move result;
        }
    }
    return move result;
}

/* R-SLIB-ITER-0004: the elements of a slice from the last to the first. */
@generic<T>
struct reversed_iter {
    const T[] items;
    usize remaining;
};

@generic<T>
impl core::Iterator for reversed_iter<T> {
    type Item = const T*;
    o<const T*> next(reversed_iter<T>* this) {
        if (this->remaining == 0usize) { return o::none; }
        this->remaining -= 1usize;
        const T* item = &this->items[this->remaining];
        return o::some(item);
    }
};

@generic<T>
reversed_iter<T> reversed(const T[] items) {
    return reversed_iter<T> { .items = items, .remaining = len(items) };
}

/* R-SLIB-ITER-0004: consecutive pieces of a slice of `size` elements, the last one possibly
   shorter (chunks), and every run of `size` adjacent elements (windows); a size of zero yields
   nothing. */
@generic<T>
struct chunks_iter {
    const T[] items;
    usize position;
    usize size;
};

@generic<T>
impl core::Iterator for chunks_iter<T> {
    type Item = const T[];
    o<const T[]> next(chunks_iter<T>* this) {
        usize total = len(this->items);
        if (this->size == 0usize || this->position >= total) { return o::none; }
        usize start = this->position;
        usize end = total;
        if (total - start > this->size) { end = start + this->size; }
        this->position = end;
        return o::some(this->items[start..end]);
    }
};

@generic<T>
chunks_iter<T> chunks(const T[] items, usize size) {
    return chunks_iter<T> { .items = items, .position = 0usize, .size = size };
}

@generic<T>
struct windows_iter {
    const T[] items;
    usize position;
    usize size;
};

@generic<T>
impl core::Iterator for windows_iter<T> {
    type Item = const T[];
    o<const T[]> next(windows_iter<T>* this) {
        usize total = len(this->items);
        if (this->size == 0usize || this->size > total ||
            this->position > total - this->size) {
            return o::none;
        }
        usize start = this->position;
        this->position = start + 1usize;
        return o::some(this->items[start..start + this->size]);
    }
};

@generic<T>
windows_iter<T> windows(const T[] items, usize size) {
    return windows_iter<T> { .items = items, .position = 0usize, .size = size };
}

/* R-SLIB-ITER-0004: the items for which the predicate holds; the predicate borrows each item. */
@generic<I: core::Iterator, F: fn(const I::Item*) -> bool>
struct filter_iter {
    I inner;
    const F* predicate;
};

@generic<I: core::Iterator, F: fn(const I::Item*) -> bool>
impl core::Iterator for filter_iter<I, F> {
    type Item = I::Item;
    o<I::Item> next(filter_iter<I, F>* this) {
        while (true) {
            o<I::Item> item = this->inner.next();
            switch (move item) {
            case variant o::some(move value):
                bool kept = this->predicate(&value);
                if (kept == true) { return o::some(move value); }
                break;
            case variant o::none:
                return o::none;
            }
        }
        return o::none;
    }
};

@generic<I: core::Iterator, F: fn(const I::Item*) -> bool>
filter_iter<I, F> filter(I inner, const F* predicate) {
    return filter_iter<I, F> { .inner = move inner, .predicate = predicate };
}

/* R-SLIB-ITER-0005: the least and the greatest item under std.cmp::Ordered, the first of equal
   ones, or none for no item. */
@generic<I: core::Iterator, I::Item: std.cmp::Ordered>
o<I::Item> min(I inner) {
    o<I::Item> best = o::none;
    while (true) {
        o<I::Item> item = inner.next();
        switch (move item) {
        case variant o::some(move value):
            bool better = true;
            switch (best) {
            case variant o::some(current):
                better = value.cmp(current) == std.cmp::ordering::less;
            case variant o::none:
                better = true;
            }
            if (better == true) {
                best = o::some(move value);
            } else {
                I::Item passed = move value;
            }
            break;
        case variant o::none:
            return move best;
        }
    }
    return move best;
}

@generic<I: core::Iterator, I::Item: std.cmp::Ordered>
o<I::Item> max(I inner) {
    o<I::Item> best = o::none;
    while (true) {
        o<I::Item> item = inner.next();
        switch (move item) {
        case variant o::some(move value):
            bool better = true;
            switch (best) {
            case variant o::some(current):
                better = value.cmp(current) == std.cmp::ordering::greater;
            case variant o::none:
                better = true;
            }
            if (better == true) {
                best = o::some(move value);
            } else {
                I::Item passed = move value;
            }
            break;
        case variant o::none:
            return move best;
        }
    }
    return move best;
}

/* R-SLIB-ITER-0005: numbers that add; an integer sum that overflows panics as Core addition
   does. */
trait Summable : copy {
    Self plus(Self this, Self other);
};

impl Summable for i32 { i32 plus(i32 this, i32 other) { return this + other; } };
impl Summable for i64 { i64 plus(i64 this, i64 other) { return this + other; } };
impl Summable for isize { isize plus(isize this, isize other) { return this + other; } };
impl Summable for u32 { u32 plus(u32 this, u32 other) { return this + other; } };
impl Summable for u64 { u64 plus(u64 this, u64 other) { return this + other; } };
impl Summable for usize { usize plus(usize this, usize other) { return this + other; } };
impl Summable for f32 { f32 plus(f32 this, f32 other) { return this + other; } };
impl Summable for f64 { f64 plus(f64 this, f64 other) { return this + other; } };

/* R-SLIB-ITER-0005: start plus every item, in order. */
@generic<I: core::Iterator, I::Item: Summable>
I::Item sum(I inner, I::Item start) {
    I::Item total = start;
    while (true) {
        o<I::Item> item = inner.next();
        switch (move item) {
        case variant o::some(move value):
            total = total.plus(value);
            break;
        case variant o::none:
            return total;
        }
    }
    return total;
}
