module test.codegen.switch_dispatch;

/* R-STMT-0024 (L46): `continue name (value);` selects anew in the labeled switch `name`; a clause
   that completes leaves the switch, `break name;` leaves it from a nested loop, and a plain
   `continue;` continues the loop around the switch. */

enum Op : u8 {
    push = 0,
    add = 1,
    loop_back = 2,
    halt = 3,
};

error Fault {
    code_error,
};

Op decode(u8 byte) throws Fault {
    o<Op> op = core::enum_at::<Op>(byte as usize);
    switch (op) {
    case variant o::some(value):
        return *value;
    case variant o::none:
        break;
    }
    throw Fault::code_error;
}

/* A small machine: every clause chooses the next instruction. */
i64 execute(const u8[] code, i64 rounds) throws Fault {
    i64 accumulator = 0i64;
    i64 counter = 0i64;
    usize pc = 0usize;
    run: switch (decode(code[pc])) {
    case Op::push:
        accumulator += code[pc + 1usize] as i64;
        pc += 2usize;
        continue run (decode(code[pc]));
    case Op::add:
        counter += 1i64;
        pc += 1usize;
        continue run (decode(code[pc]));
    case Op::loop_back:
        if (counter < rounds) {
            pc = 0usize;
            continue run (decode(code[pc]));
        }
        pc += 1usize;
        continue run (decode(code[pc]));
    case Op::halt:
        return accumulator;
    }
    return -1i64;
}

/* Owned objects of a clause are dropped when it selects anew; a nested loop leaves the switch
   with `break name;` and selects anew from inside with `continue name (value);`. */
usize words(u32 start) throws std.alloc::alloc_error {
    usize total = 0usize;
    step: switch (start) {
    case 0u32:
        std.string::string word = std.string::from_str("zero");
        str text = word;
        total += len(text);
        continue step (1u32);
    case 1u32:
        for (usize index = 0usize; index < 3usize; index += 1usize) {
            if (index == 2usize) {
                continue step (2u32);
            }
            total += 10usize;
        }
    case 2u32:
        std.string::string word = std.string::from_str("two");
        str text = word;
        total += len(text) * 100usize;
        while (true) {
            break step;
        }
    default:
        total += 1000usize;
    }
    return total;
}

/* A plain continue continues the loop around the switch. */
u32 skip_one() {
    u32 seen = 0u32;
    for (u32 index = 0u32; index < 5u32; index += 1u32) {
        step: switch (index) {
        case 1u32:
            continue;
        case 3u32:
            break step;
        default:
            seen += 1u32;
        }
    }
    return seen;
}

/* Defect L46-4: a change in a clause conflicts only with uses that a selection anew may reach: a
   clause that completes, or leaves with `break name;`, ends the switch, and the operand is read
   once before the first selection. */
u32 borrow_flow(u32 start) {
    u32 counter = 0u32;
    u32* pointer = &counter;
    step: switch (start) {
    case 0u32:
        *pointer += 1u32;
        continue step (1u32);
    case 1u32:
        counter += 10u32;
        break step;
    default:
        break;
    }
    return counter;
}

u32 borrowed_operand(u32 start) {
    u32 counter = start;
    const u32* pointer = &counter;
    step: switch (*pointer) {
    case 0u32:
        counter += 1u32;
        continue step (5u32);
    default:
        counter += 100u32;
    }
    return counter;
}

/* Defect L46-6: a fieldless enum of the library selects as one of the program does. */
u32 signal_steps(std.signal::kind kind) {
    u32 steps = 0u32;
    walk: switch (kind) {
    case std.signal::kind::user1:
        steps += 1u32;
        continue walk (std.signal::kind::user2);
    default:
        return steps + 10u32;
    }
}

/* A labeled switch in a function evaluated at translation (Core R-FUNC-0023). */
u32 collatz_steps(u32 start) {
    u32 steps = 0u32;
    u32 value = start;
    walk: switch (value % 2u32) {
    case 0u32:
        if (value == 0u32) {
            return steps;
        }
        value /= 2u32;
        steps += 1u32;
        continue walk (value % 2u32);
    default:
        if (value == 1u32) {
            return steps;
        }
        value = value * 3u32 + 1u32;
        steps += 1u32;
        continue walk (value % 2u32);
    }
}

const u32 STEPS_OF_27 = collatz_steps(27u32);

i32 main(const str[] arguments) {
    u8[7] code = {0u8, 5u8, 1u8, 2u8, 3u8, 0u8, 0u8};
    try {
        if (execute(code[0usize..7usize], 3i64) != 15i64) { return 1; }
        u8[2] broken = {9u8, 0u8};
        i64 unreached = execute(broken[0usize..2usize], 1i64);
        unreached as void;
        return 2;
    } catch (Fault failure) {
        failure as void;
    }
    try {
        if (words(0u32) != 324usize) { return 3; }
        if (words(7u32) != 1000usize) { return 4; }
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 8;
    }
    if (skip_one() != 3u32) { return 5; }
    if (STEPS_OF_27 != 111u32) { return 6; }
    u32 start = 26u32 + len(arguments) as u32;
    if (collatz_steps(start) != STEPS_OF_27) { return 7; }
    if (borrow_flow(0u32) != 11u32) { return 9; }
    if (borrowed_operand(0u32) != 101u32) { return 10; }
    if (signal_steps(std.signal::kind::user1) != 11u32) { return 11; }
    return 0;
}
