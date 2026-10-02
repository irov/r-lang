module example.tournament.season;
import std.cmp;
import std.slice;
import std.set;
import example.calculator.common::{Usage};

// Registration compares players by number through a derived std.cmp::Equal.
@derive(equal)
struct Player { u32 number; };

/* A pairing of two players, the lower number first. Derived implementations make it a set
   element (`key`) and let pairings be compared (`ordered`). */
@derive(equal, ordered, key)
struct Pairing { u32 low; u32 high; };

trait Draw {
    type Entry;
    // The largest number of entries one draw produces; an implementation may lower it.
    const u32 CAPACITY = 32u32;
    o<Self::Entry> pull(Self* this);
};
struct Seeds { u32 next; u32 end; };
impl Draw for Seeds {
    type Entry = Player;
    o<Player> pull(Seeds* this) {
        if (this->next >= this->end) { return o::none; }
        u32 number = this->next;
        this->next += 1u32;
        return o::some(Player { .number = number });
    }
};
@generic<S: Draw>
o<S::Entry> next_seed(S* source) {
    o<S::Entry> result = source->pull();
    return move result;
}
@generic<S: Draw>
bool fits(u32 players) { return players <= S::CAPACITY; }

@generic<F: fn(i32) -> i32>
i32 adjusted(const F* policy, i32 score) {
    i32 result = policy(score);
    return result;
}
i64 total(i32... scores) {
    i64 value = 0i64;
    for (i32 score in &scores) { value += score as i64; }
    return value;
}

std.string::string schedule(u32 players) throws Usage, std.alloc::alloc_error,
    std.array::push_error<u32>, std.dict::insert_error<u32, u32> {
    if (players < 2u32 || players > 32u32) {
        throw (players < 2u32) Usage { .message = "a tournament needs at least two players" }
            else Usage { .message = "a tournament supports at most 32 players" };
    }
    u32 end = players + 1u32;
    array<u32> fixtures = [home * 100u32 + away for (u32 home in 1u32..end)
        for (u32 away in 1u32..end) if (home < away)];
    dict<u32, u32> appearances = {number: 0u32 for (u32 number in 1u32..end)};
    // Award rules are ordinary dictionary values, independent of the match schedule.
    dict<u32, u32> awards = {1u32: 3u32, 2u32: 1u32, 3u32: 0u32};
    std.string::string output = std.string::create();
    for (u32 fixture in &fixtures) {
        u32 home = fixture / 100u32;
        u32 away = fixture % 100u32;
        if (home not in appearances || away not in appearances) {
            throw Usage { .message = "fixture contains an unregistered player" };
        }
        array<u32> opponents = [home, away];
        for (u32 number in &opponents) {
            o<u32*> count = appearances.get_mut(&number);
            switch (move count) {
            case variant o::some(value): **value += 1u32; break;
            case variant o::none: throw Usage { .message = "player is missing" };
            }
        }
        std.string::string line = f"{home}-{away}\n";
        output.append(line.as_str());
    }
    usize matches = len(fixtures);
    usize rules = len(awards);
    std.string::string summary = f"matches={matches} scoring_rules={rules}\n";
    output.append(summary.as_str());
    for (auto entry in &appearances) {
        u32 number = *entry.key;
        u32 count = *entry.value;
        std.string::string line = f"player={number} matches={count}\n";
        output.append(line.as_str());
    }
    return move output;
}

std.string::string draw(u32 players, u32 wanted) throws Usage, std.alloc::alloc_error,
    std.array::push_error<Player> {
    throw (fits::<Seeds>(players) == false) Usage { .message = "draw supports at most 32 players" };
    Seeds seeds = Seeds { .next = 1u32, .end = players + 1u32 };
    array<Player> registered = std.array::create::<Player>();
    registered.reserve(players as usize);
    for (u32 index = 0u32; index < players; index += 1u32) {
        o<Player> seed = next_seed(&seeds);
        switch (seed) {
        case variant o::some(value): registered.push(*value); break;
        case variant o::none: throw Usage { .message = "draw ran out of players" };
        }
    }
    Player query = Player { .number = wanted };
    const Player[] view = registered.as_slice();
    bool found = std.slice::contains(view, &query);
    return f"player={wanted} registered={found}\n";
}

std.string::string ranking(array<i32>* scores, i32 bonus) throws std.alloc::alloc_error {
    own i32* policy_value = new i32(bonus);
    fn i32 award(i32 score) move(policy_value) { return score + *policy_value; }
    std.string::string output = std.string::create();
    usize count = len(*scores);
    usize capacity = scores->capacity();
    {
        i32[] heap = scores->as_slice_mut();
        usize start = count / 2usize;
        while (start > 0usize) { start -= 1usize; std.slice::sift_down(heap, start, count); }
        usize end = count;
        while (end > 0usize) {
            i32 score = heap[0usize];
            i32 points = adjusted(&award, score);
            std.string::string line = f"score={score} adjusted={points}\n";
            output.append(line.as_str());
            end -= 1usize;
            if (end > 0usize) {
                std.slice::swap(heap, 0usize, end);
                std.slice::sift_down(heap, 0usize, end);
            }
        }
    }
    const i32[] sorted = scores->as_slice();
    i64 sum = total(...sorted);
    std.string::string summary = f"count={count} total={sum}\n";
    output.append(summary.as_str());
    // Retire one completed entry explicitly, then reuse the remaining allocation next round.
    if (count > 0usize) { o<i32> retired = scores->remove(0usize); retired as void; }
    scores->clear();
    usize remaining = len(*scores);
    usize retained = scores->capacity();
    bool reused = retained == capacity;
    std.string::string reset = f"remaining={remaining} capacity_retained={reused}\n";
    output.append(reset.as_str());
    return move output;
}

/* Round-robin rounds by the circle method: the first seat stays, the others rotate one place per
   round, and seat pairs meet across the circle. An odd field adds a bye (0). A `first` player
   other than 1 takes the fixed seat after the lineup rotates left to it. */
std.string::string rounds(u32 players, u32 first) throws Usage, std.alloc::alloc_error,
    std.array::push_error<u32>, std.dict::insert_error<Pairing, bool> {
    throw (players < 2u32 || players > 32u32) Usage { .message = "rounds supports 2 to 32 players" };
    throw (first < 1u32 || first > players) Usage { .message = "the first player is not registered" };
    array<u32> seats = std.array::create::<u32>();
    for (u32 number = 1u32; number <= players; number += 1u32) { seats.push(number); }
    if (players % 2u32 == 1u32) { seats.push(0u32); }
    u32[] lineup = seats.as_slice_mut();
    std.slice::rotate_left(lineup, (first - 1u32) as usize);
    usize size = len(seats);
    std.string::string output = std.string::create();
    std.set::set<Pairing> met = std.set::set<Pairing>::create();
    Pairing highest = Pairing { .low = 0u32, .high = 0u32 };
    u32 repeated = 0u32;
    for (usize round = 1usize; round < size; round += 1usize) {
        std.string::string line = f"round={round}";
        for (usize seat = 0usize; seat < size / 2usize; seat += 1usize) {
            u32 home = seats[seat];
            u32 away = seats[size - 1usize - seat];
            // Alternate the fixed seat's home games so no player hosts every round.
            if (seat == 0usize && round % 2usize == 0usize) { core::swap(&home, &away); }
            if (home != 0u32 && away != 0u32) {
                std.string::string pair = f" {home}-{away}";
                line.append(pair.as_str());
                Pairing pairing = Pairing { .low = home, .high = away };
                if (away < home) { pairing = Pairing { .low = away, .high = home }; }
                if (met.insert(pairing) == false) { repeated += 1u32; }
                if (pairing.cmp(&highest) == std.cmp::ordering::greater) { highest = pairing; }
            }
        }
        line.append("\n");
        output.append(line.as_str());
        u32[] circle = seats.as_slice_mut();
        std.slice::rotate_right(circle[1usize..size], 1usize);
    }
    // The circle method pairs every two players exactly once.
    usize pairings = met.count();
    u32 low = highest.low;
    u32 high = highest.high;
    std.string::string summary = f"pairings={pairings} repeated={repeated} highest={low}-{high}\n";
    output.append(summary.as_str());
    return move output;
}

// One row of a league table: an owned name and its points. Rows compare by name and points
// through a derived std.cmp::Equal; the clone hook below stays explicit.
@derive(equal)
struct Standing { std.string::string name; i32 points; };
Standing Standing::clone(const Standing* value) throws std.alloc::alloc_error {
    return Standing { .name = core::clone(&value->name), .points = value->points };
}

// More points first; equal points by name.
std.cmp::ordering by_points(const Standing* left, const Standing* right) {
    if (left->points > right->points) { return std.cmp::ordering::less; }
    if (left->points < right->points) { return std.cmp::ordering::greater; }
    const u8[] a = left->name.as_bytes();
    const u8[] b = right->name.as_bytes();
    i32 order = std.bytes::compare(a, b);
    if (order < 0) { return std.cmp::ordering::less; }
    if (order > 0) { return std.cmp::ordering::greater; }
    return std.cmp::ordering::equal;
}

/* The one-based position of the first item equal to `value`, or 0, for any std.cmp::Equal. */
@generic<T: std.cmp::Equal>
usize position_of(const T[] items, const T* value) {
    usize position = 0usize;
    for (const T* item in &items) {
        position += 1usize;
        if (item->eq(value) == true) { return position; }
    }
    return 0usize;
}

/* The league table of NAME:POINTS entries, best first, each with its entry position. The
   registration order is kept as a clone of the rows before they are sorted in place. */
std.string::string table(const str[] entries) throws Usage, std.alloc::alloc_error,
    std.convert::parse_error, core::utf8_error, std.array::push_error<Standing> {
    array<Standing> rows = std.array::create::<Standing>();
    for (str entry in &entries) {
        const u8[] bytes = entry;
        o<usize> colon = std.bytes::find(bytes, 58u8);
        usize at = 0usize;
        switch (colon) {
        case variant o::some(position): at = *position;
        case variant o::none: throw Usage { .message = "a table entry is NAME:POINTS" };
        }
        throw (at == 0usize) Usage { .message = "a table entry needs a name" };
        // The colon is ASCII, so both parts stay valid UTF-8.
        str name = core::validate_utf8(bytes[0usize..at]);
        str score = core::validate_utf8(bytes[at + 1usize..len(bytes)]);
        i32 points = std.convert::parse_i32(score, 10u32);
        Standing row = Standing { .name = std.string::from_str(name), .points = points };
        rows.push(move row);
    }
    array<Standing> registered = core::clone(&rows);
    usize registrations = len(registered);
    Standing[] view = rows.as_slice_mut();
    auto compare = by_points;
    std.slice::sort_by(view, &compare);
    std.string::string output = std.string::create();
    const Standing[] registration = registered.as_slice();
    usize place = 0usize;
    for (const Standing* row in &rows) {
        place += 1usize;
        usize entered = position_of(registration, row);
        str name = row->name.as_str();
        i32 points = row->points;
        std.string::string line = f"{place}. {name} {points} (entry {entered})\n";
        output.append(line.as_str());
    }
    std.string::string summary = f"entries={registrations}\n";
    output.append(summary.as_str());
    return move output;
}
