module test.codegen.library_containers;

import std.set;
import std.deque;

i32 main() {
    try {
        try {
            std.set::set<i32> s = std.set::set<i32>::create();
            bool first = s.insert(3);
            bool again = s.insert(3);
            bool second = s.insert(5);
            if (first == false) { throw TestAssertionFailed {.code = 1}; }
            if (again == true) { throw TestAssertionFailed {.code = 2}; }
            if (second == false) { throw TestAssertionFailed {.code = 3}; }
            usize n = s.count();
            if (n != 2usize) { throw TestAssertionFailed {.code = 4}; }
            i32 three = 3;
            bool has = s.contains(&three);
            if (has == false) { throw TestAssertionFailed {.code = 5}; }
            i32 sum = 0;
            std.set::set_iter<i32> it = s.iter();
            for (const i32* v in &it) { sum += *v; }
            if (sum != 8) { throw TestAssertionFailed {.code = 6}; }
            bool gone = s.remove(&three);
            if (gone == false) { throw TestAssertionFailed {.code = 7}; }
            usize after = s.count();
            if (after != 1usize) { throw TestAssertionFailed {.code = 8}; }

            std.deque::deque<i32> d = std.deque::deque<i32>::create();
            d.push_back(2);
            d.push_back(3);
            d.push_front(1);
            usize dn = d.count();
            if (dn != 3usize) { throw TestAssertionFailed {.code = 9}; }
            o<i32> head = d.pop_front();
            switch (head) {
            case variant o::some(h):
                if (*h != 1) { throw TestAssertionFailed {.code = 10}; }
                break;
            case variant o::none:
                throw TestAssertionFailed {.code = 11};
            }
            o<i32> tail = d.pop_back();
            switch (tail) {
            case variant o::some(t):
                if (*t != 3) { throw TestAssertionFailed {.code = 12}; }
                break;
            case variant o::none:
                throw TestAssertionFailed {.code = 13};
            }
            o<const i32*> peek = d.front();
            switch (peek) {
            case variant o::some(p):
                if (**p != 2) { throw TestAssertionFailed {.code = 14}; }
                break;
            case variant o::none:
                throw TestAssertionFailed {.code = 15};
            }
        } catch (std.dict::insert_error<i32, bool> failure) {
            failure as void;
            throw TestAssertionFailed {.code = 20};
        } catch (std.array::push_error<i32> push_failure) {
            push_failure as void;
            throw TestAssertionFailed {.code = 21};
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
