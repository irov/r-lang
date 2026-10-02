module example.logbook.main;
import std.console;
import example.logbook.model::{Level};
import example.logbook.model;
import example.logbook.inspect;
import example.calculator.common::{Usage};

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string output; i32 status; };

enum Command { levels, next, filter, schema, event };

async i32 main(const str[] arguments) {
    try {
        CommandResponse response = {.output = std.string::create(), .status = 0};
        
        try {
            usize count = len(arguments);
            if (count == 1usize) {
                std.string::string help = std.string::from_str("logbook levels|schema\nlogbook next LEVEL\nlogbook filter THRESHOLD [LEVEL MESSAGE]...\nlogbook event THRESHOLD JSON\n");
                await std.console::print(move help);
                return 0;
            }
            o<Command> selected = core::enum_from_name::<Command>(arguments[1]);
            Command command = Command::levels;
            switch (selected) {
            case variant o::some(value): command = *value; break;
            case variant o::none: throw Usage { .message = "unknown logbook command" };
            }
            switch (command) {
            case Command::levels:
                throw (count != 2usize) Usage { .message = "levels takes no arguments" };
                response.output = example.logbook.inspect::levels(); break;
            case Command::schema:
                throw (count != 2usize) Usage { .message = "schema takes no arguments" };
                response.output = example.logbook.inspect::schema(); break;
            case Command::next:
                throw (count != 3usize) Usage { .message = "next needs a level" };
                Level level = example.logbook.model::parse(arguments[2]);
                Level following = example.logbook.model::next(level);
                constexpr str name = core::enum_name(following);
                response.output = f"{name}\n"; break;
            case Command::filter:
                throw (count < 3usize || ((count - 3usize) % 2usize) != 0usize) Usage { .message = "filter needs a threshold followed by level/message pairs" };
                Level threshold = example.logbook.model::parse(arguments[2]);
                threshold as void; // Parsing validates the threshold even for an empty list.
                usize accepted = 0usize;
                for (usize index = 3usize; index < count; index += 2usize) {
                    Level level = example.logbook.model::parse(arguments[index]);
                    bool visible = example.logbook.model::enabled(threshold, level);
                    if (visible == true) {
                        constexpr str name = core::enum_name(level);
                        str message = arguments[index + 1usize];
                        std.string::string row = f"[{name}] {message}\n";
                        str text = row.as_str();
                        response.output.append(text);
                        accepted += 1usize;
                    }
                }
                std.string::string row = f"accepted={accepted}\n";
                str text = row.as_str();
                response.output.append(text); break;
            case Command::event:
                throw (count != 4usize) Usage { .message = "event needs a threshold and JSON event" };
                Level threshold = example.logbook.model::parse(arguments[2]);
                threshold as void; // Parsing validates the threshold even for an empty list.
                const u8[] source = arguments[3];
                response.output = example.logbook.inspect::event(source, threshold); break;
            }
        } catch (Usage failure) { response.output = f"{failure.message}\n"; response.status = 64; }
        await std.console::print(core::replace(&response.output, std.string::create()));
        return response.status;
    } catch (std.json::error failure) { return 65; }
}
