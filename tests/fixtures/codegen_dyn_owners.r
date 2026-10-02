module test.codegen.dyn_owners;

/* R-TYPE-0055 (L29): owners of dyn interfaces in locals, fields, containers, options and
   generic code; narrowing; the shared-owner operations; each member is destroyed once. The
   last checks cover defects L29-1 (a switch over a temporary option drops it) and L29-2 (glue
   of a class of owners represented by a type that is only moved). */

struct Counter { atomic u32 drops; };

trait Handler : sync { i32 handle(const Self* this, i32 value); void tune(Self* this, i32 bonus); };
trait Named { str name(const Self* this); };

struct Doubler { arc Counter counter; i32 bonus; };
drop(Doubler* self) {
    const Counter* c = &*self->counter;
    core::atomic_fetch_add(&c->drops, 1u32, core::memory_order::relaxed) as void;
}
impl Handler for Doubler {
    i32 handle(const Doubler* this, i32 value) { return value * 2 + this->bonus; }
    void tune(Doubler* this, i32 bonus) { this->bonus = bonus; }
};
impl Named for Doubler { str name(const Doubler* this) { return "double"; } };

struct Negator { arc Counter counter; };
drop(Negator* self) {
    const Counter* c = &*self->counter;
    core::atomic_fetch_add(&c->drops, 1u32, core::memory_order::relaxed) as void;
}
impl Handler for Negator {
    i32 handle(const Negator* this, i32 value) { return 0 - value; }
    void tune(Negator* this, i32 bonus) { }
};
impl Named for Negator { str name(const Negator* this) { return "negate"; } };

struct Route { str path; own dyn(Handler)* handler; };

@generic<T>
usize count_items(const (array<T>)* items) { return len(*items); }

u32 drops(const (arc Counter)* counter) {
    return core::atomic_load(&(*counter)->drops, core::memory_order::relaxed);
}

i32 apply(const dyn(Handler)* handler, i32 value) { return handler->handle(value); }

i32 run(arc Counter counter)
    throws std.array::push_error<own dyn(Handler)*>,
    std.dict::insert_error<str, arc dyn(Handler & send)> {
    i32 total = 0;
    {
        own Doubler* first = new Doubler {.counter = std.arc::clone(&counter), .bonus = 0};
        own Negator* second = new Negator {.counter = std.arc::clone(&counter)};
        own dyn(Handler & Named)* named = move first;
        if (std.bytes::equal(named->name(), "double") == false) { return 100; }
        named->tune(1);
        total += apply(&named, 1);
        own dyn(Handler)* wide = move named;
        array<own dyn(Handler)*> chain = std.array::create::<own dyn(Handler)*>();
        std.array::push(&chain, move wide);
        std.array::push(&chain, move second);
        if (count_items(&chain) != 2usize) { return 101; }
        for (usize index = 0usize; index < len(chain); index += 1usize) {
            total += chain[index]->handle(10);
        }
    }
    if (drops(&counter) != 2u32) { return 102; }
    {
        own Negator* third = new Negator {.counter = std.arc::clone(&counter)};
        Route route = Route {.path = "/neg", .handler = move third};
        total += route.handler->handle(5);
        o<own dyn(Handler)*> maybe = o::none;
        switch (maybe) {
        case variant o::some(h): total += (*h)->handle(1);
        case variant o::none: total += 1000;
        }
    }
    if (drops(&counter) != 3u32) { return 103; }
    {
        arc Doubler shared = new arc Doubler {.counter = std.arc::clone(&counter), .bonus = 2};
        arc dyn(Handler & send) any = move shared;
        arc dyn(Handler & send) again = std.arc::clone(&any);
        if (std.arc::ptr_eq(&any, &again) == false) { return 104; }
        if (std.arc::strong_count(&any) != 2usize) { return 104; }
        dict<str, arc dyn(Handler & send)> named = std.dict::create::<str, arc dyn(Handler & send)>();
        std.dict::insert(&named, "double", move again) as void;
        str key = "double";
        switch (std.dict::get(&named, &key)) {
        case variant o::some(found): total += (**found)->handle(3);
        case variant o::none: return 105;
        }
        weak arc dyn(Handler & send) observer = std.arc::downgrade(&any);
        switch (std.arc::upgrade(&observer)) {
        case variant o::some(strong): total += (*strong)->handle(4);
        case variant o::none: return 106;
        }
        /* L29-1: the upgraded owner of the switch is dropped with it. */
        if (std.arc::strong_count(&any) != 2usize) { return 107; }
        rc Negator local = new rc Negator {.counter = std.arc::clone(&counter)};
        rc dyn(Handler) solo = move local;
        total += solo->handle(6);
    }
    if (drops(&counter) != 5u32) { return 109; }
    {
        rc Negator plain = new rc Negator {.counter = std.arc::clone(&counter)};
        weak rc Negator observer = std.rc::downgrade(&plain);
        weak rc dyn(Handler) seen = move observer;
        switch (std.rc::upgrade(&seen)) {
        case variant o::some(strong): total += (*strong)->handle(2);
        case variant o::none: return 110;
        }
    }
    if (drops(&counter) != 6u32) { return 111; }
    return total;
}

i32 main() {
    arc Counter counter = new arc Counter {.drops = 0u32};
    try {
        i32 total = run(std.arc::clone(&counter));
        /* 3 + (21 - 10) + (-5 + 1000) + 8 + 10 - 6 - 2 */
        if (total != 1019) { return total; }
    } catch (std.array::push_error<own dyn(Handler)*> failure) {
        return 2;
    } catch (std.dict::insert_error<str, arc dyn(Handler & send)> failure) {
        return 3;
    }
    return 0;
}
