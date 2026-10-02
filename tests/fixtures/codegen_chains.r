module test.codegen.chains;

import std.string;

/* R-FUNC-0024 (L19): a call continues after a method declared @chain. */
i32 drop_count(bool bump) {
    static i32 count = 0;
    unsafe {
        if (bump == true) { count += 1; }
        return count;
    }
}

struct Token { i32 id; };
drop(Token* self) { drop_count(true) as void; }

error Invalid { i32 code; };

/* A builder that owns resources: every link consumes the receiver and returns it. */
struct Request { Token token; std.string::string path; i32 retries; };

Request Request::create(i32 id) {
    return Request { .token = Token { .id = id }, .path = std.string::create(), .retries = 0 };
}

@chain Request Request::with_path(Request this, str path)
    throws Invalid, std.alloc::alloc_error {
    throw (len(path) == 0usize) Invalid { .code = 1 };
    this.path = std.string::from_str(path);
    return move this;
}

@chain Request Request::with_retries(Request this, i32 retries) {
    this.retries = retries;
    return move this;
}

i32 Request::finish(Request this) {
    return this.token.id * 100 + (this.path.len() as i32) * 10 + this.retries;
}

i32 build(str path) throws Invalid, std.alloc::alloc_error {
    return Request::create(7).with_retries(2).with_path(path).with_retries(3).finish();
}

/* A borrow chain continues through `->`; @discardable lets the statement end the chain. */
struct Counter { i32 total; };

@discardable @chain Counter* Counter::add(Counter* this, i32 value) {
    this->total += value;
    return this;
}

i32 Counter::value(const Counter* this) { return this->total; }

/* Other suffixes after a @chain call: element, tuple element and field. */
struct Grid { i32[3] cells; };

@chain i32[3] Grid::row(const Grid* this) { return this->cells; }

@chain (i32, bool) Grid::first(const Grid* this) { return (this->cells[0], true); }

@chain Grid Grid::shifted(Grid this, i32 by) {
    for (usize index = 0usize; index < 3usize; index += 1usize) {
        this.cells[index] += by;
    }
    return move this;
}

/* A trait prototype carries the permission to generic code and dyn interfaces. */
struct Label { i32 size; };

i32 Label::width(Label this) { return this.size * 2; }

trait Named {
    @chain Label label(const Self* this);
};

struct Item { i32 size; };

impl Named for Item {
    Label label(const Item* this) { return Label { .size = this->size }; }
};

trait Scaled {
    @chain Self scaled(Self this, i32 factor);
};

struct Meter { i32 amount; };

impl Scaled for Meter {
    Meter scaled(Meter this, i32 factor) {
        this.amount *= factor;
        return move this;
    }
};

@generic<T: Scaled>
T sixfold(T value) { return (move value).scaled(2).scaled(3); }

@generic<T: Named>
i32 generic_width(const T* value) { return value->label().width(); }

i32 dyn_width(const dyn(Named)* value) { return value->label().width(); }

i32 main() {
    i32 failures = 0;
    try {
        failures += build("/ab") == 733 ? 0 : 1;
    } catch (Invalid failure) { failures += 2; }
    catch (std.alloc::alloc_error failure) { failures += 4; }
    failures += drop_count(false) == 1 ? 0 : 8;
    try {
        failures += build("") == 0 ? 16 : 32;
    } catch (Invalid failure) { failures += failure.code == 1 ? 0 : 64; }
    catch (std.alloc::alloc_error failure) { failures += 128; }
    failures += drop_count(false) == 2 ? 0 : 256;

    Counter counter = {.total = 0};
    counter.add(1)->add(2)->add(3);
    failures += counter.add(4)->value() == 10 ? 0 : 512;

    Grid grid = {.cells = {1, 2, 3}};
    failures += grid.row()[1] == 2 ? 0 : 1024;
    failures += grid.first().0 == 1 ? 0 : 2048;
    failures += grid.shifted(10).shifted(1).cells[2] == 14 ? 0 : 4096;

    Item item = {.size = 5};
    const dyn(Named)* erased = &item;
    failures += item.label().width() == 10 ? 0 : 8192;
    failures += generic_width(&item) == 10 ? 0 : 16384;
    failures += dyn_width(erased) == 10 ? 0 : 32768;
    failures += sixfold(Meter {.amount = 1}).amount == 6 ? 0 : 65536;
    return failures;
}
