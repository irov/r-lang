module test.codegen.library_signal;

// R-SLIB-SIGNAL-0001..0003 (M22): signals delivered as task completions. A delivery counted
// before the wait completes it at once; two listeners of one kind both count; a wait cancelled
// in a select leaves the deliveries for the next wait; the deadline ends a wait with timed_out;
// a spawned child starts with the default disposition of a watched signal.

error TestFailed { i32 code; };

protected std.process::child take_child(std.process::spawn_result result)
    throws std.process::process_error {
    switch (move result) {
    case variant std.process::spawn_result::spawned(move child): return move child;
    case variant std.process::spawn_result::failed(move failure): throw failure.error;
    }
}

protected std.process::exit_status take_status(std.process::wait_result result)
    throws std.process::process_error {
    switch (move result) {
    case variant std.process::wait_result::exited(move status): return status;
    case variant std.process::wait_result::failed(move failure): throw failure.error;
    }
}

/* Counted before the wait, and counted by every listener of the kind. */
protected async i32 counting() throws std.process::process_error, std.async::start_error {
    std.signal::listener first = std.signal::kind::user1.listen();
    std.signal::listener second = std.signal::listen(std.signal::kind::user1);
    std.signal::raise(std.signal::kind::user1);
    std.signal::kind::user1.raise();
    u64 seen_first = await first.next();
    u64 seen_second = await std.signal::next(&second, o::none);
    i32 status = 0;
    if (seen_first < 1u64 || seen_first > 2u64) { status = 1; }
    if (seen_second < 1u64 || seen_second > 2u64) { status = 2; }
    return status;
}

/* A wait that loses a select is cancelled; the next delivery goes to the next wait. The
   deadline ends a wait with timed_out. */
protected async i32 racing()
    throws std.process::process_error, std.async::start_error, std.time::time_error,
        std.time::duration_error {
    std.signal::listener hangups = std.signal::listen(std.signal::kind::hangup);
    std.time::instant soon =
        std.time::monotonic_now().add(std.time::duration_from_parts(0i64, 20000000u32));
    i32 status = 0;
    task_scope(1) group {
        auto pending = std.signal::next(&hangups, o::none);
        select (group) {
        case u64 count = await move pending: count as void; status = 11; break;
        case until (soon): break;
        }
        group.cancel_all();
        await group.all();
    }
    if (status != 0) { return status; }
    std.signal::raise(std.signal::kind::hangup);
    u64 later = await hangups.next();
    if (later != 1u64) { return 12; }
    std.time::instant brief =
        std.time::monotonic_now().add(std.time::duration_from_parts(0i64, 10000000u32));
    try {
        u64 unexpected = await std.signal::next(&hangups, o::some(brief));
        unexpected as void;
        return 13;
    } catch (std.process::process_error failure) {
        if (failure.code != std.process::error_code::timed_out) { return 14; }
    }
    return 0;
}

/* A child that sends itself the watched signal ends by it: its disposition is the default. */
protected async i32 inheriting()
    throws std.process::process_error, std.async::start_error, std.fs::path_error {
    std.signal::listener watched = std.signal::listen(std.signal::kind::user2);
    std.fs::path shell = std.fs::path_from_utf8("/bin/sh");
    std.process::command command = std.process::command_create(&shell);
    std.process::arg(&command, "-c");
    std.process::arg(&command, "kill -USR2 $$; exit 0");
    std.process::spawn_result started = await std.process::spawn(move command, o::none);
    std.process::child child = take_child(move started);
    std.process::wait_result finished = await std.process::wait(move child, o::none);
    std.process::exit_status status = take_status(move finished);
    (move watched) as void;
    if (status.kind != std.process::termination_kind::signalled) { return 21; }
    return 0;
}

async i32 main() {
    try {
        i32 counted = await counting();
        if (counted != 0) { return counted; }
        i32 raced = await racing();
        if (raced != 0) { return raced; }
        return await inheriting();
    } catch (std.process::process_error failure) {
        failure as void;
        return 90;
    } catch (std.async::start_error failure) {
        failure as void;
        return 91;
    } catch (std.time::time_error failure) {
        failure as void;
        return 92;
    } catch (std.time::duration_error failure) {
        failure as void;
        return 93;
    } catch (std.fs::path_error failure) {
        failure as void;
        return 94;
    }
}
