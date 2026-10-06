module example.ingest.batch;
import std.alloc;
import std.arena;
import std.pool;
import example.ingest.parse;

/* A line that is not a message, and why. */
struct rejection { usize line; example.ingest.parse::problem reason; usize position; };

/* The messages of a batch and its rejected lines. */
struct parsed {
    array<example.ingest.parse::message> messages;
    array<rejection> rejected;
};

/* What a batch left: its counts, its first message read back from the arena, the bytes of the
   arena, the usage of its budget once the parse held all its results, and the refusal of the
   budget if the parse ran out of it. */
struct report {
    usize first_line;
    usize lines;
    usize messages;
    usize fields;
    array<rejection> rejected;
    std.string::string first;
    usize stored;
    usize reserved;
    usize budget_bytes;
    o<std.alloc::usage> charged;
    o<std.alloc::alloc_error> refused;
};

/* A worker found every arena of the pool lent out. */
error no_arena { usize first_line; };

protected void push_message(array<example.ingest.parse::message>* messages,
                            example.ingest.parse::message item) throws std.alloc::alloc_error {
    try {
        messages->push(move item);
    } catch (std.array::push_error<example.ingest.parse::message> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

protected void push_rejection(array<rejection>* rejected, rejection item) throws std.alloc::alloc_error {
    try {
        rejected->push(item);
    } catch (std.array::push_error<rejection> failure) {
        switch (move failure) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

/* Parses the lines into the arena; done counts the messages parsed so far, so that it survives
   a refusal of the budget. */
parsed parse_lines(std.arena::arena* place, const array<std.string::string>* lines, usize first_line,
                   usize* done) throws std.arena::arena_error, std.alloc::alloc_error {
    array<example.ingest.parse::message> messages = [];
    array<rejection> rejected = [];
    for (usize index = 0usize; index < len(*lines); index += 1usize) {
        try {
            example.ingest.parse::message item = example.ingest.parse::parse(place, (*lines)[index]);
            push_message(&messages, move item);
            *done += 1usize;
        } catch (example.ingest.parse::parse_error failure) {
            push_rejection(&rejected, rejection {.line = first_line + index, .reason = failure.reason,
                                                 .position = failure.position});
        }
    }
    return parsed {.messages = move messages, .rejected = move rejected};
}

/* The first message as "key=value" words, read back from the arena. */
protected std.string::string first_of(const std.arena::arena* place, const parsed* outcome)
    throws std.arena::arena_error, std.alloc::alloc_error {
    std.string::string text = std.string::create();
    if (len(outcome->messages) == 0usize) { return move text; }
    const example.ingest.parse::message* item = &outcome->messages[0usize];
    for (usize index = 0usize; index < len(item->fields); index += 1usize) {
        example.ingest.parse::field part = item->fields[index];
        if (index > 0usize) { text.append(" "); }
        text.append(place->text(part.key));
        text.append("=");
        text.append(place->text(part.value));
    }
    return move text;
}

protected usize fields_of(const parsed* outcome) {
    usize total = 0usize;
    for (usize index = 0usize; index < len(outcome->messages); index += 1usize) {
        total += len(outcome->messages[index].fields);
    }
    return total;
}

protected report summary(const std.arena::arena* place, parsed outcome, usize first_line, usize lines,
                         usize budget_bytes) throws std.arena::arena_error, std.alloc::alloc_error {
    array<rejection> none = [];
    array<rejection> rejected = core::replace(&outcome.rejected, move none);
    return report {
        .first_line = first_line,
        .lines = lines,
        .messages = len(outcome.messages),
        .fields = fields_of(&outcome),
        .rejected = move rejected,
        .first = first_of(place, &outcome),
        .stored = place->used(),
        .reserved = place->reserved(),
        .budget_bytes = budget_bytes,
        .charged = o::none,
        .refused = o::none
    };
}

protected report refusal(usize first_line, usize lines, usize budget_bytes, usize done,
                         o<std.alloc::alloc_error> failure) {
    array<rejection> none = [];
    return report {
        .first_line = first_line,
        .lines = lines,
        .messages = done,
        .fields = 0usize,
        .rejected = move none,
        .first = std.string::create(),
        .stored = 0usize,
        .reserved = 0usize,
        .budget_bytes = budget_bytes,
        .charged = o::none,
        .refused = failure
    };
}

/* The report of a parse that kept its results, or of one that its budget refused. */
protected report finish(const std.arena::arena* place, o<parsed> kept, usize first_line, usize lines,
                        usize budget_bytes, usize done, o<std.alloc::alloc_error> refused,
                        o<std.alloc::usage> charged) throws std.arena::arena_error, std.alloc::alloc_error {
    switch (move kept) {
    case variant o::some(move outcome):
        report result = summary(place, move outcome, first_line, lines, budget_bytes);
        result.charged = charged;
        return move result;
    case variant o::none: break;
    }
    return refusal(first_line, lines, budget_bytes, done, refused);
}

/* Ingests a batch of lines with an arena lent by the pool. The parse runs in a budget of
   budget_bytes (Core R-STMT-0020): the blocks of the arena and the arrays of the messages are
   charged to it, and an allocation beyond it refuses the batch. The arena is reset before the
   lease puts it back, which returns every byte to the budget. */
async report ingest(std.pool::pool<std.arena::arena> arenas, array<std.string::string> lines,
                    usize first_line, usize budget_bytes)
    throws no_arena, std.arena::arena_error, std.alloc::alloc_error {
    o<std.pool::lease<std.arena::arena>> taken = arenas.acquire();
    switch (move taken) {
    case variant o::some(move lease):
        usize done = 0usize;
        o<parsed> kept = o::none;
        o<std.alloc::usage> charged = o::none;
        o<std.alloc::alloc_error> refused = o::none;
        budget (std.alloc::limits {.bytes = o::some(budget_bytes)}) {
            try {
                parsed outcome = parse_lines(lease.get_mut(), &lines, first_line, &done);
                core::replace(&charged, std.alloc::budget_usage()) as void;
                core::replace(&kept, o::some(move outcome)) as void;
            } catch (std.alloc::alloc_error failure) {
                refused = o::some(failure);
            }
        }
        report result = finish(lease.get(), move kept, first_line, len(lines), budget_bytes, done, refused, charged);
        (lease.get_mut())->reset();
        drop lease;
        return move result;
    case variant o::none: break;
    }
    throw no_arena {.first_line = first_line};
}
