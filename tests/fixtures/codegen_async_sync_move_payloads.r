module test.codegen.async_sync_move_payloads;

thread_local i32 destroyed = 0;

struct Value {
    i32 number;
};

drop(Value* self) {
    destroyed += self->number;
}

async i32 main() {
    try {
        try {
            std.sync::once_lock<Value> once = std.sync::once_lock::<Value>();
            Value first = {.number = 1};
            std.sync::set_result<Value> stored = std.sync::set(&once, move first);
            drop stored;
            Value second = {.number = 10};
            std.sync::set_result<Value> occupied = std.sync::set(&once, move second);
            if (destroyed != 0) { throw TestAssertionFailed {.code = 1}; }
            switch (move occupied) {
                case variant std.sync::set_result::stored: throw TestAssertionFailed {.code = 2};
                case variant std.sync::set_result::occupied(move returned):
                    if (returned.number != 10) { throw TestAssertionFailed {.code = 3}; }
                    drop returned;
                    break;
            }
            if (destroyed != 10) { throw TestAssertionFailed {.code = 4}; }
            drop once;
            if (destroyed != 11) { throw TestAssertionFailed {.code = 5}; }

            std.sync::sync_channel<Value> factory = std.sync::sync_channel::<Value>(1usize);
            std.sync::sync_sender<Value> sender = std.sync::sync_sender(&factory);
            std.sync::receiver<Value> receiver = std.sync::sync_receiver(move factory);
            Value sent = {.number = 100};
            std.sync::send_result<Value> sent_result = std.sync::sync_send(&sender, move sent);
            drop sent_result;
            Value rejected = {.number = 1000};
            std.sync::try_send_result<Value> full = std.sync::try_send(&sender, move rejected);
            if (destroyed != 11) { throw TestAssertionFailed {.code = 6}; }
            switch (move full) {
                case variant std.sync::try_send_result::sent: throw TestAssertionFailed {.code = 7};
                case variant std.sync::try_send_result::full(move returned):
                    if (returned.number != 1000) { throw TestAssertionFailed {.code = 8}; }
                    drop returned;
                    break;
                case variant std.sync::try_send_result::disconnected(move returned):
                    drop returned;
                    throw TestAssertionFailed {.code = 9};
            }
            if (destroyed != 1011) { throw TestAssertionFailed {.code = 10}; }
            std.sync::recv_result<Value> received = std.sync::recv(&receiver);
            if (destroyed != 1011) { throw TestAssertionFailed {.code = 11}; }
            switch (move received) {
                case variant std.sync::recv_result::received(move value):
                    if (value.number != 100) { throw TestAssertionFailed {.code = 12}; }
                    drop value;
                    break;
                case variant std.sync::recv_result::disconnected: throw TestAssertionFailed {.code = 13};
            }
            if (destroyed != 1111) { throw TestAssertionFailed {.code = 14}; }
            Value queued = {.number = 10000};
            std.sync::send_result<Value> queued_result = std.sync::sync_send(&sender, move queued);
            drop queued_result;
            drop receiver;
            if (destroyed != 11111) { throw TestAssertionFailed {.code = 15}; }
            Value disconnected = {.number = 100000};
            std.sync::send_result<Value> disconnected_result = std.sync::sync_send(&sender, move disconnected);
            if (destroyed != 11111) { throw TestAssertionFailed {.code = 16}; }
            drop disconnected_result;
            if (destroyed != 111111) { throw TestAssertionFailed {.code = 17}; }
            return 0;
        } catch (std.alloc::alloc_error error) {
            error as void;
            throw TestAssertionFailed {.code = 18};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
