module example.tournament.main;
import std.console;
import example.calculator.common::{Usage};
import example.tournament.season;

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string output; i32 status; };

enum Command { schedule, draw, rank, rounds, table };
async i32 main(const str[] arguments) {
    CommandResponse response = {.output = std.string::create(), .status = 0};

    try {
        usize count = len(arguments);
        if (count == 1usize) {
            response.output = std.string::from_str("tournament schedule PLAYERS\ntournament draw PLAYERS WANTED\ntournament rank BONUS SCORES...\ntournament rounds PLAYERS [FIRST]\ntournament table NAME:POINTS...\n");
        } else {
            o<Command> parsed = core::enum_from_name::<Command>(arguments[1]);
            Command command = Command::schedule;
            switch (parsed) {
            case variant o::some(value): command = *value; break;
            case variant o::none: throw Usage { .message = "unknown tournament command" };
            }
            throw (count < 3usize) Usage { .message = "missing tournament operand" };
            switch (command) {
            case Command::table:
                response.output = example.tournament.season::table(arguments[2usize..count]);
            case Command::rounds:
                throw (count > 4usize) Usage { .message = "rounds needs a player count and an optional first player" };
                u32 players = std.convert::parse_u32(arguments[2], 10u32);
                u32 first = 1u32;
                if (count == 4usize) { first = std.convert::parse_u32(arguments[3], 10u32); }
                response.output = example.tournament.season::rounds(players, first);
            case Command::rank:
                i32 bonus = std.convert::parse_i32(arguments[2], 10u32);
                throw (bonus < -1000000 || bonus > 1000000) Usage { .message = "bonus is limited to one million" };
                array<i32> scores = std.array::create::<i32>();
                scores.reserve(count - 3usize);
                for (usize index = 3usize; index < count; index += 1usize) {
                    i32 value = std.convert::parse_i32(arguments[index], 10u32);
                    throw (value < -1000000 || value > 1000000) Usage { .message = "score is limited to one million" };
                    scores.push(value);
                }
                response.output = example.tournament.season::ranking(&scores, bonus);
            case Command::schedule:
                throw (count != 3usize) Usage { .message = "schedule needs a player count" };
                u32 players = std.convert::parse_u32(arguments[2], 10u32);
                response.output = example.tournament.season::schedule(players);
            case Command::draw:
                throw (count != 4usize) Usage { .message = "draw needs player count and wanted number" };
                u32 players = std.convert::parse_u32(arguments[2], 10u32);
                u32 wanted = std.convert::parse_u32(arguments[3], 10u32);
                response.output = example.tournament.season::draw(players, wanted);
            }
        }
    } catch (Usage failure) { response.output = f"{failure.message}\n"; response.status = 64; }
    catch (std.convert::parse_error failure) { response.status = 65; response.output = std.string::from_str("invalid integer\n"); }
    catch (std.array::push_error<i32> failure) { return 71; }
    catch (std.array::push_error<u32> failure) { return 71; }
    catch (std.array::push_error<example.tournament.season::Player> failure) { return 71; }
    catch (std.array::push_error<example.tournament.season::Standing> failure) { return 71; }
    catch (std.dict::insert_error<u32, u32> failure) { return 71; }
    catch (std.dict::insert_error<example.tournament.season::Pairing, bool> failure) { return 71; }
    await std.console::print(core::replace(&response.output, std.string::create()));
    return response.status;
}
