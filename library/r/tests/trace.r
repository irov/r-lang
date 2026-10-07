module tests.std.trace;
import std.test;
import std.text;
import std.trace;

// The tests of std.trace (Library R-SLIB-TRACE-0001..0003): spans with parents, attributes,
// task identifiers and durations, the capacity of a tracer and its dropped count, spans finished
// by several tasks, and records as JSON. Run in test mode (Core R-FUNC-0025).

@test
async void records_spans_with_parents() throws std.test::failure, std.error::fault {
    std.trace::tracer tracer = std.trace::tracer::create(8usize);
    std.trace::span outer = tracer.start("request");
    outer.attribute("route", "/items");
    std.trace::span inner = outer.child("query \"items\"");
    inner.attribute("rows", "3\n");
    std.trace::record finished_inner = (move inner).finish();
    std.trace::record finished_outer = (move outer).finish();
    switch (finished_inner.parent) {
    case variant o::some(parent): std.test::equal(*parent, finished_outer.id);
    case variant o::none: std.test::check(false, "the inner span has a parent");
    }
    switch (finished_outer.parent) {
    case variant o::some(parent): std.test::check(false, "the outer span has no parent");
    case variant o::none: break;
    }
    std.test::check(finished_outer.task_id != 0u64, "started by a task");
    std.test::check(std.time::duration_compare(finished_outer.duration, finished_inner.duration) >= 0,
                    "the parent lasts as long as its child");
    array<std.trace::record> kept = tracer.drain();
    std.test::equal(len(kept), 2usize);
    std.test::equal_text(kept[0usize].name, "query \"items\"");
    std.string::string json = kept[0usize].json();
    std.test::check(std.text::starts_with(json, "{\"id\":2,\"parent\":1,\"task\":"), "id and parent");
    std.test::check(std.text::contains(json, "\"name\":\"query \\\"items\\\"\""), "an escaped name");
    std.test::check(std.text::contains(json, "\"attributes\":{\"rows\":\"3\\u000a\"}}"), "attributes");
    array<std.trace::record> empty = tracer.drain();
    std.test::equal(len(empty), 0usize);
}

@test
async void drops_the_oldest_at_capacity() throws std.test::failure, std.error::fault {
    std.trace::tracer tracer = std.trace::tracer::create(2usize);
    for (u32 index = 0u32; index < 5u32; index += 1u32) {
        std.trace::span step = tracer.start("step");
        std.trace::record done = (move step).finish();
        drop done;
    }
    std.test::equal(tracer.dropped(), 3u64);
    array<std.trace::record> kept = tracer.drain();
    std.test::equal(len(kept), 2usize);
    std.test::equal(kept[0usize].id, 4u64);
    std.test::equal(kept[1usize].id, 5u64);
}

protected async void work(std.trace::tracer tracer, u32 count) throws std.error::fault {
    for (u32 index = 0u32; index < count; index += 1u32) {
        std.trace::span step = tracer.start("work");
        step.attribute("index", "x");
        std.trace::record done = (move step).finish();
        drop done;
    }
}

@test
async void collects_from_several_tasks() throws std.test::failure, std.error::fault {
    std.trace::tracer tracer = std.trace::tracer::create(1000usize);
    task_scope(3) workers {
        auto a = work(tracer.share(), 100u32);
        auto b = work(tracer.share(), 100u32);
        auto c = work(tracer.share(), 100u32);
        await move a;
        await move b;
        await move c;
    }
    array<std.trace::record> kept = tracer.drain();
    std.test::equal(len(kept), 300usize);
    std.test::equal(tracer.dropped(), 0u64);
}
