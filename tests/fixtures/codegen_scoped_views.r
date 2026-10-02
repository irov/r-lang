module test.codegen.scoped_views;

/* R-STMT-0017: exclusive and shared slice loans into scoped tasks; the parent's storage
   stays borrowed until the all-members barrier or the group exit. */
async void pause() {}

@scoped
async usize fill(u8[] target, u8 value) throws std.async::start_error {
    usize count = len(target);
    for (usize index = 0usize; index < count; index += 1usize) { target[index] = value; }
    await pause();
    const u8[] written = target[0usize..count];
    return len(written);
}

@scoped
async u32 total(const u8[] source) throws std.async::start_error {
    await pause();
    u32 sum = 0u32;
    for (usize index = 0usize; index < len(source); index += 1usize) { sum += source[index] as u32; }
    return sum;
}

struct Results { usize filled; u32 first; u32 second; };

async i32 main() {
    try {
        try {
            bytes buffer = std.alloc::bytes(8usize, 0u8);
            Results results = {.filled = 0usize, .first = 0u32, .second = 0u32};
            task_scope(1) writer {
                auto operation = fill(buffer.as_slice_mut(), 3u8);
                results.filled = await move operation;
            }
            if (results.filled != 8usize) { throw TestAssertionFailed {.code = 1}; }
            task_scope(2) readers {
                auto left = total(buffer.as_slice());
                auto right = total(buffer.as_slice());
                const u8[] parent_view = buffer.as_slice();
                if (parent_view[7] != 3u8) { throw TestAssertionFailed {.code = 2}; }
                results.first = await move left;
                results.second = await move right;
            }
            if (results.first != 24u32 || results.second != 24u32) { throw TestAssertionFailed {.code = 3}; }
            task_scope(1) rewrite {
                auto operation = fill(buffer.as_slice_mut(), 5u8);
                await rewrite.all();
                const u8[] after_barrier = buffer.as_slice();
                if (after_barrier[0] != 5u8) { throw TestAssertionFailed {.code = 4}; }
                results.filled = await move operation;
            }
            i32 selected = results.filled == 8usize ? 0 : 5;
            return selected;
        } catch (std.alloc::alloc_error failure) { throw TestAssertionFailed {.code = 71}; }
        catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 75}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
