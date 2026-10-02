module std.heap;

import std.cmp;
import std.slice;

/* R-SLIB-HEAP-0001: a binary max-heap over an array of Copy elements ordered by std.cmp. */
@generic<T: std.cmp::Ordered & copy & unborrowed>
struct heap {
    array<T> items;
};

@generic<T: std.cmp::Ordered & copy & unborrowed>
heap<T> heap<T>::create() {
    array<T> items = std.array::create::<T>();
    return heap<T> { .items = move items };
}

@generic<T: std.cmp::Ordered & copy & unborrowed>
usize heap<T>::count(const heap<T>* this) {
    usize total = len(this->items);
    return total;
}

@generic<T: std.cmp::Ordered & copy & unborrowed>
bool heap<T>::is_empty(const heap<T>* this) {
    usize total = len(this->items);
    return total == 0usize;
}

@generic<T: std.cmp::Ordered & copy & unborrowed>
o<const T*> heap<T>::peek(const heap<T>* this) {
    o<const T*> top = std.array::get(&this->items, 0usize);
    return move top;
}

/* Appends and sifts the new element up. */
@generic<T: std.cmp::Ordered & copy & unborrowed>
void heap<T>::push(heap<T>* this, T value) throws std.array::push_error<T> {
    std.array::push(&this->items, value);
    usize index = len(this->items) - 1usize;
    T[] view = std.array::as_slice_mut(&this->items);
    while (index > 0usize) {
        usize parent = (index - 1usize) / 2usize;
        std.cmp::ordering order = view[parent].cmp(&view[index]);
        if (order != std.cmp::ordering::less) { return; }
        std.slice::swap(view, parent, index);
        index = parent;
    }
}

/* Removes and returns the greatest element. */
@generic<T: std.cmp::Ordered & copy & unborrowed>
@discardable
o<T> heap<T>::pop(heap<T>* this) {
    usize total = len(this->items);
    if (total == 0usize) { return o::none; }
    T[] view = std.array::as_slice_mut(&this->items);
    std.slice::swap(view, 0usize, total - 1usize);
    std.slice::sift_down(view, 0usize, total - 1usize);
    o<T> greatest = std.array::pop(&this->items);
    return move greatest;
}
