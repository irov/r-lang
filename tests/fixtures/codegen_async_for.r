module test.codegen.async_for;

import std.text;

/* R-STMT-0021 (L36): `for (T x in &source)` in an async function awaits the next item at each
   iteration: the asynchronous receive of a std.sync::receiver<T>, or the scoped `next` of a
   core::AsyncIterator started in the enclosing task group. none ends the loop. */

struct Countdown { u32 left; };

impl core::AsyncIterator for Countdown {
    type Item = u32;
    @scoped async o<u32> next(Countdown* this) throws std.error::fault {
        if (this->left == 0u32) { return o::none; }
        this->left -= 1u32;
        return o::some(this->left);
    }
};

/* Owned items move into the loop variable and are destroyed with it. */
struct Words { u32 produced; };

impl core::AsyncIterator for Words {
    type Item = std.string::string;
    @scoped async o<std.string::string> next(Words* this) throws std.error::fault {
        if (this->produced == 3u32) { return o::none; }
        this->produced += 1u32;
        std.string::string word = f"word{this->produced}";
        return o::some(move word);
    }
};

/* The second call of next fails; the error leaves the loop like any throw. */
struct Failing { u32 calls; };

impl core::AsyncIterator for Failing {
    type Item = u32;
    @scoped async o<u32> next(Failing* this) throws std.error::fault {
        this->calls += 1u32;
        if (this->calls == 2u32) {
            throw std.io::io_error {.code = std.io::error_code::closed, .native_code = 0i64};
        }
        return o::some(this->calls);
    }
};

async i32 counted() throws std.error::fault {
    Countdown counter = Countdown {.left = 4u32};
    u32 total = 0u32;
    u32 steps = 0u32;
    task_scope(1) walk {
        for (u32 value in &counter) {
            total += value;
            steps += 1u32;
        }
    }
    if (total != 6u32 || steps != 4u32) { return 1; }
    // The iterator keeps its state between loops: it is exhausted now.
    task_scope(1) again {
        for (u32 value in &counter) {
            value as void;
            return 2;
        }
    }
    return 0;
}

async i32 controlled() throws std.error::fault {
    Countdown counter = Countdown {.left = 10u32};
    u32 seen = 0u32;
    task_scope(1) walk {
        for (u32 value in &counter) {
            if (value == 8u32) { continue; }
            if (value == 5u32) { break; }
            seen += value;
        }
    }
    // 9, 7, 6 were added; 8 was skipped and 5 ended the loop.
    if (seen != 22u32) { return 11; }
    if (counter.left != 5u32) { return 12; }
    task_scope(1) early {
        for (u32 value in &counter) {
            if (value == 3u32) { return 0; }
        }
    }
    return 13;
}

/* R-STMT-0004: labeled jumps around an asynchronous loop. */
async i32 labeled() throws std.error::fault {
    Countdown counter = Countdown {.left = 10u32};
    u32 rounds = 0u32;
    u32 seen = 0u32;
    task_scope(1) walk {
        outer: while (rounds < 5u32) {
            rounds += 1u32;
            for (u32 value in &counter) {
                if (value == 7u32) { continue outer; }
                if (value == 4u32) { break outer; }
                seen += value;
            }
        }
    }
    // 9 and 8 in the first round, 6 and 5 in the second, then 4 ends both loops.
    if (seen != 28u32 || rounds != 2u32 || counter.left != 4u32) { return 51; }
    return 0;
}

async o<u32> half(u32 value) {
    if (value == 0u32) { return o::none; }
    return o::some(value / 2u32);
}

/* R-STMT-0002: `while (value is pattern)` with an awaited value. */
async i32 halving() throws std.async::start_error {
    u32 current = 64u32;
    u32 steps = 0u32;
    while (await half(current) is variant o::some(next)) {
        if (next == 0u32) { break; }
        current = next;
        steps += 1u32;
    }
    if (steps != 6u32 || current != 1u32) { return 61; }
    return 0;
}

async i32 owned_items() throws std.error::fault {
    Words words = Words {.produced = 0u32};
    std.string::string joined = std.string::create();
    task_scope(1) walk {
        for (std.string::string word in &words) {
            joined.append(word);
        }
    }
    if (std.text::equal_ignore_ascii_case(joined, "word1word2word3") == false) {
        return 21;
    }
    return 0;
}

async i32 failing() {
    Failing source = Failing {.calls = 0u32};
    u32 got = 0u32;
    try {
        task_scope(1) walk {
            for (u32 value in &source) {
                got += value;
            }
        }
        return 31;
    } catch (std.io::io_error failure) {
        if (failure.code != std.io::error_code::closed) { return 32; }
    } catch (std.error::fault other) {
        return 33;
    }
    if (got != 1u32 || source.calls != 2u32) { return 34; }
    return 0;
}

// Sends 1..count from another task, then drops its sender.
async void produce(std.sync::sender<u64> sender, u64 count) {
    u64 value = 1u64;
    while (value <= count) {
        switch (std.sync::send(&sender, value)) {
        case variant std.sync::send_result::sent: break;
        case variant std.sync::send_result::disconnected(move rejected): rejected as void;
        case variant std.sync::send_result::allocation_failed(move rejected): rejected as void;
        }
        value += 1u64;
    }
}

async u64 drain(std.sync::receiver<u64> events) throws std.async::start_error {
    u64 sum = 0u64;
    for (u64 amount in &events) {
        sum += amount;
    }
    return sum;
}

async i32 received() throws std.alloc::alloc_error, std.async::start_error {
    std.sync::channel<u64> factory = std.sync::channel::<u64>();
    std.sync::sender<u64> sender = std.sync::sender(&factory);
    task<void> producer = produce(move sender, 100u64);
    std.async::detach(move producer);
    std.sync::receiver<u64> inbox = std.sync::receiver(move factory);
    u64 sum = await drain(move inbox);
    if (sum != 5050u64) { return 41; }
    return 0;
}

async i32 main() {
    i32 first = await counted();
    if (first != 0) { return first; }
    i32 second = await controlled();
    if (second != 0) { return second; }
    i32 third = await owned_items();
    if (third != 0) { return third; }
    i32 fourth = await failing();
    if (fourth != 0) { return fourth; }
    i32 fifth = await received();
    if (fifth != 0) { return fifth; }
    i32 sixth = await labeled();
    if (sixth != 0) { return sixth; }
    i32 seventh = await halving();
    if (seventh != 0) { return seventh; }
    return 0;
}
