module example.ledger.handoff;
import example.ledger.accounts::{LedgerError};

struct Slot { i64 value; u32 phase; };

bool deliver(const std.sync::mutex<Slot>* slot, const std.sync::condvar* changed, i64 message) {
    while (true) {
        std.sync::lock_result<Slot> acquired = slot->lock();
        switch (move acquired) {
        case variant std.sync::lock_result::locked(move guard):
            Slot* value = guard.get_mut();
            u32 phase = value->phase;
            if (phase == 0u32) { value->value = message; value->phase = 1u32; }
            if (phase == 2u32) { (move guard).unlock(); return true; }
            changed->notify_one();
            std.sync::lock_result<Slot> resumed = changed->wait(move guard);
            // Recheck the predicate under a fresh guard; the slot retains the notification state.
            drop resumed;
            break;
        case variant std.sync::lock_result::poisoned(move guard): return false;
        case variant std.sync::lock_result::would_deadlock: return false;
        }
    }
    return false;
}

i64 receive(const std.sync::mutex<Slot>* slot, const std.sync::condvar* changed) throws LedgerError {
    while (true) {
        std.sync::lock_result<Slot> acquired = slot->lock();
        switch (move acquired) {
        case variant std.sync::lock_result::locked(move guard):
            Slot* value = guard.get_mut();
            bool ready = value->phase == 1u32;
            i64 message = value->value;
            if (ready == true) { value->phase = 2u32; }
            if (ready == true) {
                changed->notify_all();
                (move guard).unlock();
                return message;
            }
            std.sync::lock_result<Slot> resumed = changed->wait(move guard);
            // Recheck the predicate under a fresh guard; the slot retains the notification state.
            drop resumed;
            break;
        case variant std.sync::lock_result::poisoned(move guard): throw LedgerError::LockFailed;
        case variant std.sync::lock_result::would_deadlock: throw LedgerError::LockFailed;
        }
    }
    throw LedgerError::LockFailed;
}

std.string::string run(i64 message) throws std.thread::thread_error, LedgerError, std.alloc::alloc_error {
    Slot empty = { .value = 0i64, .phase = 0u32 };
    std.sync::mutex<Slot> slot = std.sync::mutex_new(empty);
    std.sync::condvar changed = std.sync::condvar_new();
    thread_scope {
        std.thread::scoped_join_handle<bool> worker = std.thread::spawn_scoped(deliver, &slot, &changed, message);
        i64 received = receive(&slot, &changed);
        std.thread::join_result<bool> completed = (move worker).join();
        switch (move completed) {
        case variant std.thread::join_result::returned(move acknowledged):
            throw (acknowledged == false) LedgerError::WorkerFailed; break;
        case variant std.thread::join_result::panicked(move report): throw LedgerError::WorkerFailed;
        }
        return f"received={received} acknowledged=true\n";
    }
}
