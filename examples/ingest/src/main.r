module example.ingest.main;
import std.alloc;
import std.arena;
import std.pool;
import std.console;
import std.convert;
import example.ingest.parse;
import example.ingest.batch;

/* ingest parses messages of "key=value;key=value" fields. Each batch of lines is parsed by a
   worker task into an arena that a pool lends it, inside a budget of its own; the report of a
   batch reads its first message back from the arena and shows what the budget was charged. */
enum Command { demo, run };

error Usage { u32 code; };

protected const str usage_text = "ingest demo | run BATCH_LINES BUDGET_BYTES < MESSAGES\n";

/* The arenas of the workers: blocks of 256 bytes, at most 1024 bytes in all. */
protected const usize block_size = 256usize;
protected const usize arena_limit = 1024usize;

protected std.pool::pool<std.arena::arena> arenas(usize count) throws std.alloc::alloc_error {
    array<std.arena::arena> made = [];
    for (usize index = 0usize; index < count; index += 1usize) {
        try {
            made.push(std.arena::arena::create(block_size, arena_limit));
        } catch (std.array::push_error<std.arena::arena> rejected) {
            drop rejected;
            throw std.alloc::alloc_error::out_of_memory;
        }
    }
    return std.pool::pool<std.arena::arena>::create(move made);
}

protected void push_line(array<std.string::string>* lines, std.string::string text)
    throws std.alloc::alloc_error {
    try {
        lines->push(move text);
    } catch (std.array::push_error<std.string::string> rejected) {
        drop rejected;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

protected void push_report(array<example.ingest.batch::report>* reports, example.ingest.batch::report item)
    throws std.alloc::alloc_error {
    try {
        reports->push(move item);
    } catch (std.array::push_error<example.ingest.batch::report> rejected) {
        drop rejected;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

/* Lines start..start + count of lines, copied; fewer at the end. */
protected array<std.string::string> slice_of(const array<std.string::string>* lines, usize start, usize count)
    throws std.alloc::alloc_error {
    array<std.string::string> part = [];
    for (usize index = start; index < len(*lines) && index < start + count; index += 1usize) {
        push_line(&part, std.string::from_str((*lines)[index]));
    }
    return move part;
}

protected std.string::string amount(o<usize> value) throws std.alloc::alloc_error {
    switch (value) {
    case variant o::some(bytes):
        usize plain = *bytes;
        return f"{plain}";
    case variant o::none: break;
    }
    return f"no limit";
}

/* The report of a batch as text: a heading line and indented details. */
protected std.string::string describe(const example.ingest.batch::report* item) throws std.alloc::alloc_error {
    usize first = item->first_line;
    usize last = item->first_line + item->lines - 1usize;
    usize budget_bytes = item->budget_bytes;
    usize messages = item->messages;
    switch (item->refused) {
    case variant o::some(failure):
        str reason = core::enum_name(*failure);
        usize count = item->lines;
        return f"lines {first}-{last}: the budget of {budget_bytes} bytes refused the batch ({reason}) after {messages} of {count} lines\n";
    case variant o::none: break;
    }
    usize fields = item->fields;
    usize rejected = len(item->rejected);
    std.string::string text = f"lines {first}-{last}: {messages} messages, {fields} fields, {rejected} rejected\n";
    if (messages > 0usize) {
        std.string::string sample = std.string::from_str(item->first);
        std.string::string first_line = f"  first message: {sample}\n";
        text.append(first_line);
    }
    for (usize index = 0usize; index < len(item->rejected); index += 1usize) {
        example.ingest.batch::rejection failed = item->rejected[index];
        usize line = failed.line;
        example.ingest.parse::problem reason = failed.reason;
        usize position = failed.position;
        std.string::string rejected_line = f"  line {line} rejected: {reason} at byte {position}\n";
        text.append(rejected_line);
    }
    usize stored = item->stored;
    usize reserved = item->reserved;
    std.string::string arena_line = f"  arena: {stored} bytes stored in {reserved} reserved\n";
    text.append(arena_line);
    switch (item->charged) {
    case variant o::some(seen):
        usize bytes = seen->bytes;
        std.string::string room = amount(seen->bytes_available);
        std.string::string budget_line = f"  budget of {budget_bytes} bytes: {bytes} charged, {room} left\n";
        text.append(budget_line);
    case variant o::none: break;
    }
    return move text;
}

async void say(std.string::string text) throws std.error::fault {
    await std.console::println(move text);
}

protected array<std.string::string> first_batch() throws std.alloc::alloc_error {
    array<std.string::string> lines = [];
    push_line(&lines, std.string::from_str("device=boiler;metric=temperature;value=21.5"));
    push_line(&lines, std.string::from_str("device=attic;metric=humidity;value=48"));
    push_line(&lines, std.string::from_str("boiler temperature 21.5"));
    push_line(&lines, std.string::from_str("device=cellar;metric=temperature;value=12.0"));
    return move lines;
}

protected array<std.string::string> second_batch() throws std.alloc::alloc_error {
    array<std.string::string> lines = [];
    push_line(&lines, std.string::from_str("device=garage;metric=temperature;value=8.5"));
    push_line(&lines, std.string::from_str("device=garage;=7"));
    std.string::string image = std.string::from_str("device=camera;image=");
    for (usize index = 0usize; index < 1100usize; index += 1usize) { image.append("x"); }
    push_line(&lines, move image);
    push_line(&lines, std.string::from_str("device=porch;metric=light;value=300;unit=lux"));
    return move lines;
}

protected array<std.string::string> third_batch() throws std.alloc::alloc_error {
    array<std.string::string> lines = [];
    push_line(&lines, std.string::from_str("device=boiler;metric=pressure;value=1.4"));
    push_line(&lines, std.string::from_str("device=boiler;metric=flow;value=12"));
    push_line(&lines, std.string::from_str("device=boiler;metric=power;value=18"));
    return move lines;
}

@generic<T>
protected bool absent(const (o<T>)* value) {
    switch (*value) {
    case variant o::some(held): return false;
    case variant o::none: break;
    }
    return true;
}

protected bool no_budget() {
    o<std.alloc::usage> now = std.alloc::budget_usage();
    switch (now) {
    case variant o::some(seen): return false;
    case variant o::none: break;
    }
    return true;
}

/* Two leases of a pool of two, a third that finds none, two batches at once and a third batch
   whose budget is too small for its messages. */
async void demo() throws example.ingest.batch::no_arena, std.arena::arena_error, std.error::fault {
    std.pool::pool<std.arena::arena> pool = arenas(2usize);
    usize capacity = pool.capacity();
    usize free = pool.available();
    usize block = block_size;
    usize limit = arena_limit;
    await say(f"pool: {capacity} arenas of {block}-byte blocks, at most {limit} bytes each, {free} available");
    o<std.pool::lease<std.arena::arena>> one = pool.acquire();
    o<std.pool::lease<std.arena::arena>> two = pool.acquire();
    o<std.pool::lease<std.arena::arena>> three = pool.acquire();
    usize left = pool.available();
    bool third_none = absent(&three);
    await say(f"two leases: {left} available, a third acquire finds none: {third_none}");
    drop one;
    usize back = pool.available();
    await say(f"one lease dropped: {back} available");
    drop two;
    drop three;
    array<example.ingest.batch::report> reports = [];
    task_scope(2) workers {
        auto a = example.ingest.batch::ingest(pool.share(), first_batch(), 1usize, 8192usize);
        auto b = example.ingest.batch::ingest(pool.share(), second_batch(), 5usize, 8192usize);
        example.ingest.batch::report first = await move a;
        example.ingest.batch::report second = await move b;
        push_report(&reports, move first);
        push_report(&reports, move second);
    }
    example.ingest.batch::report third =
        await example.ingest.batch::ingest(pool.share(), third_batch(), 9usize, 1000usize);
    push_report(&reports, move third);
    for (usize index = 0usize; index < len(reports); index += 1usize) {
        await std.console::print(describe(&reports[index]));
    }
    usize after = pool.available();
    bool outside = no_budget();
    await say(f"after the batches: {after} available; outside every budget block budget_usage() is none: {outside}");
}

/* Reads every line of the standard input. */
async array<std.string::string> read_all() throws std.error::fault {
    array<std.string::string> lines = [];
    bool more = true;
    while (more == true) {
        o<std.string::string> line = await std.console::read_line();
        switch (move line) {
        case variant o::some(move text): push_line(&lines, move text);
        case variant o::none: more = false;
        }
    }
    return move lines;
}

/* Batches of batch_lines lines, two at a time, each in a budget of budget_bytes. */
async void run(usize batch_lines, usize budget_bytes)
    throws example.ingest.batch::no_arena, std.arena::arena_error, std.error::fault {
    array<std.string::string> lines = await read_all();
    std.pool::pool<std.arena::arena> pool = arenas(2usize);
    usize workers = pool.capacity();
    usize batches = 0usize;
    usize messages = 0usize;
    usize rejected = 0usize;
    usize refused = 0usize;
    usize next = 0usize;
    while (next < len(lines)) {
        array<std.string::string> left = slice_of(&lines, next, batch_lines);
        usize left_first = next + 1usize;
        next += len(left);
        array<std.string::string> right = slice_of(&lines, next, batch_lines);
        usize right_first = next + 1usize;
        next += len(right);
        array<example.ingest.batch::report> reports = [];
        task_scope(2) workers {
            auto a = example.ingest.batch::ingest(pool.share(), move left, left_first, budget_bytes);
            auto b = example.ingest.batch::ingest(pool.share(), move right, right_first, budget_bytes);
            example.ingest.batch::report first = await move a;
            example.ingest.batch::report second = await move b;
            push_report(&reports, move first);
            push_report(&reports, move second);
        }
        for (usize index = 0usize; index < len(reports); index += 1usize) {
            const example.ingest.batch::report* item = &reports[index];
            if (item->lines > 0usize) {
                batches += 1usize;
                messages += item->messages;
                rejected += len(item->rejected);
                if (absent(&item->refused) == false) { refused += 1usize; }
                await std.console::print(describe(item));
            }
        }
    }
    await say(f"total: batches {batches}, messages {messages}, rejected lines {rejected}, refused batches {refused}, arenas {workers}");
}

async i32 main() {
    array<std.string::string> arguments = std.env::arguments();
    usize given = len(arguments);
    o<Command> command = o::none;
    if (given >= 2usize) { command = core::enum_from_name::<Command>(arguments[1]); }
    switch (command) {
    case variant o::none:
        await std.console::eprint(std.string::from_str(usage_text));
        if (given == 1usize) { return 0; }
        return 64;
    case variant o::some(selected):
        try {
            switch (*selected) {
            case Command::demo:
                throw (given != 2usize) Usage {.code = 2u32};
                await demo();
            case Command::run:
                throw (given != 4usize) Usage {.code = 2u32};
                u64 batch_lines = std.convert::parse_u64(arguments[2], 10u32);
                u64 budget_bytes = std.convert::parse_u64(arguments[3], 10u32);
                throw (batch_lines == 0u64) Usage {.code = 2u32};
                await run(batch_lines as usize, budget_bytes as usize);
            }
        } catch (Usage failure) {
            failure as void;
            await std.console::eprint(std.string::from_str(usage_text));
            return 64;
        } catch (std.convert::parse_error failure) {
            failure as void;
            await std.console::eprint(std.string::from_str(usage_text));
            return 64;
        } catch (example.ingest.batch::no_arena failure) {
            failure as void;
            return 70;
        } catch (std.arena::arena_error failure) {
            failure as void;
            return 70;
        } catch (std.error::fault failure) {
            failure as void;
            return 71;
        }
    }
    return 0;
}
