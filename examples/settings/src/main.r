module example.settings.main;
import std.console;
import example.settings.rules;

/* settings KEY VALUE...: checks port, workers, mode and retries and prints the settings. A
   failure of one argument is reported with its position and exits with 65; any standard failure
   is caught once as std.error::fault and reported by its portable name with 70. */
async i32 main(const str[] arguments) {
    try {
        if (len(arguments) == 1usize) {
            await std.console::print(std.string::from_str("settings KEY VALUE...\n"));
            return 0;
        }
        example.settings.rules::Settings chosen = example.settings.rules::read(arguments);
        constexpr str mode = core::enum_name(chosen.mode);
        std.string::string text = f"port={chosen.port} workers={chosen.workers} mode={mode} retries={chosen.retries}\n";
        await std.console::print(move text);
        return 0;
    } catch (example.settings.rules::setting_error failure) {
        std.string::string message = example.settings.rules::describe(move failure);
        await std.console::print(move message);
        return 65;
    } catch (std.error::fault failure) {
        std.error::error portable = std.error::from_fault(failure);
        constexpr str name = std.error::name(portable);
        std.string::string message = f"standard failure: {name}\n";
        await std.console::print(move message);
        return 70;
    }
}
