module example.logbook.inspect;
import example.logbook.model::{Level, Settings, Event};
import example.logbook.model;

std.string::string levels() throws std.alloc::alloc_error {
    std.string::string output = std.string::create();
    Level[5] variants = core::enum_variants::<Level>();
    for (const Level* value in &variants) {
        usize ordinal = core::enum_ordinal(*value);
        constexpr str name = core::enum_name(*value);
        std.string::string row = f"{ordinal} {name}\n";
        str text = row;
        output.append(text);
    }
    usize count = core::enum_count::<Level>();
    constexpr str least = core::enum_name(core::enum_min::<Level>());
    constexpr str greatest = core::enum_name(core::enum_max::<Level>());
    std.string::string row = f"levels={count} least={least} greatest={greatest}\n";
    str text = row;
    output.append(text);
    return move output;
}

std.string::string schema() throws std.alloc::alloc_error {
    constexpr str target = core::target_name();
    constexpr str profile = core::profile_name();
    constexpr str type = core::type_name::<Settings>();
    usize fields = core::field_count::<Settings>();
    constexpr str threshold = core::field_name::<Settings>(0usize);
    constexpr str retries = core::field_name::<Settings>(1usize);
    constexpr str verbose = core::field_name::<Settings>(2usize);
    usize variants = core::variant_count::<Event>();
    return f"target={target}\nprofile={profile}\ntype={type}\nfields={fields}: {threshold}, {retries}, {verbose}\nevents={variants}\n";
}

std.string::string event(const u8[] source, Level threshold) throws std.json::error, std.alloc::alloc_error {
    Event incoming = std.json::unmarshal(source);
    constexpr str kind = core::variant_name(&incoming);
    switch (move incoming) {
    case variant Event::Quit: return f"{kind}: stopped\n";
    case variant Event::Threshold(move value):
        constexpr str level = core::enum_name(value);
        return f"{kind}: threshold={level}\n";
    case variant Event::Message(move value):
        bool accepted = example.logbook.model::enabled(threshold, value.level);
        if (accepted == false) { return f"{kind}: filtered\n"; }
        constexpr str level = core::enum_name(value.level);
        return f"{kind}: [{level}] {value.text}\n";
    }
}
