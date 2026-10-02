module test.codegen.group_vacancy;

/* R-STMT-0018 (L25.3): `await group.vacancy()` waits until the group has a free slot, so a loop
   starts detached members as fast as earlier ones finish and never more than the capacity at
   once; `group.vacancy_until(deadline)` bounds that wait and reports whether a slot is free. */

struct Counter { atomic u32 runs; atomic u32 active; atomic u32 crowded; };

async void work(arc Counter counter, u32 pause)
    throws std.time::time_error, std.time::duration_error, std.async::start_error {
    {
        const Counter* shared = &*counter;
        u32 before = core::atomic_fetch_add(&shared->active, 1u32, core::memory_order::relaxed);
        if (before >= 2u32) {
            core::atomic_fetch_add(&shared->crowded, 1u32, core::memory_order::relaxed) as void;
        }
    }
    await std.time::sleep_for(std.time::duration_from_parts(0i64, pause));
    const Counter* shared = &*counter;
    core::atomic_fetch_sub(&shared->active, 1u32, core::memory_order::relaxed) as void;
    core::atomic_fetch_add(&shared->runs, 1u32, core::memory_order::relaxed) as void;
}

std.time::instant after(i64 milliseconds) throws std.time::time_error, std.time::duration_error {
    std.time::instant now = std.time::monotonic_now();
    return now.add(std.time::duration_from_parts(0i64, (milliseconds * 1000000i64) as u32));
}

async i32 main() {
    arc Counter counter = new arc Counter {.runs = 0u32, .active = 0u32, .crowded = 0u32};
    i32 status = 0;
    try {
        // At most two members run at once; each new one waits for a free slot.
        task_scope(2) group {
            for (i32 index = 0; index < 6; index += 1) {
                await group.vacancy();
                auto member = work(std.arc::clone(&counter), 20000000u32);
                std.async::detach(move member);
            }
            await group.all();
        }
        // A full group has no vacancy before a near deadline, and one once its member ends.
        task_scope(1) group {
            auto member = work(std.arc::clone(&counter), 300000000u32);
            std.async::detach(move member);
            bool early = await group.vacancy_until(after(20i64));
            if (early == true) { status += 4; }
            bool freed = await group.vacancy_until(after(900i64));
            if (freed == false) { status += 8; }
        }
        // An empty group has a vacancy at once.
        task_scope(1) group {
            await group.vacancy();
            bool empty = await group.vacancy_until(after(0i64));
            if (empty == false) { status += 16; }
        }
    } catch (std.time::time_error failure) {
        status += 32;
    } catch (std.time::duration_error failure) {
        status += 32;
    } catch (std.async::start_error failure) {
        status += 64;
    }
    const Counter* shared = &*counter;
    u32 runs = core::atomic_load(&shared->runs, core::memory_order::relaxed);
    u32 crowded = core::atomic_load(&shared->crowded, core::memory_order::relaxed);
    if (runs != 7u32) { status += 1; }
    if (crowded != 0u32) { status += 2; }
    return status;
}
