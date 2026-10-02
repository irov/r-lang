module example.dispatch.main;
import std.console;
import example.calculator.common::{Usage};
import example.dispatch.routes;
import example.dispatch.priority;
import example.dispatch.priority::{Job};
import example.dispatch.roster;
import example.dispatch.messages;
import example.dispatch.messages::{Message, Delivery};

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string output; i32 status; };

// Every list command reports on the parsed inputs and its first parameter. The commands share
// one error set, so one table holds them as function values (Core R-TYPE-0054) and the command
// name selects its entry.
std.string::string forward(const i32[] view, str omit)
    throws Usage, std.convert::parse_error, std.array::push_error<i32>,
    std.array::push_error<Job>, std.dict::insert_error<i32, bool>, std.alloc::alloc_error {
    usize count = std.convert::parse_usize(omit, 10u32);
    return example.dispatch.routes::route(view, false, count, false);
}

std.string::string returning(const i32[] view, str omit)
    throws Usage, std.convert::parse_error, std.array::push_error<i32>,
    std.array::push_error<Job>, std.dict::insert_error<i32, bool>, std.alloc::alloc_error {
    usize count = std.convert::parse_usize(omit, 10u32);
    return example.dispatch.routes::route(view, true, count, false);
}

std.string::string cancel(const i32[] view, str omit)
    throws Usage, std.convert::parse_error, std.array::push_error<i32>,
    std.array::push_error<Job>, std.dict::insert_error<i32, bool>, std.alloc::alloc_error {
    usize count = std.convert::parse_usize(omit, 10u32);
    return example.dispatch.routes::route(view, false, count, true);
}

std.string::string prioritize(const i32[] view, str limit)
    throws Usage, std.convert::parse_error, std.array::push_error<i32>,
    std.array::push_error<Job>, std.dict::insert_error<i32, bool>, std.alloc::alloc_error {
    usize count = std.convert::parse_usize(limit, 10u32);
    return example.dispatch.priority::assign(view, count);
}

std.string::string roster(const i32[] view, str withdrawn)
    throws Usage, std.convert::parse_error, std.array::push_error<i32>,
    std.array::push_error<Job>, std.dict::insert_error<i32, bool>, std.alloc::alloc_error {
    i32 identifier = std.convert::parse_i32(withdrawn, 10u32);
    return example.dispatch.roster::reconcile(view, identifier, false);
}

std.string::string pause(const i32[] view, str withdrawn)
    throws Usage, std.convert::parse_error, std.array::push_error<i32>,
    std.array::push_error<Job>, std.dict::insert_error<i32, bool>, std.alloc::alloc_error {
    i32 identifier = std.convert::parse_i32(withdrawn, 10u32);
    return example.dispatch.roster::reconcile(view, identifier, true);
}

async i32 main(const str[] arguments) {
    CommandResponse response = {.output = std.string::create(), .status = 0};

    try {
        usize count = len(arguments);
        if (count == 1usize) {
            response.output = std.string::from_str("dispatch route|returning|cancel OMIT_LAST STOP...\ndispatch priority LIMIT URGENCY...\ndispatch roster|pause WITHDRAWN_ID REQUEST_ID...\ndispatch message ATTEMPT [TEXT]\n");
        } else {
            throw (count < 3usize) Usage { .message = "command needs its limit or withdrawn ID" };
            str name = arguments[1];
            if (std.bytes::equal(name, "message") == true) {
                throw (count > 4usize) Usage {.message = "message needs an attempt and optional text"};
                u32 attempt = std.convert::parse_u32(arguments[2], 10u32);
                Message message = Message::Status;
                if (count == 4usize) {
                    o<u32> retry = attempt == 0u32 ? o::none : o::some(attempt);
                    message = Message::Submit {
                        .body = std.string::from_str(arguments[3]), .retry = retry,
                    };
                } else {
                    throw (attempt != 0u32) Usage {.message = "retry needs message text"};
                }
                Delivery delivery = {
                    .first = example.dispatch.messages::deliver,
                    .retry = example.dispatch.messages::redeliver,
                };
                response.output = await example.dispatch.messages::process(move message, delivery);
            } else {
                dict<str, fn(const i32[], str) -> std.string::string throws(Usage,
                    std.convert::parse_error, std.array::push_error<i32>, std.array::push_error<Job>,
                    std.dict::insert_error<i32, bool>, std.alloc::alloc_error)> commands = {
                    "route": forward, "returning": returning, "cancel": cancel,
                    "priority": prioritize, "roster": roster, "pause": pause,
                };
                switch (std.dict::get(&commands, &name)) {
                case variant o::some(command):
                    array<i32> inputs = std.array::with_capacity::<i32>(count - 3usize);
                    for (usize index = 3usize; index < count; index += 1usize) {
                        i32 value = std.convert::parse_i32(arguments[index], 10u32);
                        inputs.push(value);
                    }
                    const i32[] view = inputs.as_slice();
                    response.output = (*command)(view, arguments[2]);
                case variant o::none: throw Usage { .message = "unknown dispatch command" };
                }
            }
        }
    } catch (Usage failure) { response.output = f"{failure.message}\n"; response.status = 64; }
    catch (std.convert::parse_error failure) {
        std.error::error error = std.error::from_parse(failure);
        response.output = error.diagnostic(); response.status = 65;
    } catch (std.array::push_error<i32> failure) { return 71; }
    catch (std.array::push_error<Job> failure) { return 71; }
    catch (std.dict::insert_error<i32, bool> failure) { return 71; }
    catch (std.dict::insert_error<str, fn(const i32[], str) -> std.string::string throws(Usage,
        std.convert::parse_error, std.array::push_error<i32>, std.array::push_error<Job>,
        std.dict::insert_error<i32, bool>, std.alloc::alloc_error)> failure) { return 71; }
    await std.console::print(core::replace(&response.output, std.string::create()));
    return response.status;
}
