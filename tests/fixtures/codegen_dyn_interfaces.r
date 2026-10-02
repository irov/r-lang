module test.codegen.dyn_interfaces;

/* R-TYPE-0051: dyn interfaces over the closed set of converted types: throwing, default,
   overridden and supertrait methods, a generic implementation, an associated equality, an
   interface borrow stored in a field, converted in a generic body and weakened to shared. */

error Missing { u32 key; };

trait Measure { u32 size(const Self* this); };

trait Store : Measure {
    u32 get(const Self* this, u32 key) throws Missing;
    void put(Self* this, u32 key, u32 value);
    u32 twice(const Self* this, u32 key) throws Missing { return this->get(key) * 2u32; }
};

struct Memory { u32[4] slots; };
struct Constant { u32 value; };

impl Measure for Memory { u32 size(const Memory* this) { return len(this->slots) as u32; } };
impl Measure for Constant { u32 size(const Constant* this) { return 1u32; } };

impl Store for Memory {
    u32 get(const Memory* this, u32 key) throws Missing {
        if (key >= 4u32) { throw Missing {.key = key}; }
        return this->slots[key];
    }
    void put(Memory* this, u32 key, u32 value) { this->slots[key % 4u32] = value; }
};

impl Store for Constant {
    u32 get(const Constant* this, u32 key) throws Missing { return this->value + key; }
    void put(Constant* this, u32 key, u32 value) { this->value = value; }
    u32 twice(const Constant* this, u32 key) throws Missing { return 1000u32; }
};

@generic<T: copy>
struct Cell { T value; };
@generic<T: copy>
impl Measure for Cell<T> { u32 size(const Cell<T>* this) { return sizeof(T) as u32; } };
@generic<T: copy>
impl Store for Cell<T> {
    u32 get(const Cell<T>* this, u32 key) throws Missing { return key; }
    void put(Cell<T>* this, u32 key, u32 value) {}
};

trait Source { type Item; o<Self::Item> next(Self* this); };
struct Countdown { u32 left; };
impl Source for Countdown {
    type Item = u32;
    o<u32> next(Countdown* this) {
        if (this->left == 0u32) { return o::none; }
        this->left -= 1u32;
        return o::some(this->left);
    }
};

struct Service { const dyn(Store)* store; u32 base; };

u32 lookup(const Service* service, u32 key) throws Missing {
    return service->store->get(key) + service->base;
}

@generic<T: Store>
u32 through(const T* value) {
    const dyn(Store)* erased = value;
    return erased->size();
}

u32 drain(dyn(Source & Item = u32)* source) {
    u32 total = 0u32;
    o<u32> item = source->next();
    while (true) {
        switch (item) {
        case variant o::some(value): total += *value; break;
        case variant o::none: return total;
        }
        item = source->next();
    }
}

u32 fill(dyn(Store)* store) {
    store->put(1u32, 7u32);
    const dyn(Store)* view = store;
    return view->size();
}

i32 main() {
    Memory memory = {.slots = {1u32, 2u32, 3u32, 4u32}};
    Constant constant = {.value = 10u32};
    Cell<u64> cell = {.value = 5u64};
    Countdown countdown = {.left = 4u32};
    u32 filled = fill(&memory) + fill(&constant);
    bool fill_ok = filled == 5u32 && memory.slots[1] == 7u32 && constant.value == 7u32;
    Service first = {.store = &memory, .base = 100u32};
    Service second = {.store = &constant, .base = 0u32};
    Service third = {.store = &cell, .base = 1u32};
    bool generic_ok = through(&memory) == 4u32 && through(&cell) == 8u32;
    bool drain_ok = drain(&countdown) == 6u32;
    try {
        bool a_ok = lookup(&first, 1u32) == 107u32;
        bool b_ok = lookup(&second, 2u32) == 9u32;
        bool c_ok = lookup(&third, 3u32) == 4u32;
        const dyn(Store)* view = &constant;
        bool d_ok = view->twice(1u32) == 1000u32;
        const dyn(Store)* other = &memory;
        bool e_ok = other->twice(2u32) == 6u32;
        if (a_ok == false || b_ok == false || c_ok == false || d_ok == false || e_ok == false) {
            return 2;
        }
        u32 missing = other->get(9u32);
        missing as void;
        return 1;
    } catch (Missing failure) {
        if (failure.key != 9u32 || fill_ok == false || generic_ok == false || drain_ok == false) {
            return 3;
        }
    }
    return 0;
}
