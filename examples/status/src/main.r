module example.status.main;
import std.console;
import example.status.records;

/* One labelled column: the label and the value's text padded to fixed widths. Any value whose
   type satisfies core::Format fits, whether it implements the trait or is formatted by the
   standard rules. */
@generic<T: core::Format>
std.string::string column(str label, const T* value) throws std.alloc::alloc_error {
    return f"{label:10} {value:24}\n";
}

async i32 show_devices() throws std.error::fault {
    example.status.records::Device[3] fleet = example.status.records::fleet();
    std.string::string report = std.string::create();
    for (usize index = 0usize; index < 3usize; index += 1usize) {
        const example.status.records::Device* device = &fleet[index];
        std.string::string line = f"{device}\n";
        report.append(line);
    }
    fleet as void;
    await std.console::print(move report);
    return 0;
}

/* The last line holds values of different types behind one interface. */
async i32 show_readings() throws std.error::fault {
    u32 count = 3u32;
    array<own dyn(core::Format & send)*> summary =
        std.array::create::<own dyn(core::Format & send)*>();
    own u32* total = new u32(count);
    own example.status.records::Celsius* peak =
        new example.status.records::Celsius {.degrees = 21.5};
    own bool* complete = new bool(false);
    try {
        std.array::push(&summary, move total);
        std.array::push(&summary, move peak);
        std.array::push(&summary, move complete);
    } catch (std.array::push_error<own dyn(core::Format & send)*> failed) {
        return 71;
    }
    example.status.records::Reading[3] values = example.status.records::readings();
    std.string::string first = column("room", &values[0]);
    std.string::string second = column("heater", &values[1]);
    std.string::string third = column("attic", &values[2]);
    std.format::builder out = std.format::create();
    std.format::append_str(&out, "summary: ");
    for (usize index = 0usize; index < len(summary); index += 1usize) {
        if (index != 0usize) { std.format::append_str(&out, ", "); }
        summary[index]->format(&out);
    }
    std.format::append_str(&out, "\n");
    std.string::string last = std.format::finish(move out);
    await std.console::print(f"{first}{second}{third}{last}");
    return 0;
}

std.string::string probe_line(str address, str port) throws std.error::fault {
    u16 number = std.convert::parse_u16(port, 10u32);
    std.net::socket_address target = example.status.records::endpoint(address, number);
    return column("endpoint", &target);
}

enum Command { devices, readings, probe };

async i32 main() {
    i32 code = 0;
    try {
        array<std.string::string> arguments = std.env::arguments();
        o<Command> command = o::none;
        if (len(arguments) >= 2usize) {
            command = core::enum_from_name::<Command>(arguments[1]);
        }
        switch (command) {
        case variant o::some(chosen):
            switch (*chosen) {
            case Command::devices: {
                if (len(arguments) != 2usize) { code += 64; }
                code += await show_devices();
            }
            case Command::readings: {
                if (len(arguments) != 2usize) { code += 64; }
                code += await show_readings();
            }
            case Command::probe: {
                if (len(arguments) != 4usize) {
                    code += 64;
                    await std.console::print(
                        std.string::from_str("status probe ADDRESS PORT\n"));
                } else {
                    std.string::string line =
                        probe_line(arguments[2], arguments[3]);
                    await std.console::print(move line);
                }
            }
            }
        case variant o::none:
            // Help without arguments, a usage error otherwise.
            if (len(arguments) > 1usize) { code += 64; }
            await std.console::print(
                std.string::from_str("status devices|readings|probe ADDRESS PORT\n"));
        }
        drop arguments;
    } catch (std.error::fault failure) {
        auto name = std.error::name(std.error::from_fault(failure));
        std.string::string line = std.string::from_str("status failed: ");
        line.append(name);
        line.append("\n");
        await std.console::print(move line);
        code += 70;
    }
    return code;
}
