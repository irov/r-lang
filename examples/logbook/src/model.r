module example.logbook.model;
import example.calculator.common::{Usage};

enum Level : u8 { trace = 0, debug = 10, info = 20, warn = 30, error = 40 };
struct Settings { Level threshold; u32 retries; bool verbose; };
enum Event {
    Quit,
    Threshold(Level),
    Message { Level level; std.string::string text; },
};

Level parse(str text) throws Usage {
    o<Level> parsed = core::enum_from_name::<Level>(text);
    switch (parsed) {
    case variant o::some(value): return *value;
    case variant o::none: throw Usage { .message = "unknown log level" };
    }
}

Level next(Level value) {
    usize ordinal = core::enum_ordinal(value);
    o<Level> following = core::enum_at::<Level>(ordinal + 1usize);
    switch (following) {
    case variant o::some(result): return *result;
    case variant o::none: return value;
    }
}

bool enabled(Level threshold, Level value) {
    usize least = core::enum_ordinal(threshold);
    usize candidate = core::enum_ordinal(value);
    return candidate >= least;
}
