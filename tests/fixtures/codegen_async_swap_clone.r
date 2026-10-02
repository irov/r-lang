module test.codegen.async_swap_clone;
import std.slice;

/* R-OWN-0019, R-OWN-0020 (L26) in async frames: exchanged and cloned values live across
   suspensions, a generic clone runs in a task, and every tracked value is destroyed once. */
struct Counter { atomic u32 drops; };
struct Tracked { arc Counter counter; i32 value; };
drop(Tracked* self) {
    const Counter* c = &*self->counter;
    core::atomic_fetch_add(&c->drops, 1u32, core::memory_order::relaxed) as void;
}
Tracked Tracked::clone(const Tracked* value) {
    return Tracked {.counter = core::clone(&value->counter), .value = value->value + 1000};
}

u32 drops(const (arc Counter)* counter) {
    const Counter* c = &**counter;
    return core::atomic_load(&c->drops, core::memory_order::relaxed);
}

async void pause() throws std.error::fault {
    await std.time::sleep_for(std.time::duration_from_seconds(0i64));
}

@generic<T: clone & unborrowed & send>
async T copied(T value) throws std.error::fault {
    await pause();
    T copy = core::clone(&value);
    await pause();
    return move copy;
}

async i32 run() throws TestAssertionFailed, std.error::fault, std.array::push_error<std.string::string>,
    std.array::push_error<Tracked> {
    std.string::string left = std.string::from_str("left");
    std.string::string right = std.string::from_str("right!");
    await pause();
    core::swap(&left, &right);
    await pause();
    if (left.len() != 6usize || right.len() != 4usize) { throw TestAssertionFailed {.code = 1}; }
    array<std.string::string> words = std.array::create();
    words.push(std.string::from_str("one"));
    words.push(std.string::from_str("three"));
    array<std.string::string> words_copy = core::clone(&words);
    await pause();
    words_copy.push(std.string::from_str("x"));
    if (len(words) != 2usize || len(words_copy) != 3usize) { throw TestAssertionFailed {.code = 2}; }
    std.string::string[] view = words_copy.as_slice_mut();
    std.slice::rotate_left(view, 1usize);
    if (words_copy[0].len() != 5usize) { throw TestAssertionFailed {.code = 3}; }
    arc Counter counter = new arc Counter {.drops = 0u32};
    array<Tracked> tracked = std.array::create();
    tracked.push(Tracked {.counter = core::clone(&counter), .value = 1});
    array<Tracked> tracked_copy = core::clone(&tracked);
    await pause();
    if (tracked_copy[0].value != 1001) { throw TestAssertionFailed {.code = 4}; }
    drop tracked_copy;
    drop tracked;
    if (drops(&counter) != 2u32) { throw TestAssertionFailed {.code = 5}; }
    std.string::string text = std.string::from_str("task");
    std.string::string text_copy = await copied(move text);
    i64 number = await copied(41i64);
    if (text_copy.len() != 4usize || number != 41i64) { throw TestAssertionFailed {.code = 6}; }
    return 0;
}

async i32 main() {
    try {
        return await run();
    } catch (TestAssertionFailed failure) {
        return failure.code;
    } catch (std.array::push_error<std.string::string> failure) {
        return 90;
    } catch (std.array::push_error<Tracked> failure) {
        return 91;
    } catch (std.error::fault failure) {
        return 92;
    }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
