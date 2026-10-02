module test.codegen.async_call_deref;

/* R-BORROW-0005, spec 25.1 (L13.3): an async frame dereferences the borrow a call returns
   directly: it holds the borrow in a hidden local of the frame for the one expression, so an
   assignment, a compound assignment, a read, a field of the referent and a new borrow of it
   all work, also in a generic async instance. */

struct Item {
    i32 count;
    i32[2] marks;
};

struct Shelf {
    Item[3] items;
};

Item* Shelf::item(Shelf* this, usize index) {
    return &this->items[index];
}

const Item* Shelf::view(const Shelf* this, usize index) {
    return &this->items[index];
}

@generic<T>
T* first(T[] values) {
    return &values[0];
}

@generic<T: copy & send & unborrowed>
protected async i32 bump(T seed) {
    i32[3] numbers = {1, 2, 3};
    *first::<i32>(numbers[0usize..3usize]) += 10;
    seed as void;
    return numbers[0];
}

protected async i32 work() {
    Shelf shelf = Shelf { .items = {Item { .count = 1, .marks = {0, 0} },
                                    Item { .count = 2, .marks = {0, 0} },
                                    Item { .count = 3, .marks = {0, 0} }} };
    (*shelf.item(0usize)).count += 5;
    (*shelf.item(1usize)).marks[1] = 9;
    (*shelf.item(2usize)).count = 30;
    i32 viewed = (*shelf.view(1usize)).marks[1];
    const Item* kept = &*shelf.view(2usize);
    i32 total = shelf.items[0].count + viewed + (*kept).count;
    try {
        total += await bump::<i32>(0);
    } catch (std.async::start_error failure) {
        failure as void;
        return -1;
    }
    return total;
}

async i32 main() {
    try {
        i32 total = await work();
        return total - 56;
    } catch (std.async::start_error failure) {
        failure as void;
        return 9;
    }
}
