module test.codegen.exclusive_results;

/* R-BORROW-0004, R-BORROW-0009, R-EXPR-0001: an exclusive borrow or a mutable slice as a
   result is an exclusive reborrow of the inputs it derives from. The result is used through a
   local, directly through a temporary borrow, as an argument, through a trait method and a
   generic function, and as a nullable result. A result dereferenced directly may come from a call
   whose arguments are computed by other calls. */

struct Counter { i32[4] slots; i32 total; };

i32* Counter::slot(Counter* this, usize index) { return &this->slots[index]; }
i32[] Counter::window(Counter* this, usize begin, usize end) { return this->slots[begin..end]; }
i32*? Counter::find(Counter* this, i32 value) {
    for (usize index = 0usize; index < 4usize; index += 1usize) {
        if (this->slots[index] == value) { return &this->slots[index]; }
    }
    return null;
}

trait Store {
    i32* cell(Self* this);
};
struct Single { i32 value; };
impl Store for Single {
    i32* cell(Single* this) { return &this->value; }
};
impl Store for Counter {
    i32* cell(Counter* this) { return &this->total; }
};
@generic<T: Store> void bump(T* store) {
    i32* target = store->cell();
    *target += 1;
}

@generic<T> T* first_mut(T[] items) { return &items[0]; }
i32[] tail(i32[] values) { return values[1usize..len(values)]; }
i32* pick(i32* left, i32* right, bool take_left) {
    if (take_left == true) { return left; }
    return right;
}
const i32* first(const i32[] values) { return &values[0]; }
void add(i32* target, i32 amount) { *target += amount; }

i32 synchronous() {
    Counter c = {.slots = {1, 2, 3, 4}, .total = 0};
    i32* second = c.slot(1usize);
    *second = 20;
    *c.slot(2usize) = 3;
    *c.slot(0usize) += 1;
    i32*? found = c.find(3);
    if (found != null) { *found = 30; }
    add(c.slot(3usize), 36);
    i32[] view = c.window(0usize, 2usize);
    view[1] += 1;
    if (*c.slot(0usize) != 2 || c.slots[1] != 21 || c.slots[2] != 30 || c.slots[3] != 40) {
        return 1;
    }
    Single single = {.value = 7};
    bump(&single);
    bump(&c);
    if (single.value != 8 || c.total != 1) { return 2; }
    i32[4] values = {1, 2, 3, 4};
    i32* head = first_mut::<i32>(values[0usize..4usize]);
    *head = 10;
    i32[] rest = tail(values[0usize..4usize]);
    rest[0] = 20;
    const i32[] shared = values[0usize..4usize];
    if (*first(shared) != 10 || values[1] != 20) { return 3; }
    i32 a = 1;
    i32 b = 2;
    i32* chosen = pick(&a, &b, false);
    *chosen = 5;
    if (a != 1 || b != 5) { return 4; }
    return 0;
}

i32 indexed(str text) {
    Counter c = {.slots = {1, 2, 3, 4}, .total = 0};
    *c.slot(len(text)) += 10;
    const i32 doubled = *c.slot(len(text)) * 2;
    *c.slot(len(text) + 1usize) = doubled;
    return c.slots[1] + c.slots[2];
}

async i32 later(i32 x) {
    Counter c = {.slots = {x, 2, 3, 4}, .total = 0};
    i32* s = c.slot(0usize);
    *s += 1;
    return c.slots[0];
}

async i32 main() {
    const i32 status = synchronous();
    if (status != 0) { return status; }
    if (await later(4) != 5) { return 5; }
    if (indexed("a") != 36) { return 6; }
    return 0;
}
