module example.reflection.model;

/* A fieldless enumeration with explicit discriminants. Reflection sees both the declaration
   order (ordinals 0..4, enum_variants, enum_at) and the discriminant values (enum_min and
   enum_max are the variants with the least and the greatest discriminant). */
enum Level : u8 {
    trace = 0,
    debug = 10,
    info = 20,
    warn = 30,
    error = 40,
};

/* A tagged union: variant_count counts every variant, variant_name reads the active one. */
enum Command {
    quit,
    jump(i32),
    say { Level level; i32 code; },
};

/* A struct: field_count and field_name enumerate the fields in declaration order. */
struct Settings {
    Level threshold;
    u32 retries;
    bool verbose;
};

/* Parses a level name; an unknown name falls back to the lowest level. `enum_from_name`
   compares the bytes of the name against a table of the declared names, and `enum_min` is a
   reflection constant, so both are valid `return` operands. */
Level level_from_text(str text) {
    o<Level> parsed = core::enum_from_name::<Level>(text);
    switch (parsed) {
    case variant o::some(level):
        return *level;
    case variant o::none:
        return core::enum_min::<Level>();
    }
}

/* The level after `level` in declaration order, saturating at the last one. */
Level next_level(Level level) {
    usize ordinal = core::enum_ordinal(level);
    o<Level> next = core::enum_at::<Level>(ordinal + 1);
    switch (next) {
    case variant o::some(value):
        return *value;
    case variant o::none:
        return level;
    }
}

/* Whether a message at `level` passes the configured threshold: the ordinals order the
   variants without exposing their discriminants. */
bool enabled(const Settings* settings, Level level) {
    usize threshold = core::enum_ordinal(settings->threshold);
    usize candidate = core::enum_ordinal(level);
    return candidate >= threshold;
}

/* The name of the active variant of a command. */
constexpr str command_name(const Command* command) {
    constexpr str name = core::variant_name(command);
    return name;
}

/* Reflection inside a generic body: the form is folded when the parameter is substituted,
   so every instantiation receives the spelling of its own type argument. */
@generic<T: copy>
constexpr str type_of(T value) {
    constexpr str name = core::type_name::<T>();
    value as void;
    return name;
}

/* The number of variants of any fieldless enumeration, as a generic constant. */
@generic<T: copy>
usize cardinality(T value) {
    usize count = core::enum_count::<T>();
    value as void;
    return count;
}

/* A trait that every field of a record implements, so that a generic body may visit the fields
   of any struct or tuple whose fields prove it (`fields(Scored)`). */
trait Scored {
    u32 score(const Self* this);
    void clear(Self* this);
};

impl Scored for Level {
    u32 score(const Self* this) { return core::enum_ordinal(*this) as u32; }
    void clear(Self* this) { *this = core::enum_min::<Level>(); }
};

impl Scored for u32 {
    u32 score(const Self* this) { return *this; }
    void clear(Self* this) { *this = 0u32; }
};

impl Scored for bool {
    u32 score(const Self* this) {
        if (*this == true) { return 1u32; }
        return 0u32;
    }
    void clear(Self* this) { *this = false; }
};

/* A translation-time loop repeats its block once per field, and core::field borrows the field of
   each repetition with the type of that field; the generic body is checked once against the
   methods of Scored and every instantiation unrolls the loop for its own fields. */
@generic<T: fields(Scored)>
u32 total_score(const T* record) {
    u32 total = 0u32;
    for (constexpr usize index in 0usize..core::field_count::<T>()) {
        total += core::field(record, index)->score();
    }
    return total;
}

/* core::field_mut borrows each field exclusively. */
@generic<T: fields(Scored)>
void clear_all(T* record) {
    for (constexpr usize index in 0usize..core::field_count::<T>()) {
        core::field_mut(record, index)->clear();
    }
}

/* The loop constant is a constant of each repetition, so it also names the field. */
@generic<T: fields(Scored)>
constexpr str first_scored(const T* record) {
    for (constexpr usize index in 0usize..core::field_count::<T>()) {
        if (core::field(record, index)->score() != 0u32) {
            return core::field_name::<T>(index);
        }
    }
    return "";
}
