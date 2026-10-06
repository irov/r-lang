module example.workspace.main;
import std.console;
import std.text;
import example.workspace.report;
import example.workspace.files;
import example.calculator.common::{Usage};

struct CommandStorage1 { std.string::string value; };
struct CommandStorage2 { std.string::string value; };

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string output; i32 status; };

enum Command { init, inspect, first, stat, record, read, tail, publish, rename, remove, mkdir, rmdir };

async i32 main(const str[] arguments) {
    try {
        CommandResponse response = {.output = std.string::create(), .status = 0};
        
        try {
            if (len(arguments) == 1usize) {
                std.string::string help = std.string::from_str("workspace init|inspect|first PATH\nworkspace stat|read|remove|mkdir|rmdir ROOT NAME\nworkspace record ROOT NAME TEXT\nworkspace publish|rename ROOT FROM TO\nworkspace tail PATH BYTE_COUNT\n");
                await std.console::print(move help);
                return 0;
            }
            throw (len(arguments) < 3usize) Usage { .message = "command and root path required" };
            o<Command> parsed = core::enum_from_name::<Command>(arguments[1]);
            Command command = Command::inspect;
            switch (parsed) {
            case variant o::some(value): command = *value; break;
            case variant o::none: throw Usage { .message = "unknown workspace command" };
            }
            usize required = 4usize;
            if (command == Command::init || command == Command::inspect || command == Command::first) { required = 3usize; }
            if (command == Command::record || command == Command::publish || command == Command::rename) { required = 5usize; }
            throw (len(arguments) != required) Usage { .message = "wrong argument count" };
            CommandStorage1 state_name_text = {.value = std.string::create()};
            CommandStorage2 state_extra_text = {.value = std.string::create()};
            if (len(arguments) > 3usize) { state_name_text.value = std.string::from_str(arguments[3]); }
            if (len(arguments) > 4usize) { state_extra_text.value = std.string::from_str(arguments[4]); }
            std.fs::path root_path = std.fs::path_from_utf8(arguments[2]);
            if (command == Command::init) {
                await root_path.create_directory(false);
                response.output = std.string::from_str("created\n");
            } else {
                if (command == Command::inspect) {
                    std.fs::metadata info = await root_path.metadata();
                    response.output = example.workspace.report::describe(info);
                } else {
                    if (command == Command::tail) {
                        u32 count = std.convert::parse_u32(state_name_text.value, 10u32);
                        throw (count > 1048576u32) Usage { .message = "tail is limited to 1 MiB" };
                        std.fs::path selected_path = root_path.clone();
                        std.string::string summary = await example.workspace.files::tail(move selected_path, count);
                        response.output = move summary;
                    } else {
                        std.fs::directory root = await root_path.open_directory();
                        if (command == Command::first) {
                            std.fs::directory_iter iterator = await root.iterate();
                            std.string::string summary = await example.workspace.report::first(move iterator);
                            response.output = move summary;
                        } else {
                            std.fs::path name = std.fs::path_from_utf8(state_name_text.value);
                            throw (name.is_absolute() == true) Usage { .message = "name must be relative to root" };
                            switch (command) {
                            case Command::stat:
                                std.fs::metadata info = await root.metadata_beneath(&name);
                                response.output = example.workspace.report::describe(info); break;
                            case Command::record:
                                std.fs::open_file_options options = example.workspace.files::settings(std.fs::access::read_write, std.fs::create_mode::create_new);
                                std.fs::file file = await root.open_file_beneath(&name, options);
                                std.string::string message = std.string::from_str(state_extra_text.value);
                                std.string::string summary = await example.workspace.files::record(move file, move message);
                                response.output = move summary; break;
                            case Command::read:
                                bytes data = await root.read_file_beneath(&name, 1048576usize);
                                usize length = len(data);
                                u32 checksum = std.hash::crc32(data);
                                response.output = f"bytes={length} crc32={checksum}\n"; break;
                            case Command::publish:
                                bytes data = await root.read_file_beneath(&name, 1048576usize);
                                str leaf = state_extra_text.value;
                                throw (len(leaf) == 0usize || std.text::contains(leaf, "/") == true || std.text::contains(leaf, "\\") == true || std.bytes::equal(leaf, ".") == true || std.bytes::equal(leaf, "..") == true) Usage { .message = "publication destination must be a file name" };
                                std.fs::path target_name = std.fs::path_from_utf8(leaf);
                                throw (target_name.is_absolute() == true) Usage { .message = "destination must be relative" };
                                std.fs::path target = root_path.join(&target_name);
                                await example.workspace.files::publish(move target, move data);
                                response.output = std.string::from_str("published\n"); break;
                            case Command::rename:
                                std.fs::path target = std.fs::path_from_utf8(state_extra_text.value);
                                await root.rename_beneath(&name, &root, &target);
                                response.output = std.string::from_str("renamed\n"); break;
                            case Command::remove:
                                await root.remove_file_beneath(&name);
                                response.output = std.string::from_str("removed\n"); break;
                            case Command::mkdir:
                                await root.create_directory_beneath(&name, false);
                                std.fs::directory child = await root.open_directory_beneath(&name);
                                std.fs::directory_iter iterator = await child.iterate();
                                std.string::string summary = await example.workspace.report::first(move iterator);
                                response.output = move summary;
                                await (move child).close(); break;
                            case Command::rmdir:
                                await root.remove_directory_beneath(&name);
                                response.output = std.string::from_str("removed\n"); break;
                            default: throw Usage { .message = "unexpected command" };
                            }
                        }
                        await (move root).close();
                    }
                }
            }
        } catch (Usage failure) { response.output = f"{failure.message}\n"; response.status = 64; }
        catch (std.fs::fs_error failure) {
            std.error::error error = failure.as_error();
            std.string::string description = error.diagnostic();
            response.output = f"{description}\n"; response.status = 74;
        }
        await std.console::print(core::replace(&response.output, std.string::create()));
        return response.status;
    } catch (std.fs::path_error failure) { return 65; }
    catch (std.convert::parse_error failure) { return 65; }
}
