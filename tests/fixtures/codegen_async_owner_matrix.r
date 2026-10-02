module test.codegen.async_owner_matrix;

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

i32 list_worker(list<Tracked> items) {
    usize count = len(items);
    i32 value = count as i32;
    return value;
}

async i32 main() {
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
    } catch (std.async::start_error failure) { return 90; }
      catch (std.thread::thread_error failure) { return 91; }
      catch (std.alloc::alloc_error failure) { return 92; }
      catch (std.list::push_error<Tracked> failure) { return 93; }
      catch (std.dict::insert_error<i32, Tracked> failure) { return 94; }
    i32 selected = core::atomic_load(&counter->drops, core::memory_order::relaxed) == 2u32 ? 0 : 8;
    return selected;
}
