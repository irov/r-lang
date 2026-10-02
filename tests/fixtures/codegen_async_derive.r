module fixture.async_derive;

import std.string;

@derive(clone, equal, ordered, key)
enum Command { start(u32), stop, pause { u32 seconds; u32 repeat; } };

@derive(clone, equal, ordered)
struct Job { u32 id; Command command; std.string::string owner; };

/* R-STMT-0010 (L27-1): a Move o place without outer move is borrowed, not consumed. */
async u32 owner_length(o<Job> pending) {
    u32 length = 0u32;
    switch (pending) {
    case variant o::some(job): length = job->owner.len() as u32;
    case variant o::none: length = 99u32;
    }
    switch (move pending) {
    case variant o::some(move job): length += job.id;
    case variant o::none: length += 1u32;
    }
    return length;
}

async i32 compare(Job left, Job right) throws std.alloc::alloc_error {
    Job copy = core::clone(&left);
    if (copy.eq(&left) == false) { return 1; }
    std.cmp::ordering order = left.cmp(&right);
    if (order != std.cmp::ordering::less) { return 2; }
    Command command = core::clone(&right.command);
    if (core::key_equal(&command, &right.command) == false) { return 3; }
    if (core::hash(&command) != core::hash(&right.command)) { return 4; }
    return 0;
}

async i32 main() {
    Job first = {.id = 1u32, .command = Command::start(5u32), .owner = std.string::from_str("ann")};
    Job second = {
        .id = 1u32,
        .command = Command::pause {.seconds = 2u32, .repeat = 1u32},
        .owner = std.string::from_str("bob")};
    i32 status = await compare(move first, move second);
    if (status != 0) { return status; }
    Job third = {.id = 7u32, .command = Command::stop, .owner = std.string::from_str("carol")};
    u32 length = await owner_length(o::some(move third));
    if (length != 12u32) { return 10; }
    u32 none = await owner_length(o::none);
    if (none != 100u32) { return 11; }
    return 0;
}
