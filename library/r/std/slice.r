module std.slice;

import std.cmp;

/* R-SLIB-SLICE-0001: searches over a shared slice; every result borrows from the slice. */
@generic<T: std.cmp::Equal>
bool contains(const T[] items, const T* value) {
    for (const T* item in &items) {
        bool same = item->eq(value);
        if (same == true) { return true; }
    }
    return false;
}

@generic<T: std.cmp::Equal>
o<usize> index_of(const T[] items, const T* value) {
    usize count = len(items);
    usize index = 0usize;
    while (index < count) {
        bool same = items[index].eq(value);
        if (same == true) { return o::some(index); }
        index += 1usize;
    }
    return o::none;
}

@generic<T>
o<const T*> first(const T[] items) {
    usize count = len(items);
    if (count == 0usize) { return o::none; }
    const T* item = &items[0usize];
    return o::some(item);
}

@generic<T>
o<const T*> last(const T[] items) {
    usize count = len(items);
    if (count == 0usize) { return o::none; }
    const T* item = &items[count - 1usize];
    return o::some(item);
}

@generic<T: std.cmp::Ordered>
o<const T*> min_of(const T[] items) {
    usize count = len(items);
    if (count == 0usize) { return o::none; }
    usize best = 0usize;
    usize index = 1usize;
    while (index < count) {
        std.cmp::ordering order = items[index].cmp(&items[best]);
        if (order == std.cmp::ordering::less) { best = index; }
        index += 1usize;
    }
    const T* item = &items[best];
    return o::some(item);
}

@generic<T: std.cmp::Ordered>
o<const T*> max_of(const T[] items) {
    usize count = len(items);
    if (count == 0usize) { return o::none; }
    usize best = 0usize;
    usize index = 1usize;
    while (index < count) {
        std.cmp::ordering order = items[index].cmp(&items[best]);
        if (order == std.cmp::ordering::greater) { best = index; }
        index += 1usize;
    }
    const T* item = &items[best];
    return o::some(item);
}

@generic<T: std.cmp::Ordered>
bool is_sorted(const T[] items) {
    usize count = len(items);
    usize index = 1usize;
    while (index < count) {
        std.cmp::ordering order = items[index - 1usize].cmp(&items[index]);
        if (order == std.cmp::ordering::greater) { return false; }
        index += 1usize;
    }
    return true;
}

/* The index of `value` in a sorted slice, or none. */
@generic<T: std.cmp::Ordered>
o<usize> binary_search(const T[] items, const T* value) {
    usize low = 0usize;
    usize high = len(items);
    while (low < high) {
        usize middle = low + (high - low) / 2usize;
        std.cmp::ordering order = items[middle].cmp(value);
        if (order == std.cmp::ordering::equal) { return o::some(middle); }
        if (order == std.cmp::ordering::less) {
            low = middle + 1usize;
        } else {
            high = middle;
        }
    }
    return o::none;
}

/* R-SLIB-SLICE-0002: in-place rearrangements of an exclusive slice. A Copy element is copied;
   any other element is exchanged in place by core::swap (Core R-OWN-0019) and shall be
   unborrowed. An element type that is neither is rejected when the function is instantiated. */
@generic<T>
protected bool requires_unborrowed() {
    panic("std.slice: an element that is not Copy shall be unborrowed");
}

@generic<T>
void swap(T[] items, usize left, usize right) {
    @if (T is copy) {
        T held = items[left];
        items[left] = items[right];
        items[right] = held;
    } @else @if (T is unborrowed) {
        usize count = len(items);
        if (left >= count || right >= count) {
            panic("std.slice::swap: index out of range");
        }
        if (left != right) {
            /* Two distinct elements of one exclusive slice never alias. */
            unsafe {
                raw T* left_place = &items[left] as raw T*;
                raw T* right_place = &items[right] as raw T*;
                T[] one = core::slice_from_raw_parts_mut(left_place, 1usize);
                T[] other = core::slice_from_raw_parts_mut(right_place, 1usize);
                core::swap(&one[0usize], &other[0usize]);
            }
        }
    } @else {
        @if (requires_unborrowed::<T>() == true) { }
    }
}

@generic<T>
void reverse(T[] items) {
    usize count = len(items);
    if (count < 2usize) { return; }
    usize low = 0usize;
    usize high = count - 1usize;
    while (low < high) {
        swap(items, low, high);
        low += 1usize;
        high -= 1usize;
    }
}

/* Reverses items[low..high] in place. */
@generic<T>
protected void reverse_range(T[] items, usize low, usize high) {
    if (high - low < 2usize) { return; }
    usize front = low;
    usize back = high - 1usize;
    while (front < back) {
        swap(items, front, back);
        front += 1usize;
        back -= 1usize;
    }
}

/* Moves the first `count` elements to the end, keeping the order of both parts. */
@generic<T>
void rotate_left(T[] items, usize count) {
    usize total = len(items);
    if (count > total) {
        panic("std.slice::rotate_left: count exceeds the slice length");
    }
    reverse_range(items, 0usize, count);
    reverse_range(items, count, total);
    reverse_range(items, 0usize, total);
}

/* Moves the last `count` elements to the front, keeping the order of both parts. */
@generic<T>
void rotate_right(T[] items, usize count) {
    usize total = len(items);
    if (count > total) {
        panic("std.slice::rotate_right: count exceeds the slice length");
    }
    rotate_left(items, total - count);
}

/* Moves the element at `root` down until the heap property holds below `end`. */
@generic<T: std.cmp::Ordered>
void sift_down(T[] items, usize root, usize end) {
    usize current = root;
    while (true) {
        usize child = current * 2usize + 1usize;
        if (child >= end) { return; }
        usize sibling = child + 1usize;
        if (sibling < end) {
            std.cmp::ordering order = items[child].cmp(&items[sibling]);
            if (order == std.cmp::ordering::less) { child = sibling; }
        }
        std.cmp::ordering against = items[current].cmp(&items[child]);
        if (against != std.cmp::ordering::less) { return; }
        swap(items, current, child);
        current = child;
    }
}

/* Heapsort: ascending order, in place, without recursion or allocation; not stable. */
@generic<T: std.cmp::Ordered>
void sort(T[] items) {
    usize count = len(items);
    if (count < 2usize) { return; }
    usize start = count / 2usize;
    while (start > 0usize) {
        start -= 1usize;
        sift_down(items, start, count);
    }
    usize end = count;
    while (end > 1usize) {
        end -= 1usize;
        swap(items, 0usize, end);
        sift_down(items, 0usize, end);
    }
}

@generic<T, F: fn(const T*, const T*) -> std.cmp::ordering>
protected void sift_down_by(T[] items, usize root, usize end, const F* compare) {
    usize current = root;
    while (true) {
        usize child = current * 2usize + 1usize;
        if (child >= end) { return; }
        usize sibling = child + 1usize;
        if (sibling < end) {
            std.cmp::ordering order = compare(&items[child], &items[sibling]);
            if (order == std.cmp::ordering::less) { child = sibling; }
        }
        std.cmp::ordering against = compare(&items[current], &items[child]);
        if (against != std.cmp::ordering::less) { return; }
        swap(items, current, child);
        current = child;
    }
}

/* Heapsort by a comparison: ascending in the order `compare` defines; not stable. */
@generic<T, F: fn(const T*, const T*) -> std.cmp::ordering>
void sort_by(T[] items, const F* compare) {
    usize count = len(items);
    if (count < 2usize) { return; }
    usize start = count / 2usize;
    while (start > 0usize) {
        start -= 1usize;
        sift_down_by(items, start, count, compare);
    }
    usize end = count;
    while (end > 1usize) {
        end -= 1usize;
        swap(items, 0usize, end);
        sift_down_by(items, 0usize, end, compare);
    }
}
