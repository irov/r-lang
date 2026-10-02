module example.statistics.segments;

/* The maximal non-decreasing runs of the readings in input order. Each run is a view of the
   readings: the array copies no values and carries their origin, so it cannot outlive them
   (Core R-BORROW-0018). */
array<const i32[]> runs(const i32[] readings) throws std.array::push_error<const i32[]> {
    array<const i32[]> found = std.array::create::<const i32[]>();
    usize start = 0usize;
    for (usize index = 1usize; index < len(readings); index += 1usize) {
        if (readings[index] < readings[index - 1usize]) {
            std.array::push(&found, readings[start..index]);
            start = index;
        }
    }
    if (start < len(readings)) {
        std.array::push(&found, readings[start..len(readings)]);
    }
    return move found;
}

/* One row per run with its readings, then the number of runs and the one-based position of
   the first longest run. */
std.string::string describe_runs(const i32[] readings)
    throws std.array::push_error<const i32[]>, std.alloc::alloc_error {
    array<const i32[]> found = runs(readings);
    std.string::string output = std.string::create();
    usize number = 0usize;
    usize longest = 0usize;
    usize longest_length = 0usize;
    for (const (const i32[])* run in &found) {
        number += 1usize;
        output.append("run");
        for (usize index = 0usize; index < len(*run); index += 1usize) {
            i32 value = (*run)[index];
            std.string::string cell = f" {value}";
            output.append(cell.as_str());
        }
        output.append("\n");
        if (len(*run) > longest_length) {
            longest = number;
            longest_length = len(*run);
        }
    }
    if (number == 0usize) {
        output.append("runs=0 longest=none\n");
    } else {
        std.string::string total = f"runs={number} longest={longest}\n";
        output.append(total.as_str());
    }
    return move output;
}

/* A search over part of the readings: the part, the value sought and the position of the
   part's first reading among all readings. */
struct Query { const i32[] readings; i32 wanted; usize offset; };

/* The first position of a value among all readings and its occurrences in one part. */
struct Found { o<usize> first; usize count; };

/* Searches one part. The query holds a view of the readings, which an ordinary task may not
   take (Core R-BORROW-0024); a scoped task may, because its group keeps the readings alive
   until the task ends (Core R-STMT-0017). */
@scoped
async Found search(Query query) {
    Found found = {.first = o::none, .count = 0usize};
    for (usize index = 0usize; index < len(query.readings); index += 1usize) {
        if (query.readings[index] == query.wanted) {
            if (found.count == 0usize) {
                found.first = o::some(query.offset + index);
            }
            found.count += 1usize;
        }
    }
    return found;
}

/* Formats where the value occurs: its first position among all readings, which lies in the
   front half when the value occurs there, and its number of occurrences in both halves. */
std.string::string report(Found front, Found back) throws std.alloc::alloc_error {
    usize count = front.count + back.count;
    switch (front.first) {
    case variant o::some(position):
        usize first = *position;
        return f"first={first} count={count}\n";
    case variant o::none:
        break;
    }
    switch (back.first) {
    case variant o::some(position):
        usize first = *position;
        return f"first={first} count={count}\n";
    case variant o::none:
        break;
    }
    return f"first=none count={count}\n";
}

/* Searches both halves of the readings concurrently. Each task borrows its half through a
   query; the group keeps the readings alive until both tasks end. */
async std.string::string locate(array<i32> readings, i32 wanted)
    throws std.alloc::alloc_error, std.async::start_error {
    const i32[] view = readings.as_slice();
    usize middle = len(view) / 2usize;
    Query front = {.readings = view[0usize..middle], .wanted = wanted, .offset = 0usize};
    Query back = {.readings = view[middle..len(view)], .wanted = wanted, .offset = middle};
    task_scope(2) halves {
        auto front_task = search(front);
        auto back_task = search(back);
        Found front_found = await move front_task;
        Found back_found = await move back_task;
        return report(front_found, back_found);
    }
}
