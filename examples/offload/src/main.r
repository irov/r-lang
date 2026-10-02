module example.offload.main;
import std.console;
import example.offload.native;

/* The ticks of a task that runs on the executor beside the blocking calls. */
struct Ticker { atomic u32 ticks; atomic u32 done; };

std.time::duration millis(u32 count) throws std.error::fault {
    return std.time::duration_from_parts((count / 1000u32) as i64, (count % 1000u32) * 1000000u32);
}

// One tick per millisecond until the calls are done.
async void tick(arc Ticker ticker) throws std.error::fault {
    bool running = true;
    while (running == true) {
        {
            const Ticker* shared = &*ticker;
            if (core::atomic_load(&shared->done, core::memory_order::acquire) != 0u32) {
                running = false;
            }
            core::atomic_fetch_add(&shared->ticks, 1u32, core::memory_order::relaxed) as void;
        }
        await std.time::sleep_for(millis(1u32));
    }
}

/* The identifier of the task that runs this function (Library R-SLIB-ASYNC-0018). */
u64 current_task(u64 unused) {
    unused as void;
    return std.async::task_id();
}

std.string::string answer(bool value) throws std.error::fault {
    if (value == true) { return std.string::from_str("yes"); }
    return std.string::from_str("no");
}

/* Six calls of ms milliseconds start at once beside a ticker. Four pool threads run four calls,
   the other two wait in the queue for a free thread, and the ticker keeps running meanwhile. */
async i32 naps(u32 ms) throws std.error::fault {
    arc Ticker ticker = new arc Ticker {.ticks = 0u32, .done = 0u32};
    std.time::instant start = std.time::monotonic_now();
    u32 slept = 0u32;
    task_scope(8) group {
        arc Ticker watch = std.arc::clone(&ticker);
        auto ticking = tick(move watch);
        auto a = std.async::blocking(example.offload.native::nap, ms);
        auto b = std.async::blocking(example.offload.native::nap, ms);
        auto c = std.async::blocking(example.offload.native::nap, ms);
        auto d = std.async::blocking(example.offload.native::nap, ms);
        auto e = std.async::blocking(example.offload.native::nap, ms);
        auto f = std.async::blocking(example.offload.native::nap, ms);
        slept += (await move a) + (await move b) + (await move c);
        slept += (await move d) + (await move e) + (await move f);
        {
            const Ticker* shared = &*ticker;
            core::atomic_store(&shared->done, 1u32, core::memory_order::release);
        }
        await move ticking;
    }
    std.time::duration elapsed = std.time::instant_duration(std.time::monotonic_now(), start);
    const Ticker* shared = &*ticker;
    u32 ticks = core::atomic_load(&shared->ticks, core::memory_order::acquire);
    std.string::string ran = answer(ticks > 1u32);
    std.string::string rounds =
        answer(std.time::duration_compare(elapsed, millis(ms * 2u32)) >= 0);
    /* Each call on the pool is a task of its own, started after this one. */
    u64 here = std.async::task_id();
    u64 pooled = await std.async::blocking(current_task, 0u64);
    std.string::string separate = answer(pooled > here);
    await std.console::print(
        f"6 calls of {ms} ms slept {slept} ms\nthe ticker kept running: {ran}\ntwo rounds on four pool threads: {rounds}\na call runs as a task of its own: {separate}\n");
    return 0;
}

/* A timer wins the select against a call of ms milliseconds. Cancellation cannot interrupt a
   call that a pool thread has taken, so the group ends only after the call returns. */
async i32 cancel(u32 ms) throws std.error::fault {
    std.time::instant start = std.time::monotonic_now();
    bool timer_won = false;
    task_scope(3) group {
        auto call = std.async::blocking(example.offload.native::nap, ms);
        auto timer = std.time::sleep_for(millis(10u32));
        select (group) {
        case u32 slept = await move call: slept as void; break;
        case await move timer: timer_won = true; break;
        }
        group.cancel_all();
        await group.all();
    }
    std.time::duration elapsed = std.time::instant_duration(std.time::monotonic_now(), start);
    std.string::string won = answer(timer_won);
    std.string::string waited = answer(std.time::duration_compare(elapsed, millis(ms)) >= 0);
    await std.console::print(
        f"the timer won: {won}\nthe group waited for the call to return: {waited}\n");
    return 0;
}

enum Command { nap, cancel };

async i32 main() {
    i32 status = 0;
    try {
        array<std.string::string> arguments = std.env::arguments();
        o<Command> command = o::none;
        if (len(arguments) == 3usize) {
            command = core::enum_from_name::<Command>(arguments[1].as_str());
        }
        switch (command) {
        case variant o::some(chosen):
            u32 ms = std.convert::parse_u32(arguments[2].as_str(), 10u32);
            switch (*chosen) {
            case Command::nap: status += await naps(ms);
            case Command::cancel: status += await cancel(ms);
            }
        case variant o::none:
            // Help without arguments, a usage error otherwise.
            if (len(arguments) > 1usize) { status += 64; }
            await std.console::print(
                std.string::from_str("offload nap|cancel MILLISECONDS\n"));
        }
        drop arguments;
    } catch (std.error::fault failure) {
        auto name = std.error::name(std.error::from_fault(failure));
        std.string::string line = std.string::from_str("offload failed: ");
        line.append(name);
        line.append("\n");
        await std.console::print(move line);
        status += 70;
    }
    return status;
}
