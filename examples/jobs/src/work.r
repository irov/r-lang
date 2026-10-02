module example.jobs.work;

/* A typed job key: `@derive(key)` defines the hash and equal hooks from the field. */
@generic<T: key & copy>
@derive(key)
struct JobKey { T value; };

std.string::string dispatch(const u32[] ids) throws std.alloc::alloc_error,
    std.dict::insert_error<JobKey<u32>, u32> {
    dict<JobKey<u32>, u32> accepted = std.dict::create::<JobKey<u32>, u32>();
    std.string::string output = std.string::create();
    for (u32 id in &ids) {
        JobKey<u32> key = JobKey<u32> { .value = id };
        bool duplicate = accepted.contains(&key);
        if (duplicate == true) {
            std.string::string row = f"duplicate={id}\n";
            output.append(row.as_str());
        } else {
            u64 hash = core::hash(&key);
            u32 shard = (hash % 4u64) as u32;
            o<u32> previous = accepted.insert(key, shard); previous as void;
            std.string::string row = f"accepted={id} shard={shard}\n";
            output.append(row.as_str());
        }
    }
    usize count = len(accepted);
    std.string::string summary = f"jobs={count}\n";
    output.append(summary.as_str());
    return move output;
}

struct Audit { au32 started; au32 released; au32 finalized; au32 completed; au32 failed; };
@generic<T: send & unborrowed>
struct Lease { T value; arc Audit audit; };
@generic<T: send & unborrowed>
drop(Lease<T>* self) {
    const Audit* audit = &*self->audit;
    u32 previous = core::atomic_fetch_add(&audit->released, 1u32, core::memory_order::release); previous as void;
}

async void process(Lease<bytes> payload, arc Audit audit, u32 milliseconds) {
    core::atomic_store(&audit->started, 1u32, core::memory_order::release);
    try {
        i64 seconds = (milliseconds / 1000u32) as i64;
        u32 nanos = (milliseconds % 1000u32) * 1000000u32;
        std.time::duration delay = std.time::duration_from_parts(seconds, nanos);
        await delay.sleep_for();
        const u8[] source = std.array::as_slice(&payload.value);
        u32 checksum = std.hash::crc32(source);
        core::atomic_store(&audit->completed, checksum, core::memory_order::release);
    } catch (std.time::duration_error failure) {
        core::atomic_store(&audit->failed, 1u32, core::memory_order::release);
    } catch (std.time::time_error failure) {
        core::atomic_store(&audit->failed, 1u32, core::memory_order::release);
    } catch (std.async::start_error failure) {
        core::atomic_store(&audit->failed, 1u32, core::memory_order::release);
    } finally {
        u32 previous = core::atomic_fetch_add(&audit->finalized, 1u32, core::memory_order::release); previous as void;
    }
}

// This polling adapter yields to the scheduler; it never blocks a worker with a busy wait.
async void pause() throws std.time::time_error, std.time::duration_error, std.async::start_error {
    std.time::duration delay = std.time::duration_from_parts(0i64, 1000000u32);
    await delay.sleep_for();
}

async std.string::string supervise(bytes data, u32 milliseconds, bool cancel, bool detach)
    throws std.alloc::alloc_error, std.async::start_error, std.time::time_error, std.time::duration_error {
    arc Audit audit = new arc Audit { .started = 0u32, .released = 0u32, .finalized = 0u32,
        .completed = 0u32, .failed = 0u32 };
    arc Audit lease_audit = audit.clone();
    arc Audit child_audit = audit.clone();
    Lease<bytes> payload = Lease<bytes> { .value = move data, .audit = move lease_audit };
    task<void> operation = process(move payload, move child_audit, milliseconds);
    while (core::atomic_load(&audit->started, core::memory_order::acquire) == 0u32) { await pause(); }
    if (cancel == true) { (move operation).cancel(); }
    else {
        if (detach == true) { (move operation).detach(); }
        else { await move operation; }
    }
    while (core::atomic_load(&audit->released, core::memory_order::acquire) == 0u32) { await pause(); }
    u32 released = core::atomic_load(&audit->released, core::memory_order::acquire);
    u32 finalized = core::atomic_load(&audit->finalized, core::memory_order::acquire);
    u32 checksum = core::atomic_load(&audit->completed, core::memory_order::acquire);
    u32 failed = core::atomic_load(&audit->failed, core::memory_order::acquire);
    return f"released={released} finalized={finalized} crc32={checksum} failed={failed}\n";
}
