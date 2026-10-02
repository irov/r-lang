module test.codegen.async_dyn_owners;

/* R-TYPE-0055 (L29): owners of interfaces across suspension points, tasks of a group, a thread
   and a scoped async method; every member is destroyed exactly once. */

async void pause() {}

struct Counter { atomic u32 drops; };

trait Store : send & sync {
    u32 load(const Self* this, u32 key);
    @scoped
    async u32 fetch(const Self* this, u32 key) throws std.async::start_error;
};
trait Sized { u32 size(const Self* this); };

struct Memory { arc Counter counter; u32 base; };
drop(Memory* self) {
    const Counter* c = &*self->counter;
    core::atomic_fetch_add(&c->drops, 1u32, core::memory_order::relaxed) as void;
}
impl Store for Memory {
    u32 load(const Memory* this, u32 key) { return this->base + key; }
    @scoped
    async u32 fetch(const Memory* this, u32 key) throws std.async::start_error {
        await pause();
        return this->base + key;
    }
};
impl Sized for Memory { u32 size(const Memory* this) { return 1u32; } };

struct Remote { arc Counter counter; u32 factor; };
drop(Remote* self) {
    const Counter* c = &*self->counter;
    core::atomic_fetch_add(&c->drops, 1u32, core::memory_order::relaxed) as void;
}
impl Store for Remote {
    u32 load(const Remote* this, u32 key) { return this->factor * key; }
    @scoped
    async u32 fetch(const Remote* this, u32 key) throws std.async::start_error {
        return this->factor * key;
    }
};
impl Sized for Remote { u32 size(const Remote* this) { return 2u32; } };

u32 drops(const (arc Counter)* counter) {
    return core::atomic_load(&(*counter)->drops, core::memory_order::relaxed);
}

async u32 query(arc dyn(Store & send & sync) store, u32 key) throws std.async::start_error {
    await pause();
    return store->load(key);
}

/* The owner moves into the task; the member is destroyed when the task's frame ends. */
async u32 consume(own dyn(Store & send & sync)* store, u32 key) throws std.async::start_error {
    await pause();
    return store->load(key);
}

u32 work(arc dyn(Store & send & sync) store) { return store->load(5u32); }

async i32 run(arc Counter counter) throws std.async::start_error, std.thread::thread_error {
    u32 total = 0u32;
    {
        arc Memory memory = new arc Memory {.counter = std.arc::clone(&counter), .base = 10u32};
        arc dyn(Store & send & sync) shared = move memory;
        task_scope(2) group {
            auto a = query(std.arc::clone(&shared), 1u32);
            auto b = query(std.arc::clone(&shared), 2u32);
            u32 x = await move a;
            u32 y = await move b;
            total += x + y;
        }
        arc dyn(Store & send & sync) copy = std.arc::clone(&shared);
        std.thread::join_handle<u32> worker = std.thread::spawn(work, move copy);
        std.thread::join_result<u32> outcome = (move worker).join();
        switch (move outcome) {
        case variant std.thread::join_result::returned(move value): total += value;
        case variant std.thread::join_result::panicked(move report): return 100;
        }
        task_scope(1) fetching { total += await shared->fetch(4u32); }
        if (drops(&counter) != 0u32) { return 101; }
        total += await query(move shared, 5u32);
    }
    if (drops(&counter) != 1u32) { return 102; }
    {
        own Remote* remote = new Remote {.counter = std.arc::clone(&counter), .factor = 3u32};
        own dyn(Store & Sized & send & sync)* both = move remote;
        await pause();
        total += both->size();
        own dyn(Store & send & sync)* narrow = move both;
        task_scope(1) fetching { total += await narrow->fetch(2u32); }
        total += await consume(move narrow, 7u32);
        if (drops(&counter) != 2u32) { return 103; }
    }
    /* 11 + 12 + 15 + 14 + 15 + 2 + 6 + 21 */
    if (total != 96u32) { return total as i32; }
    return 0;
}

async i32 main() {
    arc Counter counter = new arc Counter {.drops = 0u32};
    try {
        i32 status = await run(std.arc::clone(&counter));
        if (status != 0) { return status; }
    } catch (std.async::start_error failure) {
        return 2;
    } catch (std.thread::thread_error failure) {
        return 3;
    }
    if (drops(&counter) != 2u32) { return 4; }
    return 0;
}
