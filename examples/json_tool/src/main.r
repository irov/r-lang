module example.json_tool.main;
import std.console;
import example.json_tool.tree;
import example.json_tool.config;
import example.json_tool.stream;
import example.calculator.common::{Usage};

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string output; i32 status; };

enum Command { pretty, compact, keys, get, index, set, append, take, shift, stats, number, names, config, strict, snapshot, reload, fragments, first };

async i32 main(const str[] arguments) {
    try {
        CommandResponse response = {.output = std.string::create(), .status = 0};
        
        try {
            if (len(arguments) == 1usize) {
                std.string::string help = std.string::from_str("json_tool compact|keys|stats|number|config|strict|snapshot JSON\njson_tool pretty JSON INDENT\njson_tool get|take JSON KEY\njson_tool set JSON KEY VALUE\njson_tool append JSON VALUE\njson_tool index|shift JSON INDEX\njson_tool names LEFT RIGHT\njson_tool reload BASE UPDATE\njson_tool fragments ARRAY CHUNK_SIZE\njson_tool first < input\n");
                await std.console::print(move help);
                return 0;
            }
            o<Command> parsed = core::enum_from_name::<Command>(arguments[1]);
            Command command = Command::compact;
            switch (parsed) {
            case variant o::some(value): command = *value; break;
            case variant o::none: throw Usage { .message = "unknown JSON command" };
            }
            usize required = 3usize;
            if (command == Command::first) { required = 2usize; }
            if (command == Command::pretty || command == Command::get || command == Command::index || command == Command::take || command == Command::shift || command == Command::append || command == Command::names || command == Command::reload || command == Command::fragments) { required = 4usize; }
            if (command == Command::set) { required = 5usize; }
            throw (len(arguments) != required) Usage { .message = "wrong argument count" };
            switch (command) {
            case Command::first:
                std.string::string report = await example.json_tool.stream::first();
                response.output = move report; break;
            case Command::config: response.output = example.json_tool.config::normalize(arguments[2], false); break;
            case Command::strict: response.output = example.json_tool.config::normalize(arguments[2], true); break;
            case Command::snapshot: response.output = example.json_tool.config::snapshot(arguments[2]); break;
            case Command::reload: response.output = example.json_tool.config::reload(arguments[2], arguments[3]); break;
            case Command::number: response.output = example.json_tool.tree::number(arguments[2]); break;
            case Command::names:
                bool same = std.json::name_equal(arguments[2], arguments[3], true);
                bool exact = std.json::name_equal(arguments[2], arguments[3], false);
                response.output = f"exact={exact} folded={same}"; break;
            case Command::fragments:
                usize chunk = std.convert::parse_usize(arguments[3], 10u32);
                throw (chunk == 0usize || chunk > 1048576usize) Usage { .message = "chunk must be in 1..1048576" };
                response.output = example.json_tool.stream::fragments(arguments[2], chunk); break;
            case Command::pretty:
                u32 indent = std.convert::parse_u32(arguments[3], 10u32);
                throw (indent > 8u32) Usage { .message = "indent must be in 0..8" };
                std.json::options options = example.json_tool.tree::options(indent, false, std.json::mode::document);
                std.json::value tree = std.json::parse_with_options(arguments[2], options);
                response.output = tree.stringify_with_options(options); break;
            default:
                std.json::value tree = std.json::parse(arguments[2]);
                switch (command) {
                case Command::compact: response.output = tree.stringify(); break;
                case Command::keys: response.output = example.json_tool.tree::keys(&tree); break;
                case Command::get: response.output = example.json_tool.tree::field(&tree, arguments[3]); break;
                case Command::stats: response.output = example.json_tool.tree::statistics(move tree); tree = std.json::null(); tree.kind() as void; break;
                case Command::index:
                    usize index = std.convert::parse_usize(arguments[3], 10u32);
                    response.output = example.json_tool.tree::element(&tree, index); break;
                case Command::set:
                    std.json::value value = std.json::parse(arguments[4]);
                    tree.insert(arguments[3], move value);
                    response.output = tree.stringify(); break;
                case Command::append:
                    std.json::value value = std.json::parse(arguments[3]);
                    tree.append(move value);
                    response.output = tree.stringify(); break;
                case Command::take:
                    std.json::value taken = tree.take_field(arguments[3]);
                    response.output = example.json_tool.tree::extraction(move tree, move taken); tree = std.json::null(); tree.kind() as void; break;
                case Command::shift:
                    usize index = std.convert::parse_usize(arguments[3], 10u32);
                    std.json::value taken = tree.take_index(index);
                    response.output = example.json_tool.tree::extraction(move tree, move taken); tree = std.json::null(); tree.kind() as void; break;
                default: throw Usage { .message = "unexpected command" };
                }
                break;
            }
        } catch (Usage failure) { response.output = f"{failure.message}"; response.status = 64; }
        catch (std.json::error failure) {
            std.json::error_code code = failure.code;
            bool duplicate = code == std.json::error_code::duplicate_key;
            response.output = f"JSON error at byte {failure.offset}, pointer={failure.pointer}, duplicate={duplicate}";
            response.status = 65;
        }
        response.output.append("\n");
        await std.console::print(core::replace(&response.output, std.string::create()));
        return response.status;
    } catch (std.array::push_error<std.json::value> failure) { return 71; }
    catch (std.convert::parse_error failure) { return 65; }
}
