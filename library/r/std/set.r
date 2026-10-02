module std.set;

/* R-SLIB-SET-0001: a set of keys over the standard dictionary; every element is a dictionary
   key under the key contract, insertion order is the iteration order. */
@generic<T: key & unborrowed>
struct set {
    dict<T, bool> entries;
};

@generic<T: key & unborrowed>
set<T> set<T>::create() {
    dict<T, bool> entries = std.dict::create::<T, bool>();
    return set<T> { .entries = move entries };
}

/* Inserts the value; true when it was absent. */
@generic<T: key & unborrowed>
@discardable
bool set<T>::insert(set<T>* this, T value) throws std.dict::insert_error<T, bool> {
    o<bool> previous = std.dict::insert(&this->entries, move value, true);
    switch (previous) {
    case variant o::some(present):
        return false;
    case variant o::none:
        return true;
    }
}

@generic<T: key & unborrowed>
bool set<T>::contains(const set<T>* this, const T* value) {
    bool found = std.dict::contains(&this->entries, value);
    return found;
}

/* Removes the value; true when it was present. */
@generic<T: key & unborrowed>
@discardable
bool set<T>::remove(set<T>* this, const T* value) {
    o<bool> removed = std.dict::remove(&this->entries, value);
    switch (removed) {
    case variant o::some(present):
        return true;
    case variant o::none:
        return false;
    }
}

@generic<T: key & unborrowed>
usize set<T>::count(const set<T>* this) {
    usize total = len(this->entries);
    return total;
}

@generic<T: key & unborrowed>
bool set<T>::is_empty(const set<T>* this) {
    usize total = len(this->entries);
    return total == 0usize;
}

@generic<T: key & unborrowed>
void set<T>::clear(set<T>* this) {
    std.dict::clear(&this->entries);
}

/* R-SLIB-SET-0002: iteration yields shared borrows of the elements in insertion order. */
@generic<T: key & unborrowed>
struct set_iter {
    std.dict::iter<T, bool> cursor;
};

@generic<T: key & unborrowed>
impl core::Iterator for set_iter<T> {
    type Item = const T*;
    o<const T*> next(set_iter<T>* this) {
        o<std.dict::entry_ref<T, bool>> entry = std.dict::next(&this->cursor);
        switch (entry) {
        case variant o::some(found):
            const T* key = found->key;
            return o::some(key);
        case variant o::none:
            return o::none;
        }
    }
};

@generic<T: key & unborrowed>
set_iter<T> set<T>::iter(const set<T>* this) {
    std.dict::iter<T, bool> cursor = std.dict::iter(&this->entries);
    return set_iter<T> { .cursor = move cursor };
}
