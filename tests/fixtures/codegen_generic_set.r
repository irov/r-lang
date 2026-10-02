module test.codegen.generic_set;

/* R-FUNC-0013 and R-ARRAY-0004: an associated function of a generic owner infers the schema
   prefix from the owner spelling, and len counts the entries of a dict. */

@generic<T: key>
struct set { dict<T, bool> entries; };

@generic<T: key>
set<T> set<T>::create() {
    dict<T, bool> entries = std.dict::create::<T, bool>();
    return set<T> { .entries = move entries };
}

@generic<T: key & unborrowed>
bool set<T>::insert(set<T>* this, T value) throws std.dict::insert_error<T, bool> {
    o<bool> previous = std.dict::insert(&this->entries, move value, true);
    switch (previous) {
    case variant o::some(v):
        v as void;
        return false;
    case variant o::none:
        return true;
    }
}

@generic<T: key>
bool set<T>::contains(const set<T>* this, const T* value) {
    bool found = std.dict::contains(&this->entries, value);
    return found;
}

@generic<T: key>
usize set<T>::count(const set<T>* this) {
    usize n = len(this->entries);
    return n;
}

i32 main() {
    try {
        try {
            set<i32> s = set<i32>::create();
            bool a = s.insert(3);
            bool b = s.insert(3);
            if (a == false) { throw TestAssertionFailed {.code = 1}; }
            if (b == true) { throw TestAssertionFailed {.code = 1}; }
            i32 three = 3;
            bool has = s.contains(&three);
            if (has == false) { throw TestAssertionFailed {.code = 2}; }
            usize n = s.count();
            if (n != 1usize) { throw TestAssertionFailed {.code = 3}; }
        } catch (std.dict::insert_error<i32, bool> f) {
            f as void;
            throw TestAssertionFailed {.code = 4};
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
