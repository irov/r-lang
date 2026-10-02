module std.sorted;

import std.cmp;
import std.slice;

/* R-SLIB-SORTED-0001: a sorted set of Copy keys kept in an array; lookups use binary search
   and insertion keeps the order. */
@generic<K: std.cmp::Ordered & copy & unborrowed>
struct set {
    array<K> keys;
};

@generic<K: std.cmp::Ordered & copy & unborrowed>
set<K> set<K>::create() {
    array<K> keys = std.array::create::<K>();
    return set<K> { .keys = move keys };
}

@generic<K: std.cmp::Ordered & copy & unborrowed>
usize set<K>::count(const set<K>* this) {
    usize total = len(this->keys);
    return total;
}

/* The position of the first key not less than `key`. */
@generic<K: std.cmp::Ordered & copy & unborrowed>
usize set<K>::lower_bound(const set<K>* this, const K* key) {
    const K[] view = std.array::as_slice(&this->keys);
    usize low = 0usize;
    usize high = len(view);
    while (low < high) {
        usize middle = low + (high - low) / 2usize;
        std.cmp::ordering order = view[middle].cmp(key);
        if (order == std.cmp::ordering::less) {
            low = middle + 1usize;
        } else {
            high = middle;
        }
    }
    return low;
}

@generic<K: std.cmp::Ordered & copy & unborrowed>
bool set<K>::contains(const set<K>* this, const K* key) {
    const K[] view = std.array::as_slice(&this->keys);
    o<usize> found = std.slice::binary_search(view, key);
    switch (found) {
    case variant o::some(index):
        return true;
    case variant o::none:
        return false;
    }
}

/* Inserts the key at its sorted position; true when it was absent. */
@generic<K: std.cmp::Ordered & copy & unborrowed>
@discardable
bool set<K>::insert(set<K>* this, K key) throws std.array::push_error<K> {
    usize position = this->lower_bound(&key);
    usize total = len(this->keys);
    if (position < total) {
        const K[] view = std.array::as_slice(&this->keys);
        std.cmp::ordering order = view[position].cmp(&key);
        if (order == std.cmp::ordering::equal) { return false; }
    }
    std.array::push(&this->keys, key);
    K[] mutable = std.array::as_slice_mut(&this->keys);
    usize index = total;
    while (index > position) {
        std.slice::swap(mutable, index, index - 1usize);
        index -= 1usize;
    }
    return true;
}

/* Removes the key; true when it was present. */
@generic<K: std.cmp::Ordered & copy & unborrowed>
@discardable
bool set<K>::remove(set<K>* this, const K* key) {
    const K[] view = std.array::as_slice(&this->keys);
    o<usize> found = std.slice::binary_search(view, key);
    switch (found) {
    case variant o::some(index):
        std.array::remove(&this->keys, *index);
        return true;
    case variant o::none:
        return false;
    }
}

@generic<K: std.cmp::Ordered & copy & unborrowed>
const K[] set<K>::as_slice(const set<K>* this) {
    const K[] view = std.array::as_slice(&this->keys);
    return view;
}

/* R-SLIB-SORTED-0002: a sorted map from Copy keys to Copy values, with the keys and values
   held in two arrays at matching positions. */
@generic<K: std.cmp::Ordered & copy & unborrowed, V: copy & unborrowed>
struct map {
    array<K> keys;
    array<V> values;
};

@generic<K: std.cmp::Ordered & copy & unborrowed, V: copy & unborrowed>
map<K, V> map<K, V>::create() {
    array<K> keys = std.array::create::<K>();
    array<V> values = std.array::create::<V>();
    return map<K, V> { .keys = move keys, .values = move values };
}

@generic<K: std.cmp::Ordered & copy & unborrowed, V: copy & unborrowed>
usize map<K, V>::count(const map<K, V>* this) {
    usize total = len(this->keys);
    return total;
}

@generic<K: std.cmp::Ordered & copy & unborrowed, V: copy & unborrowed>
usize map<K, V>::lower_bound(const map<K, V>* this, const K* key) {
    const K[] view = std.array::as_slice(&this->keys);
    usize low = 0usize;
    usize high = len(view);
    while (low < high) {
        usize middle = low + (high - low) / 2usize;
        std.cmp::ordering order = view[middle].cmp(key);
        if (order == std.cmp::ordering::less) {
            low = middle + 1usize;
        } else {
            high = middle;
        }
    }
    return low;
}

@generic<K: std.cmp::Ordered & copy & unborrowed, V: copy & unborrowed>
o<const V*> map<K, V>::get(const map<K, V>* this, const K* key) {
    const K[] view = std.array::as_slice(&this->keys);
    o<usize> found = std.slice::binary_search(view, key);
    switch (found) {
    case variant o::some(index):
        o<const V*> value = std.array::get(&this->values, *index);
        return move value;
    case variant o::none:
        return o::none;
    }
}

@generic<K: std.cmp::Ordered & copy & unborrowed, V: copy & unborrowed>
bool map<K, V>::contains(const map<K, V>* this, const K* key) {
    const K[] view = std.array::as_slice(&this->keys);
    o<usize> found = std.slice::binary_search(view, key);
    switch (found) {
    case variant o::some(index):
        return true;
    case variant o::none:
        return false;
    }
}

/* Inserts or replaces; the previous value of an existing key is returned. */
@generic<K: std.cmp::Ordered & copy & unborrowed, V: copy & unborrowed>
@discardable
o<V> map<K, V>::insert(map<K, V>* this, K key, V value)
    throws std.array::push_error<K>, std.array::push_error<V> {
    usize position = this->lower_bound(&key);
    usize total = len(this->keys);
    if (position < total) {
        const K[] view = std.array::as_slice(&this->keys);
        std.cmp::ordering order = view[position].cmp(&key);
        if (order == std.cmp::ordering::equal) {
            V[] slots = std.array::as_slice_mut(&this->values);
            V previous = slots[position];
            slots[position] = value;
            return o::some(previous);
        }
    }
    std.array::push(&this->keys, key);
    /* The key leaves again when its value finds no room, so the arrays stay in step. */
    try {
        std.array::push(&this->values, value);
    } catch (std.array::push_error<V> failure) {
        o<K> dropped = std.array::pop(&this->keys);
        dropped as void;
        throw failure;
    }
    K[] keys = std.array::as_slice_mut(&this->keys);
    usize index = total;
    while (index > position) {
        std.slice::swap(keys, index, index - 1usize);
        index -= 1usize;
    }
    V[] values = std.array::as_slice_mut(&this->values);
    usize index_2 = total;
    while (index_2 > position) {
        std.slice::swap(values, index_2, index_2 - 1usize);
        index_2 -= 1usize;
    }
    return o::none;
}

/* Removes the key; the removed value is returned. */
@generic<K: std.cmp::Ordered & copy & unborrowed, V: copy & unborrowed>
@discardable
o<V> map<K, V>::remove(map<K, V>* this, const K* key) {
    const K[] view = std.array::as_slice(&this->keys);
    o<usize> found = std.slice::binary_search(view, key);
    switch (found) {
    case variant o::some(index):
        std.array::remove(&this->keys, *index);
        o<V> removed_value = std.array::remove(&this->values, *index);
        return move removed_value;
    case variant o::none:
        return o::none;
    }
}
