module example.dispatch.priority;
import std.heap;
import std.cmp;

struct Job { usize id; i32 urgency; };

// Higher urgency wins; equal urgency keeps input order.
impl std.cmp::Ordered for Job {
    std.cmp::ordering cmp(const Job* this, const Job* other) {
        if (this->urgency < other->urgency) { return std.cmp::ordering::less; }
        if (this->urgency > other->urgency) { return std.cmp::ordering::greater; }
        if (this->id > other->id) { return std.cmp::ordering::less; }
        if (this->id < other->id) { return std.cmp::ordering::greater; }
        return std.cmp::ordering::equal;
    }
};

std.string::string assign(const i32[] urgencies, usize limit)
    throws std.array::push_error<Job>, std.alloc::alloc_error {
    std.heap::heap<Job> pending = std.heap::heap<Job>::create();
    i32 low = 0;
    i32 high = 100;
    i32 minimum = 100;
    i32 maximum = 0;
    usize id = 0usize;
    for (const i32* urgency in &urgencies) {
        const i32* bounded = std.cmp::clamp(urgency, &low, &high);
        i32 normalized = *bounded;
        const i32* min_value = std.cmp::min(&minimum, &normalized);
        minimum = *min_value;
        const i32* max_value = std.cmp::max(&maximum, &normalized);
        maximum = *max_value;
        Job job = { .id = id, .urgency = normalized };
        pending.push(job);
        id += 1usize;
    }
    usize initial = pending.count();
    std.string::string output = f"jobs={initial}\n";
    if (initial > 0usize) {
        bool varied = std.cmp::is_less(&minimum, &maximum);
        bool uniform = std.cmp::is_equal(&minimum, &maximum);
        std.string::string row = f"min={minimum} max={maximum} varied={varied} uniform={uniform}\n";
        str view = row.as_str();
        output.append(view);
    }
    o<const Job*> next = pending.peek();
    switch (next) {
    case variant o::some(pointer):
        const Job* job = *pointer;
        std.string::string row = f"next={job->id}:{job->urgency}\n";
        str view = row.as_str();
        output.append(view); break;
    case variant o::none: break;
    }
    usize completed = 0usize;
    output.append("assigned:");
    while (completed < limit) {
        o<Job> work = pending.pop();
        switch (work) {
        case variant o::some(job):
            std.string::string row = f" {job->id}:{job->urgency}";
            str view = row.as_str();
            output.append(view);
            completed += 1usize; break;
        case variant o::none: completed = limit; break;
        }
    }
    usize remaining = pending.count();
    bool empty = pending.is_empty();
    std.string::string footer = f"\nremaining={remaining} empty={empty}\n";
    str footer_view = footer.as_str();
    output.append(footer_view);
    return move output;
}
