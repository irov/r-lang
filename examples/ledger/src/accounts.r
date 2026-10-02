module example.ledger.accounts;

struct Balance { i64 amount; usize entries; };
struct Applied { bool success; usize count; };
error LedgerError { LockFailed, WorkerFailed };


bool blocking_post(const std.sync::mutex<Balance>* account, i32 delta) {
    std.sync::lock_result<Balance> acquired = account->lock();
    switch (move acquired) {
    case variant std.sync::lock_result::locked(move guard): {
                Balance* value = guard.get_mut();
                value->amount += delta as i64;
                value->entries += 1usize;
            }
            (move guard).unlock(); return true;
    case variant std.sync::lock_result::poisoned(move guard): return false;
    case variant std.sync::lock_result::would_deadlock: return false;
    }
}

Applied apply(const std.sync::mutex<Balance>* account, const std.sync::barrier* start, const i32[] entries) {
    // Start the two posting lanes together. No fallible work separates spawn from this wait.
    start->wait() as void;
    usize count = 0usize;
    for (const i32* delta in &entries) {
        std.sync::try_lock_result<Balance> immediate = account->try_lock();
        switch (move immediate) {
        case variant std.sync::try_lock_result::locked(move guard): {
                Balance* value = guard.get_mut();
                value->amount += *delta as i64;
                value->entries += 1usize;
            }
            (move guard).unlock(); break;
        case variant std.sync::try_lock_result::would_block:
            bool applied = blocking_post(account, *delta);
            if (applied == false) { return Applied { .success = false, .count = count }; }
            break;
        case variant std.sync::try_lock_result::poisoned(move guard): return Applied { .success = false, .count = count };
        case variant std.sync::try_lock_result::would_deadlock: return Applied { .success = false, .count = count };
        }
        count += 1usize;
    }
    return Applied { .success = true, .count = count };
}

Balance snapshot(const std.sync::mutex<Balance>* account) throws LedgerError {
    std.sync::lock_result<Balance> acquired = account->lock();
    switch (move acquired) {
    case variant std.sync::lock_result::locked(move guard):
        const Balance* value = guard.get();
        Balance result = *value;
        (move guard).unlock();
        return result;
    case variant std.sync::lock_result::poisoned(move guard): throw LedgerError::LockFailed;
    case variant std.sync::lock_result::would_deadlock: throw LedgerError::LockFailed;
    }
}

std.string::string batch(const i32[] entries) throws std.thread::thread_error, std.sync::barrier_error, std.alloc::alloc_error, LedgerError {
    Balance initial = { .amount = 0i64, .entries = 0usize };
    std.sync::mutex<Balance> account = std.sync::mutex_new(initial);
    std.sync::barrier start = std.sync::barrier_new(2usize);
    usize count = len(entries);
    usize middle = count / 2usize;
    usize posted = 0usize;
    thread_scope {
        std.thread::scoped_join_handle<Applied> worker = std.thread::spawn_scoped(apply, &account, &start, entries[0usize..middle]);
        Applied local = apply(&account, &start, entries[middle..count]);
        std.thread::join_result<Applied> completed = (move worker).join();
        switch (move completed) {
        case variant std.thread::join_result::returned(move result):
            throw (result.success == false || local.success == false) LedgerError::LockFailed;
            posted = local.count + result.count; break;
        case variant std.thread::join_result::panicked(move report): throw LedgerError::WorkerFailed;
        }
    }
    Balance final = snapshot(&account);
    throw (final.entries != posted) LedgerError::LockFailed;
    return f"balance={final.amount} entries={final.entries}\n";
}
