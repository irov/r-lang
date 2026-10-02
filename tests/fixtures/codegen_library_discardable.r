module test.codegen.library_discardable;

import std.set;
import std.deque;
import std.heap;
import std.sorted;

// Library R-LIB-0026: mutation results in statement position are discarded (Core R-FUNC-0021).
i32 exercise() throws std.dict::insert_error<i32, i32>, std.dict::insert_error<i32, bool>,
                      std.array::push_error<i32>, std.list::push_error<i32> {
    dict<i32, i32> d = std.dict::create::<i32, i32>();
    std.dict::insert(&d, 1, 2);
    d.insert(1, 3);
    i32 one = 1;
    bool present = d.contains(&one);
    if (present == false) { return 1; }
    std.dict::remove(&d, &one);
    d.remove(&one);
    bool still = d.contains(&one);
    if (still == true) { return 2; }

    array<i32> a = std.array::create::<i32>();
    std.array::push(&a, 1);
    std.array::push(&a, 2);
    std.array::push(&a, 3);
    a.pop();
    std.array::remove(&a, 0usize);
    usize remaining = len(a);
    if (remaining != 1usize) { return 3; }

    list<i32> l = std.list::create::<i32>();
    { i32* node = std.list::push_back(&l, 5); *node += 1; }
    { i32* node = std.list::push_front(&l, 4); *node += 1; }
    l.pop_front();
    std.list::pop_back(&l);

    i32 slot = 1;
    core::replace(&slot, 7);
    if (slot != 7) { return 4; }

    std.set::set<i32> s = std.set::set<i32>::create();
    s.insert(3);
    s.insert(3);
    usize members = s.count();
    if (members != 1usize) { return 5; }
    i32 three = 3;
    s.remove(&three);
    bool empty = s.is_empty();
    if (empty == false) { return 6; }

    std.deque::deque<i32> q = std.deque::deque<i32>::create();
    q.push_back(1);
    q.push_back(2);
    q.push_back(3);
    q.pop_front();
    q.pop_back();
    usize queued = q.count();
    if (queued != 1usize) { return 7; }

    std.heap::heap<i32> h = std.heap::heap<i32>::create();
    h.push(5);
    h.push(9);
    h.pop();
    usize heaped = h.count();
    if (heaped != 1usize) { return 8; }

    std.sorted::set<i32> ss = std.sorted::set<i32>::create();
    ss.insert(2);
    ss.insert(2);
    usize sorted_members = ss.count();
    if (sorted_members != 1usize) { return 9; }
    i32 two = 2;
    ss.remove(&two);
    usize sorted_left = ss.count();
    if (sorted_left != 0usize) { return 10; }

    std.sorted::map<i32, i32> m = std.sorted::map<i32, i32>::create();
    m.insert(1, 10);
    m.insert(1, 11);
    usize mapped = m.count();
    if (mapped != 1usize) { return 11; }
    m.remove(&one);
    usize mapped_left = m.count();
    if (mapped_left != 0usize) { return 12; }
    return 0;
}

i32 main() {
    try {
        try {
            i32 status = exercise();
            return status;
        } catch (std.dict::insert_error<i32, i32> failure) { failure as void; throw TestAssertionFailed {.code = 21}; }
          catch (std.dict::insert_error<i32, bool> failure) { failure as void; throw TestAssertionFailed {.code = 22}; }
          catch (std.array::push_error<i32> failure) { failure as void; throw TestAssertionFailed {.code = 23}; }
          catch (std.list::push_error<i32> failure) { failure as void; throw TestAssertionFailed {.code = 24}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
