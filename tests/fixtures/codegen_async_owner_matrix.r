module test.codegen.async_owner_matrix;

// Owners transferred into tasks and threads (Core R-FUNC-0010, R-FUNC-0012, R-AM-0014).
// tests/codegen_async_owner_matrix_wrapper.c refuses the frame allocation of the starts marked
// "refused" below (the 3rd, 6th, 9th, 12th and 16th task start of the program, the hosted root
// being the 1st), refuses every allocation attempt of std.thread::spawn in turn until a spawn
// succeeds, and opens `ready` in parameter_resume once wait_for has suspended on the gated task.
// Each scenario counts the destruction of its elements on a counter of its own and returns zero
// or the number of the check that failed.

struct Counter { atomic u32 drops; };
struct Tracked { arc Counter counter; own i32* value; };
drop(Tracked* self) {
    const Counter* counter = &*self->counter;
    core::atomic_fetch_add(&counter->drops, 1u32, core::memory_order::relaxed) as void;
}

@generic<T: send & unborrowed>
async T relay(T value) { return move value; }

@generic<T>
error Rejected { T payload; };

@generic<T: send & unborrowed>
async void reject(T value) throws Rejected<T> {
    try { throw Rejected<T> { .payload = move value }; }
    catch (Rejected<T> failure) { throw; }
}

async i32 number() { return 42; }
async i32 wait_for(task<i32> operation) {
    i32 value = await move operation;
    return value;
}
async i32 gated(std.async::notify gate) {
    try { await gate.notified(); } catch (std.async::start_error failure) { return 0; }
    return 42;
}

i32 list_worker(list<Tracked> items) {
    usize count = len(items);
    i32 value = count as i32;
    return value;
}

u32 drops_of(const Counter* counter) {
    return core::atomic_load(&counter->drops, core::memory_order::relaxed);
}

Tracked track(arc Counter counter, i32 value) {
    return Tracked { .counter = move counter, .value = new i32(value) };
}

// A list named by a refused start stays with the caller; the next start moves it into the task,
// which returns the same elements or, cancelled, destroys them once.
async i32 list_start(bool cancel) throws std.alloc::alloc_error, std.list::push_error<Tracked> {
    arc Counter counter = new arc Counter { .drops = 0u32 };
    list<Tracked> items = std.list::create::<Tracked>();
    std.list::push_back(&items, track(std.arc::clone(&counter), 7)) as void;
    i32 status = 0;
    try {
        list<Tracked> returned = await relay(move items); // refused
        drop returned;
        return 1;
    } catch (std.async::start_error failure) {
        if (failure != std.async::start_error::allocation_failed) { status = 2; }
    }
    if (len(items) != 1usize) { status = 3; }
    if (drops_of(&*counter) != 0u32) { status = 4; }
    try {
        if (cancel == true) {
            task_scope(1) group {
                task<list<Tracked>> running = relay(move items);
                std.async::cancel(move running);
                await group.all();
            }
        } else {
            list<Tracked> returned = await relay(move items);
            if (len(returned) != 1usize) { status = 5; }
            if (drops_of(&*counter) != 0u32) { status = 6; }
            drop returned;
        }
    } catch (std.async::start_error failure) { status = 7; }
    if (drops_of(&*counter) != 1u32) { status = 8; }
    if (std.arc::strong_count(&counter) != 1usize) { status = 9; }
    return status;
}

// The same for a dict that the task hands back inside a checked error.
async i32 dict_start(bool cancel)
    throws std.alloc::alloc_error, std.dict::insert_error<i32, Tracked> {
    arc Counter counter = new arc Counter { .drops = 0u32 };
    dict<i32, Tracked> table = std.dict::create::<i32, Tracked>();
    o<Tracked> previous = std.dict::insert(&table, 11, track(std.arc::clone(&counter), 9));
    (move previous) as void;
    i32 status = 0;
    try {
        await reject(move table); // refused
        return 1;
    } catch (std.async::start_error failure) {
        if (failure != std.async::start_error::allocation_failed) { status = 2; }
    } catch (Rejected<dict<i32, Tracked>> failure) {
        return 3;
    }
    if (len(table) != 1usize) { status = 4; }
    if (drops_of(&*counter) != 0u32) { status = 5; }
    try {
        if (cancel == true) {
            task_scope(1) group {
                task<void throws Rejected<dict<i32, Tracked>>> running = reject(move table);
                std.async::cancel(move running);
                await group.all();
            }
        } else {
            try {
                await reject(move table);
                status = 6;
            } catch (Rejected<dict<i32, Tracked>> failure) {
                if (len(failure.payload) != 1usize) { status = 7; }
                if (drops_of(&*counter) != 0u32) { status = 8; }
            }
        }
    } catch (std.async::start_error failure) { status = 9; }
    if (drops_of(&*counter) != 1u32) { status = 10; }
    if (std.arc::strong_count(&counter) != 1usize) { status = 11; }
    return status;
}

// A task named by a refused start stays with the caller, who starts and awaits it again.
async i32 task_start() throws std.async::start_error {
    task<i32> child = number();
    i32 status = 0;
    try {
        i32 first = await wait_for(move child); // refused
        first as void;
        return 1;
    } catch (std.async::start_error failure) {
        if (failure != std.async::start_error::allocation_failed) { status = 2; }
    }
    i32 value = await wait_for(move child);
    if (value != 42) { status = 3; }
    return status;
}

// A list named by a refused std.thread::spawn stays with the caller after every refused
// allocation of the spawn; the spawn that succeeds moves it to the thread, which destroys it.
i32 thread_start() throws std.alloc::alloc_error, std.list::push_error<Tracked> {
    arc Counter counter = new arc Counter { .drops = 0u32 };
    list<Tracked> items = std.list::create::<Tracked>();
    std.list::push_back(&items, track(std.arc::clone(&counter), 5)) as void;
    i32 status = 0;
    u32 refusals = 0u32;
    while (true) {
        try {
            std.thread::join_handle<i32> worker = std.thread::spawn(list_worker, move items);
            std.thread::join_result<i32> joined = std.thread::join(move worker);
            switch (move joined) {
                case variant std.thread::join_result::returned(move value):
                    if (value != 1) { status = 1; }
                    break;
                case variant std.thread::join_result::panicked(move report): status = 2;
            }
            break;
        } catch (std.thread::thread_error failure) {
            if (failure != std.thread::thread_error::resource_exhausted) { status = 3; }
            if (len(items) != 1usize) { status = 4; }
            if (drops_of(&*counter) != 0u32) { status = 5; }
            refusals += 1u32;
        }
    }
    if (refusals == 0u32) { status = 6; }
    if (drops_of(&*counter) != 1u32) { status = 7; }
    if (std.arc::strong_count(&counter) != 1usize) { status = 8; }
    return status;
}

// A task parameter held by wait_for across a suspension: the gate keeps the awaited task
// pending, so wait_for always suspends; it then completes after the gate opens, or is cancelled
// while it waits and consumes the task it awaited.
async i32 parameter_resume(bool cancel) throws std.alloc::alloc_error, std.async::start_error {
    std.async::notify gate = std.async::notify_new();
    std.async::notify ready = std.async::notify_new();
    task<i32> pending = gated(gate.clone());
    task<i32> running = wait_for(move pending);
    await ready.notified();
    if (cancel == true) {
        std.async::cancel(move running);
        return 0;
    }
    gate.notify_one();
    i32 value = await move running;
    if (value != 42) { return 1; }
    return 0;
}

// Containers, weak owners and tasks relayed through tasks and a thread.
async i32 program() {
    arc Counter counter = new arc Counter { .drops = 0u32 };
    try {
        list<Tracked> items = std.list::create::<Tracked>();
        {
            Tracked item = { .counter = std.arc::clone(&counter), .value = new i32(7) };
            Tracked* inserted = std.list::push_back(&items, move item);
            const Tracked* observed = inserted; observed as void;
        }
        list<Tracked> returned = await relay(move items);
        if (len(returned) != 1usize) { return 1; }
        std.thread::join_handle<i32> worker = std.thread::spawn(list_worker, move returned);
        std.thread::join_result<i32> joined = std.thread::join(move worker);
        switch (move joined) {
            case variant std.thread::join_result::returned(move value):
                if (value != 1) { return 2; }
                break;
            case variant std.thread::join_result::panicked(move report): return 3;
        }
        if (core::atomic_load(&counter->drops, core::memory_order::relaxed) != 1u32) { return 4; }

        dict<i32, Tracked> table = std.dict::create::<i32, Tracked>();
        Tracked item = { .counter = std.arc::clone(&counter), .value = new i32(9) };
        o<Tracked> previous = std.dict::insert(&table, 11, move item);
        (move previous) as void;
        dict<i32, Tracked> forwarded = await relay(move table);
        try {
            await reject(move forwarded);
            return 5;
        } catch (Rejected<dict<i32, Tracked>> failure) {
            if (len(failure.payload) != 1usize) { return 6; }
        } finally {
            if (core::atomic_load(&counter->drops, core::memory_order::relaxed) != 2u32) {
                panic("payload was not destroyed before finally");
            }
        }

        weak arc Counter observer = std.arc::downgrade(&counter);
        weak arc Counter weak_returned = await relay(move observer);
        drop weak_returned;
        task<i32> operation = number();
        i32 value = await wait_for(move operation);
        if (value != 42) { return 7; }
    } catch (std.async::start_error failure) { return 10; }
      catch (std.thread::thread_error failure) { return 11; }
      catch (std.alloc::alloc_error failure) { return 12; }
      catch (std.list::push_error<Tracked> failure) { return 13; }
      catch (std.dict::insert_error<i32, Tracked> failure) { return 14; }
    i32 selected = core::atomic_load(&counter->drops, core::memory_order::relaxed) == 2u32 ? 0 : 8;
    return selected;
}

// A nonzero status is the base of the failing scenario plus the number of its failed check, kept
// below the statuses the target reserves for the main error boundary.
async i32 main() {
    try {
        i32 listed = await list_start(false);
        if (listed != 0) { return 10 + listed; }
        i32 listed_cancelled = await list_start(true);
        if (listed_cancelled != 0) { return 20 + listed_cancelled; }
        i32 rejected = await dict_start(false);
        if (rejected != 0) { return 30 + rejected; }
        i32 rejected_cancelled = await dict_start(true);
        if (rejected_cancelled != 0) { return 45 + rejected_cancelled; }
        i32 restarted = await task_start();
        if (restarted != 0) { return 60 + restarted; }
        i32 spawned = thread_start();
        if (spawned != 0) { return 65 + spawned; }
        i32 resumed = await parameter_resume(false);
        if (resumed != 0) { return 75 + resumed; }
        i32 resumed_cancelled = await parameter_resume(true);
        if (resumed_cancelled != 0) { return 80 + resumed_cancelled; }
        i32 relayed = await program();
        if (relayed != 0) { return 85 + relayed; }
    } catch (std.async::start_error failure) { return 100; }
      catch (std.alloc::alloc_error failure) { return 101; }
      catch (std.list::push_error<Tracked> failure) { return 102; }
      catch (std.dict::insert_error<i32, Tracked> failure) { return 103; }
    return 0;
}
