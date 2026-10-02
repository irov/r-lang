module test.codegen.sync_value_operations;

i32 main() {
    try {
        std.sync::once_lock<i32> once = std.sync::once_lock::<i32>();
        o<const i32*> before = std.sync::get(&once);
        switch (before) {
            case variant o::none: 
                break;
            case variant o::some(value): 
                value as void; throw TestAssertionFailed {.code = 1};
        }
        std.sync::set_result<i32> first = std.sync::set(&once, 42);
        switch (move first) {
            case variant std.sync::set_result::stored: 
                break;
            case variant std.sync::set_result::occupied(move value): 
                value as void; throw TestAssertionFailed {.code = 2};
        }
        std.sync::set_result<i32> second = std.sync::set(&once, 99);
        switch (move second) {
            case variant std.sync::set_result::stored: 
                throw TestAssertionFailed {.code = 3};
            case variant std.sync::set_result::occupied(move value): 
                if (value != 99) { throw TestAssertionFailed {.code = 4}; }
                break;
        }
        o<const i32*> after = std.sync::get(&once);
        switch (after) {
            case variant o::none: 
                throw TestAssertionFailed {.code = 5};
            case variant o::some(value): 
                if (**value != 42) { throw TestAssertionFailed {.code = 6}; }
                break;
        }
        try {
            std.sync::channel<i32> factory = std.sync::channel::<i32>();
            std.sync::sender<i32> sender = std.sync::sender(&factory);
            std.sync::sender<i32> clone = std.sync::clone_sender(&sender);
            std.sync::receiver<i32> receiver = std.sync::receiver(move factory);
            std.sync::send_result<i32> sent = std.sync::send(&clone, 17);
            switch (move sent) {
                case variant std.sync::send_result::sent: 
                    break;
                case variant std.sync::send_result::disconnected(move value): 
                    value as void; throw TestAssertionFailed {.code = 7};
                case variant std.sync::send_result::allocation_failed(move value): 
                    value as void; throw TestAssertionFailed {.code = 8};
            }
            std.sync::recv_result<i32> received = std.sync::recv(&receiver);
            switch (move received) {
                case variant std.sync::recv_result::received(move value): 
                    if (value != 17) { throw TestAssertionFailed {.code = 9}; }
                    break;
                case variant std.sync::recv_result::disconnected: 
                    throw TestAssertionFailed {.code = 10};
            }
            std.sync::try_recv_result<i32> empty = std.sync::try_recv(&receiver);
            switch (move empty) {
                case variant std.sync::try_recv_result::received(move value): 
                    value as void; throw TestAssertionFailed {.code = 11};
                case variant std.sync::try_recv_result::empty: 
                    break;
                case variant std.sync::try_recv_result::disconnected: 
                    throw TestAssertionFailed {.code = 12};
            }
            drop clone;
            drop sender;
            std.sync::try_recv_result<i32> disconnected = std.sync::try_recv(&receiver);
            switch (move disconnected) {
                case variant std.sync::try_recv_result::received(move value): 
                    value as void; throw TestAssertionFailed {.code = 13};
                case variant std.sync::try_recv_result::empty: 
                    throw TestAssertionFailed {.code = 14};
                case variant std.sync::try_recv_result::disconnected: 
                    break;
            }
            std.sync::sync_channel<i32> bounded = std.sync::sync_channel::<i32>(1usize);
            std.sync::sync_sender<i32> bounded_sender = std.sync::sync_sender(&bounded);
            std.sync::sync_sender<i32> bounded_clone = std.sync::clone_sync_sender(&bounded_sender);
            std.sync::receiver<i32> bounded_receiver = std.sync::sync_receiver(move bounded);
            std.sync::send_result<i32> stored = std.sync::sync_send(&bounded_clone, 71);
            drop stored;
            std.sync::try_send_result<i32> full = std.sync::try_send(&bounded_sender, 72);
            switch (move full) {
                case variant std.sync::try_send_result::sent: 
                    throw TestAssertionFailed {.code = 15};
                case variant std.sync::try_send_result::full(move value): 
                    if (value != 72) { throw TestAssertionFailed {.code = 16}; }
                    break;
                case variant std.sync::try_send_result::disconnected(move value): 
                    value as void; throw TestAssertionFailed {.code = 17};
            }
            drop bounded_receiver;
            std.sync::try_send_result<i32> rejected = std.sync::try_send(&bounded_sender, 73);
            switch (move rejected) {
                case variant std.sync::try_send_result::sent: 
                    throw TestAssertionFailed {.code = 18};
                case variant std.sync::try_send_result::full(move value): 
                    value as void; throw TestAssertionFailed {.code = 19};
                case variant std.sync::try_send_result::disconnected(move value): 
                    if (value != 73) { throw TestAssertionFailed {.code = 20}; }
                    break;
            }
            return 0;
        } catch (std.alloc::alloc_error error) {
            error as void;
            throw TestAssertionFailed {.code = 21};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
