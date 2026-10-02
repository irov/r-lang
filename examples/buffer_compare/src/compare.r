module example.buffer_compare.compare;

struct CommandStorage1 { std.string::string value; };

struct Pair {
    std.secret::buffer expected;
    std.secret::buffer candidate;
};

std.string::string compare(str expected, str candidate)
    throws std.alloc::alloc_error, std.bytes::bytes_error, std.alloc::new_error<Pair> {
    usize expected_length = len(expected);
    usize candidate_length = len(candidate);
    std.secret::buffer protected_input = std.secret::with_length(expected_length);
    {
        u8[] destination = protected_input.as_slice_mut();
        usize copied = std.bytes::copy(destination, expected); copied as void;
    }
    bytes received = std.alloc::bytes(candidate_length, 0u8);
    {
        u8[] destination = received.as_slice_mut();
        usize copied = std.bytes::copy(destination, candidate); copied as void;
    }
    // Reuse the received allocation when adopting a buffer from an input operation.
    std.secret::buffer protected_candidate = std.secret::from_bytes(move received);
    Pair pair = { .expected = move protected_input, .candidate = move protected_candidate };
    own Pair* request = std.alloc::try_new(move pair);
    CommandStorage1 state_output = {.value = std.string::create()};
    {
        const u8[] left = std.secret::as_slice(&request->expected);
        const u8[] right = std.secret::as_slice(&request->candidate);
        bool equal = std.secret::constant_time_equal(left, right);
        usize left_length = std.secret::len(&request->expected);
        usize right_length = std.secret::len(&request->candidate);
        state_output.value = f"equal={equal} expected_bytes={left_length} candidate_bytes={right_length}\n";
    }
    // An opaque handoff retains the allocation and its destructor; no raw dereference occurs.
    unsafe {
        raw Pair* token = core::release(move request);
        own Pair* restored = core::adopt(token);
        Pair finished = std.alloc::into_value(move restored);
        u8[] left = std.secret::as_slice_mut(&finished.expected);
        std.secret::zeroize(left);
        u8[] right = std.secret::as_slice_mut(&finished.candidate);
        std.secret::zeroize(right);
    }
    state_output.value.append("buffers_erased\n");
    return core::replace(&state_output.value, std.string::create());
}
