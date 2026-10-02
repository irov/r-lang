module test.codegen.swap_clone;
import std.slice;
import std.cmp;

/* R-OWN-0019, R-OWN-0020 (L26): core::swap exchanges two places without dropping; core::clone
   copies Copy values, standard owners, shared owners and hooked nominal types; std.slice
   rearranges elements that are not Copy (Library R-SLIB-SLICE-0002). Every tracked value is
   destroyed exactly once. */
struct Counter { atomic u32 drops; };
struct Tracked { arc Counter counter; i32 value; };
drop(Tracked* self) {
    const Counter* c = &*self->counter;
    core::atomic_fetch_add(&c->drops, 1u32, core::memory_order::relaxed) as void;
}
Tracked Tracked::clone(const Tracked* value) {
    return Tracked {.counter = core::clone(&value->counter), .value = value->value + 1000};
}

struct Label { std.string::string text; };
Label Label::clone(const Label* value) throws std.alloc::alloc_error {
    std.string::string copy = core::clone(&value->text);
    copy.append("'");
    return Label {.text = move copy};
}

struct Record { std.string::string name; i32 rank; };
std.cmp::ordering by_rank(const Record* left, const Record* right) {
    if (left->rank < right->rank) { return std.cmp::ordering::less; }
    if (left->rank > right->rank) { return std.cmp::ordering::greater; }
    return std.cmp::ordering::equal;
}

@generic<T: clone & unborrowed>
T copy_of(const T* value) throws std.alloc::alloc_error {
    return core::clone(value);
}

u32 drops(const (arc Counter)* counter) {
    const Counter* c = &**counter;
    return core::atomic_load(&c->drops, core::memory_order::relaxed);
}

i32 swaps() throws TestAssertionFailed, std.alloc::alloc_error {
    i32 a = 1;
    i32 b = 2;
    core::swap(&a, &b);
    if (a != 2 || b != 1) { throw TestAssertionFailed {.code = 1}; }
    std.string::string left = std.string::from_str("left");
    std.string::string right = std.string::from_str("right!");
    core::swap(&left, &right);
    if (left.len() != 6usize || right.len() != 4usize) { throw TestAssertionFailed {.code = 2}; }
    arc Counter counter = new arc Counter {.drops = 0u32};
    Tracked first = {.counter = core::clone(&counter), .value = 1};
    Tracked second = {.counter = core::clone(&counter), .value = 2};
    core::swap(&first, &second);
    if (first.value != 2 || second.value != 1 || drops(&counter) != 0u32) {
        throw TestAssertionFailed {.code = 3};
    }
    i32[3] items = {7, 8, 9};
    i32 outside = 5;
    core::swap(&items[2], &outside);
    if (items[2] != 5 || outside != 9) { throw TestAssertionFailed {.code = 4}; }
    drop first;
    drop second;
    if (drops(&counter) != 2u32) { throw TestAssertionFailed {.code = 5}; }
    return 0;
}

i32 clones() throws TestAssertionFailed, std.error::fault, std.array::push_error<std.string::string>,
    std.array::push_error<Tracked>, std.list::push_error<Label>,
    std.dict::insert_error<u32, array<std.string::string>> {
    str view = "view";
    str view_copy = core::clone(&view);
    if (len(view_copy) != 4usize) { throw TestAssertionFailed {.code = 10}; }
    std.string::string text = std.string::from_str("hello");
    std.string::string text_copy = core::clone(&text);
    text_copy.append(" world");
    if (text.len() != 5usize || text_copy.len() != 11usize) { throw TestAssertionFailed {.code = 11}; }
    array<std.string::string> words = std.array::create();
    words.push(std.string::from_str("a"));
    words.push(std.string::from_str("bb"));
    array<std.string::string> words_copy = core::clone(&words);
    words_copy.push(std.string::from_str("ccc"));
    if (len(words) != 2usize || len(words_copy) != 3usize || words_copy[1].len() != 2usize) {
        throw TestAssertionFailed {.code = 12};
    }
    list<Label> labels = std.list::create();
    Label item = {.text = std.string::from_str("x")};
    Label* stored = labels.push_back(move item);
    stored as void;
    list<Label> labels_copy = core::clone(&labels);
    o<const Label*> front = labels_copy.front();
    switch (front) {
    case variant o::some(label):
        const Label* first_label = *label;
        if (first_label->text.len() != 2usize) { throw TestAssertionFailed {.code = 13}; }
    case variant o::none: throw TestAssertionFailed {.code = 13};
    }
    dict<u32, array<std.string::string>> table = std.dict::create();
    array<std.string::string> row = std.array::create();
    row.push(std.string::from_str("cell"));
    o<array<std.string::string>> replaced = table.insert(7u32, move row);
    drop replaced;
    dict<u32, array<std.string::string>> table_copy = core::clone(&table);
    if (len(table_copy) != 1usize) { throw TestAssertionFailed {.code = 14}; }
    o<std.string::string> maybe = o::some(std.string::from_str("m"));
    o<std.string::string> maybe_copy = core::clone(&maybe);
    switch (move maybe_copy) {
    case variant o::some(move value): if (value.len() != 1usize) { throw TestAssertionFailed {.code = 15}; }
    case variant o::none: throw TestAssertionFailed {.code = 16};
    }
    own std.string::string* boxed = new std.string::string(std.string::from_str("boxed"));
    own std.string::string* boxed_copy = core::clone(&boxed);
    const std.string::string* inner = &*boxed_copy;
    if (inner->len() != 5usize) { throw TestAssertionFailed {.code = 17}; }
    arc Counter counter = new arc Counter {.drops = 0u32};
    arc Counter counter_copy = core::clone(&counter);
    if (std.arc::strong_count(&counter) != 2usize) { throw TestAssertionFailed {.code = 18}; }
    drop counter_copy;
    std.string::string[2] pair = {std.string::from_str("p"), std.string::from_str("qq")};
    std.string::string[2] pair_copy = core::clone(&pair);
    if (pair_copy[1].len() != 2usize) { throw TestAssertionFailed {.code = 19}; }
    (i32, std.string::string) tuple = (3, std.string::from_str("t"));
    (i32, std.string::string) tuple_copy = core::clone(&tuple);
    const std.string::string* element = &tuple_copy.1;
    if (tuple_copy.0 != 3 || element->len() != 1usize) { throw TestAssertionFailed {.code = 20}; }
    array<Tracked> tracked = std.array::create();
    tracked.push(Tracked {.counter = core::clone(&counter), .value = 1});
    tracked.push(Tracked {.counter = core::clone(&counter), .value = 2});
    array<Tracked> tracked_copy = core::clone(&tracked);
    if (tracked_copy[1].value != 1002) { throw TestAssertionFailed {.code = 21}; }
    drop tracked_copy;
    drop tracked;
    if (drops(&counter) != 4u32) { throw TestAssertionFailed {.code = 22}; }
    Label label = {.text = std.string::from_str("label")};
    Label label_copy = copy_of(&label);
    i32 number = 9;
    i32 number_copy = copy_of(&number);
    if (label_copy.text.len() != 6usize || number_copy != 9) { throw TestAssertionFailed {.code = 23}; }
    return 0;
}

i32 slices() throws TestAssertionFailed, std.error::fault, std.array::push_error<Record>,
    std.array::push_error<std.string::string> {
    array<Record> records = std.array::create();
    records.push(Record {.name = std.string::from_str("carol"), .rank = 3});
    records.push(Record {.name = std.string::from_str("al"), .rank = 1});
    records.push(Record {.name = std.string::from_str("bob"), .rank = 2});
    Record[] view = records.as_slice_mut();
    auto compare = by_rank;
    std.slice::sort_by(view, &compare);
    if (records[0].rank != 1 || records[2].name.len() != 5usize) { throw TestAssertionFailed {.code = 30}; }
    Record[] rotated = records.as_slice_mut();
    std.slice::rotate_left(rotated, 1usize);
    if (records[0].rank != 2 || records[2].rank != 1) { throw TestAssertionFailed {.code = 31}; }
    Record[] back = records.as_slice_mut();
    std.slice::rotate_right(back, 1usize);
    std.slice::reverse(back);
    std.slice::swap(back, 0usize, 2usize);
    if (records[0].rank != 1 || records[2].rank != 3) { throw TestAssertionFailed {.code = 32}; }
    array<std.string::string> names = std.array::create();
    names.push(std.string::from_str("delta"));
    names.push(std.string::from_str("al"));
    std.string::string[] name_view = names.as_slice_mut();
    std.slice::reverse(name_view);
    if (names[0].len() != 2usize) { throw TestAssertionFailed {.code = 33}; }
    i32[5] numbers = {5, 1, 4, 2, 3};
    i32[] slots = &numbers;
    std.slice::sort(slots);
    std.slice::rotate_right(slots, 2usize);
    if (numbers[0] != 4 || numbers[2] != 1) { throw TestAssertionFailed {.code = 34}; }
    return 0;
}

i32 main() {
    try {
        i32 status = swaps();
        status += clones();
        status += slices();
        return status;
    } catch (TestAssertionFailed failure) {
        return failure.code;
    } catch (std.array::push_error<std.string::string> failure) {
        return 90;
    } catch (std.array::push_error<Tracked> failure) {
        return 91;
    } catch (std.array::push_error<Record> failure) {
        return 92;
    } catch (std.list::push_error<Label> failure) {
        return 93;
    } catch (std.dict::insert_error<u32, array<std.string::string>> failure) {
        return 94;
    } catch (std.error::fault failure) {
        return 95;
    }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
