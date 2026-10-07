module example.reflection.main;

import example.reflection.model::{Level, Command, Settings, level_from_text, next_level,
                                  enabled, command_name, type_of, cardinality, total_score,
                                  clear_all, first_scored};

/* Byte-for-byte comparison of two strings; `str` converts to a byte slice implicitly. */
bool same(str left, str right) {
    const u8[] a = left;
    const u8[] b = right;
    usize index = 0;
    if (len(a) != len(b)) { return false; }
    while (index < len(a)) {
        if (a[index] != b[index]) { return false; }
        index += 1;
    }
    return true;
}

i32 main() {
    /* Enumeration constants: count, bounds and the variants in declaration order. */
    Level[5] levels = core::enum_variants::<Level>();
    if (core::enum_count::<Level>() != 5) { return 1; }
    if (core::enum_min::<Level>() != Level::trace || core::enum_max::<Level>() != Level::error) {
        return 2;
    }
    if (levels[0] != Level::trace || levels[2] != Level::info || levels[4] != Level::error) {
        return 3;
    }

    /* The name and the ordinal of a value are selected at run time. */
    Level warn = Level::warn;
    constexpr str warn_name = core::enum_name(warn);
    bool ok = same(warn_name, "warn");
    if (ok == false) { return 4; }
    if (core::enum_ordinal(warn) != 3) { return 5; }

    /* From text to a variant and along the declaration order. */
    Level parsed = level_from_text("debug");
    if (parsed != Level::debug) { return 6; }
    Level unknown = level_from_text("verbose");
    if (unknown != Level::trace) { return 6; }
    Level after_warn = next_level(Level::warn);
    if (after_warn != Level::error) { return 7; }
    Level after_error = next_level(Level::error);
    if (after_error != Level::error) { return 7; }

    /* Tagged unions: the number of variants and the active one. */
    Command command = Command::say { .level = Level::info, .code = 7 };
    constexpr str active = command_name(&command);
    bool ok_2 = same(active, "say");
    if (ok_2 == false) { return 8; }
    if (core::variant_count::<Command>() != 3) { return 9; }

    /* Structs: the fields by declaration index; a reflection constant is call-free, so it
       is passed directly as an argument. */
    if (core::field_count::<Settings>() != 3) { return 10; }
    bool ok_3 = same(core::field_name::<Settings>(0), "threshold");
    if (ok_3 == false) { return 11; }
    bool ok_4 = same(core::field_name::<Settings>(2), "verbose");
    if (ok_4 == false) { return 12; }

    /* Canonical type names, also for derived types and inside generic instantiations. */
    bool ok_5 = same(core::type_name::<Settings>(), "example.reflection.model::Settings");
    if (ok_5 == false) { return 13; }
    bool ok_6 = same(core::type_name::<array<Level>>(), "array<example.reflection.model::Level>");
    if (ok_6 == false) { return 14; }
    constexpr str spelled = type_of(warn);
    bool ok_7 = same(spelled, "example.reflection.model::Level");
    if (ok_7 == false) { return 15; }
    constexpr str scalar = type_of(42u32);
    bool ok_8 = same(scalar, "u32");
    if (ok_8 == false) { return 16; }
    usize count = cardinality(Level::info);
    if (count != 5) { return 17; }

    /* The translation target and the selected library profile. */
    constexpr str target = core::target_name();
    constexpr str profile = core::profile_name();
    bool ok_9 = same(target, "arm64-apple-darwin");
    if (ok_9 == false) { return 18; }
    bool ok_10 = same(profile, "hosted-native-async");
    if (ok_10 == false) { return 19; }

    /* Ordinals compare levels without exposing their discriminants. */
    Settings settings = Settings { .threshold = Level::info, .retries = 3, .verbose = false };
    bool quiet = enabled(&settings, Level::debug);
    if (quiet == true) { return 20; }
    bool loud = enabled(&settings, Level::warn);
    if (loud == false) { return 20; }

    /* Fields by a constant index: a translation-time loop visits every field of a record whose
       fields implement Scored, struct or tuple alike. */
    if (total_score(&settings) != 5u32) { return 21; }
    bool ok_11 = same(first_scored(&settings), "threshold");
    if (ok_11 == false) { return 22; }
    (u32, bool) pair = (4u32, true);
    if (total_score(&pair) != 5u32) { return 23; }
    clear_all(&settings);
    if (total_score(&settings) != 0u32 || settings.threshold != Level::trace) { return 24; }
    bool ok_12 = same(first_scored(&settings), "");
    if (ok_12 == false) { return 25; }

    /* `&place` borrows only the field, so two fields of one place are borrowed at once. */
    u32* retries = core::field_mut(&settings, 1usize);
    const bool* verbose = core::field(&settings, 2usize);
    *retries = 9u32;
    if (*verbose == true || settings.retries != 9u32) { return 26; }
    return 0;
}
