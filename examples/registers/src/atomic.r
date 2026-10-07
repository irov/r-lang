module example.registers.operations;
import example.calculator.common::{Usage};

enum Operation { add, sub, bit_and, bit_or, bit_xor, exchange, cas };

Operation operation_at(const array<std.string::string>* commands, usize index) throws Usage {
    o<const std.string::string*> found = commands->get(index);
    switch (found) {
    case variant o::some(value):
        str text = **value;
        o<Operation> parsed = core::enum_from_name::<Operation>(text);
        switch (parsed) {
        case variant o::some(operation): return *operation;
        case variant o::none: throw Usage { .message = "unknown register operation" };
        }
        break;
    case variant o::none: throw Usage { .message = "missing register operation" };
    }
    throw Usage { .message = "unknown register operation" };
}

u32 number_at(const array<std.string::string>* commands, usize index) throws Usage, std.convert::parse_error {
    o<const std.string::string*> found = commands->get(index);
    switch (found) {
    case variant o::some(value):
        str text = **value;
        return std.convert::parse_u32(text, 10u32);
    case variant o::none: throw Usage { .message = "missing register value" };
    }
}

std.string::string script(u32 initial, const array<std.string::string>* commands) throws Usage, std.convert::parse_error, std.alloc::alloc_error {
    au32 register_value = 0u32;
    core::atomic_store(&register_value, initial, core::memory_order::release);
    std.string::string output = std.string::create();
    usize index = 0usize;
    while (index < len(*commands)) {
        Operation operation = operation_at(commands, index);
        index += 1usize;
        throw (index == len(*commands)) Usage { .message = "register operation needs a value" };
        u32 operand = number_at(commands, index);
        index += 1usize;
        u32 before = 0u32;
        bool changed = true;
        switch (operation) {
        case Operation::add: before = core::atomic_fetch_add(&register_value, operand, core::memory_order::relaxed); break;
        case Operation::sub: before = core::atomic_fetch_sub(&register_value, operand, core::memory_order::relaxed); break;
        case Operation::bit_and: before = core::atomic_fetch_and(&register_value, operand, core::memory_order::relaxed); break;
        case Operation::bit_or: before = core::atomic_fetch_or(&register_value, operand, core::memory_order::relaxed); break;
        case Operation::bit_xor: before = core::atomic_fetch_xor(&register_value, operand, core::memory_order::relaxed); break;
        case Operation::exchange: before = core::atomic_exchange(&register_value, operand, core::memory_order::acq_rel); break;
        case Operation::cas:
            throw (index == len(*commands)) Usage { .message = "cas needs expected and desired values" };
            u32 desired = number_at(commands, index);
            index += 1usize;
            core::atomic_compare_exchange_result<u32> result = core::atomic_compare_exchange(&register_value, operand, desired, core::memory_order::acq_rel, core::memory_order::acquire);
            switch (move result) {
            case variant core::atomic_compare_exchange_result::exchanged(move observed): before = observed; break;
            case variant core::atomic_compare_exchange_result::unchanged(move observed): before = observed; changed = false; break;
            }
            break;
        }
        u32 after = core::atomic_load(&register_value, core::memory_order::acquire);
        constexpr str name = core::enum_name(operation);
        std.string::string row = f"{name} before={before} after={after} accepted={changed}\n";
        str text = row;
        output.append(text);
    }
    u32 final = core::atomic_load(&register_value, core::memory_order::acquire);
    bool lock_free = core::atomic_is_lock_free(&register_value);
    std.string::string row = f"value={final} lock_free={lock_free}\n";
    str text = row;
    output.append(text);
    return move output;
}

struct Tickets { au32 next; };
error TicketError { WorkerFailed };

u64 issue(const Tickets* tickets, u32 count) {
    u64 total = 0u64;
    for (u32 index = 0u32; index < count; index += 1u32) {
        u32 ticket = core::atomic_fetch_add(&tickets->next, 1u32, core::memory_order::relaxed);
        total += ticket as u64;
    }
    return total;
}

std.string::string race(u32 iterations) throws std.thread::thread_error, std.alloc::alloc_error, TicketError {
    Tickets tickets = { .next = 0u32 };
    thread_scope {
        std.thread::scoped_join_handle<u64> worker = std.thread::spawn_scoped(issue, &tickets, iterations);
        u64 total = issue(&tickets, iterations);
        std.thread::join_result<u64> result = (move worker).join();
        switch (move result) {
        case variant std.thread::join_result::returned(move sum): total += sum; break;
        case variant std.thread::join_result::panicked(move report): throw TicketError::WorkerFailed;
        }
        u32 count = core::atomic_load(&tickets.next, core::memory_order::acquire);
        return f"tickets={count} sum={total}\n";
    }
}
