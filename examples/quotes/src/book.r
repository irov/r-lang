module example.quotes.book;

struct Quote { u64 minor; usize revision; };
error BookError { LockFailed };


u64 write(const std.sync::rw_lock<Quote>* prices, u64 minor) throws BookError {
    std.sync::write_lock_result<Quote> acquired = prices->write();
    switch (move acquired) {
    case variant std.sync::write_lock_result::locked(move guard):
        const Quote* before = guard.get();
        u64 value = before->minor;
        {
            Quote* updated = guard.get_mut();
            updated->minor = minor;
            updated->revision += 1usize;
        }
        (move guard).unlock();
        return value;
    case variant std.sync::write_lock_result::poisoned(move guard): throw BookError::LockFailed;
    case variant std.sync::write_lock_result::would_deadlock: throw BookError::LockFailed;
    }
}

u64 try_write(const std.sync::rw_lock<Quote>* prices, u64 minor) throws BookError {
    std.sync::try_write_lock_result<Quote> acquired = prices->try_write();
    switch (move acquired) {
    case variant std.sync::try_write_lock_result::locked(move guard):
        const Quote* before = guard.get();
        u64 value = before->minor;
        {
            Quote* updated = guard.get_mut();
            updated->minor = minor;
            updated->revision += 1usize;
        }
        (move guard).unlock();
        return value;
    case variant std.sync::try_write_lock_result::poisoned(move guard): throw BookError::LockFailed;
    case variant std.sync::try_write_lock_result::would_deadlock: throw BookError::LockFailed;
    case variant std.sync::try_write_lock_result::would_block: u64 waited = write(prices, minor); return waited;
    }
}

Quote read(const std.sync::rw_lock<Quote>* prices) throws BookError {
    std.sync::read_lock_result<Quote> acquired = prices->read();
    switch (move acquired) {
    case variant std.sync::read_lock_result::locked(move guard):
        const Quote* observed = guard.get(); observed as void;
        Quote value = *observed;
        (move guard).unlock();
        return value;
    case variant std.sync::read_lock_result::poisoned(move guard): throw BookError::LockFailed;
    case variant std.sync::read_lock_result::would_deadlock: throw BookError::LockFailed;
    }
}

Quote try_read(const std.sync::rw_lock<Quote>* prices) throws BookError {
    std.sync::try_read_lock_result<Quote> acquired = prices->try_read();
    switch (move acquired) {
    case variant std.sync::try_read_lock_result::locked(move guard):
        const Quote* observed = guard.get(); observed as void;
        Quote value = *observed;
        (move guard).unlock();
        return value;
    case variant std.sync::try_read_lock_result::poisoned(move guard): throw BookError::LockFailed;
    case variant std.sync::try_read_lock_result::would_deadlock: throw BookError::LockFailed;
    case variant std.sync::try_read_lock_result::would_block: Quote waited = read(prices); return waited;
    }
}

struct Publication { au32 done; };

bool publish(const u64[] values, const std.sync::rw_lock<Quote>* prices, const Publication* completion) {
    bool success = true;
    try {
        for (const u64* value in &values) {
            try_write(prices, *value) as void;
        }
    } catch (BookError failure) { success = false; }
    core::atomic_store(&completion->done, 1u32, core::memory_order::release);
    return success;
}

std.string::string watch(const u64[] values) throws BookError, std.thread::thread_error, std.alloc::alloc_error {
    Quote initial = { .minor = 0u64, .revision = 0usize };
    std.sync::rw_lock<Quote> prices = std.sync::rwlock_new(initial);
    Publication completion = { .done = 0u32 };
    usize snapshots = 0usize;
    thread_scope {
        std.thread::scoped_join_handle<bool> producer = std.thread::spawn_scoped(publish, values, &prices, &completion);
        while (core::atomic_load(&completion.done, core::memory_order::acquire) == 0u32) {
            Quote observed = try_read(&prices); observed as void;
            snapshots += 1usize;
            std.thread::yield_now();
        }
        std.thread::join_result<bool> result = (move producer).join();
        switch (move result) {
        case variant std.thread::join_result::returned(move success):
            throw (success == false) BookError::LockFailed; break;
        case variant std.thread::join_result::panicked(move report): throw BookError::LockFailed;
        }
    }
    Quote final = read(&prices);
    return f"revision={final.revision} gross={final.minor} snapshots={snapshots}\n";
}
