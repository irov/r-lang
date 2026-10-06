module example.quotes.main;
import std.console;
import example.quotes.book;
import example.quotes.book::{Quote, BookError};
import example.quotes.policy;
import example.quotes.policy::{PolicyError};
import example.calculator.common::{Usage};

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string output; i32 status; };

enum Command { prices, force, rate, watch };

async i32 main(const str[] arguments) {
    try {
        CommandResponse response = {.output = std.string::create(), .status = 0};
        
        try {
            if (len(arguments) == 1usize) {
                std.string::string help = std.string::from_str("quotes prices|force PRICE_MINOR...\nquotes rate TAX_BASIS_POINTS PRICE_MINOR...\nquotes watch PRICE_MINOR...\n");
                await std.console::print(move help);
                return 0;
            }
            o<Command> selected = core::enum_from_name::<Command>(arguments[1]);
            Command command = Command::prices;
            switch (selected) {
            case variant o::some(value): command = *value; break;
            case variant o::none: throw Usage { .message = "unknown quote command" };
            }
            std.sync::once authorization = std.sync::once_new();
            std.sync::once_lock<u32> tax = std.sync::once_lock::<u32>();
            usize first = 2usize;
            if (command == Command::rate) {
                throw (len(arguments) < 3usize) Usage { .message = "rate needs tax basis points" };
                u32 explicit_rate = std.convert::parse_u32(arguments[2], 10u32);
                throw (explicit_rate > 10000u32) PolicyError::InvalidRate;
                std.sync::set_result<u32> configured = tax.set(explicit_rate);
                switch (move configured) {
                case variant std.sync::set_result::stored: break;
                case variant std.sync::set_result::occupied(move rejected): throw PolicyError::InvalidRate;
                }
                first = 3usize;
            }
            Quote initial = { .minor = 0u64, .revision = 0usize };
            std.sync::rw_lock<Quote> book = std.sync::rwlock_new(initial);
            array<u64> publication = std.array::create::<u64>();
            for (usize index = first; index < len(arguments); index += 1usize) {
                // Completed once operations skip repeated policy validation.
                if (command == Command::force) { authorization.call_once_force(example.quotes.policy::validate); }
                else { authorization.call_once(example.quotes.policy::validate); }
                u64 price = std.convert::parse_u64(arguments[index], 10u32);
                throw (price > 1000000000000u64) Usage { .message = "price is limited to one trillion minor units" };
                const u32* rate = tax.get_or_init(example.quotes.policy::default_rate);
                u64 basis = *rate as u64;
                u64 gross = price + ((price * basis + 5000u64) / 10000u64);
                if (command == Command::watch) {
                    publication.push(gross);
                } else {
                u64 previous = example.quotes.book::try_write(&book, gross);
                Quote current = example.quotes.book::try_read(&book);
                std.string::string row = f"revision={current.revision} net={price} gross={current.minor} previous={previous}\n";
                str text = row;
                response.output.append(text);
            }
                }
            if (command == Command::watch) {
                const u64[] values = publication.as_slice();
                response.output = example.quotes.book::watch(values);
            }
            drop authorization; drop book; drop publication;
            o<const u32*> configured = tax.get();
            switch (configured) {
            case variant o::some(value):
                u32 rate = **value;
                std.string::string row = f"tax_basis_points={rate}\n";
                str text = row;
                response.output.append(text); break;
            case variant o::none: response.output.append("no prices\n"); break;
            }
        } catch (Usage failure) { response.output = f"{failure.message}\n"; response.status = 64; }
        catch (PolicyError failure) { response.output = std.string::from_str("quote policy rejected\n"); response.status = 78; }
        catch (BookError failure) { response.output = std.string::from_str("quote lock failed\n"); response.status = 70; }
        await std.console::print(core::replace(&response.output, std.string::create()));
        return response.status;
    } catch (std.array::push_error<u64> failure) { return 71; }
    catch (std.env::env_error failure) { return 78; }
    catch (std.convert::parse_error failure) { return 65; }
}
