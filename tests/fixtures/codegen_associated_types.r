module test.codegen.associated_types;

/* R-TYPE-0045, R-TYPE-0046: associated types, projections and the core Iterator trait. */

trait Source {
    type Item;
    o<Self::Item> pull(Self* this);
};

struct Counter { i32 next_value; i32 limit; };

impl Source for Counter {
    type Item = i32;
    o<i32> pull(Counter* this) {
        if (this->next_value >= this->limit) { return o::none; }
        i32 value = this->next_value;
        this->next_value += 1;
        return o::some(value);
    }
};

impl core::Iterator for Counter {
    type Item = i32;
    o<i32> next(Counter* this) {
        o<i32> pulled = this->pull();
        return move pulled;
    }
};

@generic<S: Source>
o<S::Item> take_one(S* source) {
    o<S::Item> first = source->pull();
    return move first;
}

@generic<I: core::Iterator>
o<I::Item> first_of(I* it) {
    o<I::Item> v = it->next();
    return move v;
}

i32 main() {
    Counter c = Counter { .next_value = 5, .limit = 7 };
    o<i32> first = take_one(&c);
    o<i32> second = first_of(&c);
    i32 total = 0;
    switch (move first) { case variant o::some(move v): total += v; break; case variant o::none: break; }
    switch (move second) { case variant o::some(move w): total += w; break; case variant o::none: break; }
    return total - 11;
}
