module example.orders.main;
import std.console;
import std.convert;
import std.fs;
import std.pool;
import std.postgres;
import std.text;
import std.tls;
import example.orders.store;

/* orders keeps products and orders in the PostgreSQL server that PGHOST, PGPORT, PGUSER,
   PGPASSWORD and PGDATABASE name: placing an order takes stock in a transaction, every order is
   announced with NOTIFY, and orders and products move in and out as CSV with COPY. */
enum Command { init, stock, place, show, totals, dump, load, watch, demo };

error Usage { u32 code; };

protected const str usage_text =
    "orders init | stock SKU NAME COUNT | place SKU QUANTITY | show | totals | dump | load FILE\n"
    "orders watch COUNT | demo\n";

str word(const array<std.string::string>* arguments, usize index) {
    return (*arguments)[index];
}

protected std.string::string owned(const array<std.string::string>* arguments, usize index)
    throws std.alloc::alloc_error {
    return std.string::from_str(word(arguments, index));
}

protected i64 number(const array<std.string::string>* arguments, usize index)
    throws std.convert::parse_error {
    return std.convert::parse_i64(word(arguments, index), 10u32);
}

async void say(std.string::string text) throws std.error::fault {
    await std.console::println(move text);
}

protected std.string::string report(example.orders.store::placed outcome, str sku) throws std.alloc::alloc_error {
    i64 id = outcome.order_id;
    i64 left = outcome.available;
    if (outcome.accepted == true) { return f"order {id}: {left} left"; }
    if (left < 0i64) { return f"refused: no product {sku}"; }
    return f"refused: only {left} of {sku}";
}

protected async i64 buy(std.pool::pool<std.postgres::connection> shop, i64 quantity)
    throws std.postgres::pg_error, std.error::fault {
    o<std.pool::lease<std.postgres::connection>> taken = shop.acquire();
    switch (move taken) {
    case variant o::some(move lease):
        example.orders.store::placed outcome =
            await example.orders.store::place((lease.get())->share(), std.string::from_str("tea"), quantity);
        if (outcome.accepted == true) { return 1i64; }
    case variant o::none: break;
    }
    return 0i64;
}

protected async bool sleep_on(std.postgres::connection db) throws std.postgres::pg_error, std.error::fault {
    try {
        array<std.postgres::value> none = example.orders.store::values();
        std.postgres::rows slept = await db.query("SELECT pg_sleep(30)", move none);
        drop slept;
    } catch (std.postgres::pg_error failure) {
        return std.text::equal_ignore_ascii_case(failure.sqlstate, "57014");
    }
    return false;
}

/* Every step in one program: two tasks order the same product from a pool of connections while a
   third connection listens, one order is refused for lack of stock, a long statement is cancelled,
   and the orders leave as CSV. */
async void demo() throws std.postgres::pg_error, std.tls::tls_error, std.error::fault {
    std.postgres::connection db = await example.orders.store::open();
    await example.orders.store::init(db.share());
    await example.orders.store::stock(db.share(), std.string::from_str("tea"), std.string::from_str("green tea"), 5i64);
    await say(f"stock: green tea 5");
    std.postgres::connection listener = await example.orders.store::open();
    await listener.listen("orders");
    std.pool::pool<std.postgres::connection> shop =
        await std.postgres::connect_pool(std.postgres::options::from_environment(), 2usize);
    i64 accepted = 0i64;
    task_scope(2) group {
        auto first = buy(shop.share(), 2i64);
        auto second = buy(shop.share(), 2i64);
        accepted += await move first;
        accepted += await move second;
    }
    await say(f"two tasks ordered 2 each: {accepted} accepted");
    example.orders.store::placed third =
        await example.orders.store::place(db.share(), std.string::from_str("tea"), 2i64);
    await say(report(third, "tea"));
    u32 announced = 0u32;
    for (u32 heard = 0u32; heard < 2u32; heard += 1u32) {
        std.postgres::notification message = await listener.wait_notification();
        if (std.text::ends_with(message.payload, " tea 2") == true) { announced += 1u32; }
    }
    await say(f"announced on channel orders: {announced}");
    std.postgres::canceller key = db.canceller();
    u32 cancelled = 0u32;
    task_scope(2) group {
        auto slow = sleep_on(db.share());
        await std.time::sleep_for(std.time::duration_from_parts(0i64, 200000000u32));
        await std.postgres::cancel(move key);
        if (await move slow == true) { cancelled += 1u32; }
    }
    await say(f"long statements cancelled: {cancelled}");
    bytes csv = await example.orders.store::export_orders(db.share());
    std.string::string text = std.string::from_utf8(csv.as_slice());
    await std.console::print(move text);
    std.string::string shown = await example.orders.store::listing(db.share());
    await std.console::print(move shown);
    await (move listener).close();
    await (move db).close();
}

/* A server error is named by its SQLSTATE, any other failure by its error code. */
protected str label_of(const std.postgres::pg_error* failure) {
    if (failure->code == std.postgres::error_code::server) { return failure->sqlstate; }
    return core::enum_name(failure->code);
}

async i32 main() {
    array<std.string::string> arguments = std.env::arguments();
    usize given = len(arguments);
    o<Command> command = o::none;
    if (given >= 2usize) { command = core::enum_from_name::<Command>(arguments[1]); }
    switch (command) {
    case variant o::none:
        await std.console::eprint(std.string::from_str(usage_text));
        if (given == 1usize) { return 0; }
        return 64;
    case variant o::some(selected):
        try {
            switch (*selected) {
            case Command::init:
                throw (given != 2usize) Usage {.code = 2u32};
                std.postgres::connection db = await example.orders.store::open();
                await example.orders.store::init(db.share());
                await (move db).close();
                await say(f"ready");
            case Command::stock:
                throw (given != 5usize) Usage {.code = 2u32};
                i64 count = number(&arguments, 4usize);
                std.postgres::connection db = await example.orders.store::open();
                await example.orders.store::stock(db.share(), owned(&arguments, 2usize), owned(&arguments, 3usize), count);
                await (move db).close();
                str sku = word(&arguments, 2usize);
                await say(f"stock {sku}: {count}");
            case Command::place:
                throw (given != 4usize) Usage {.code = 2u32};
                i64 quantity = number(&arguments, 3usize);
                std.postgres::connection db = await example.orders.store::open();
                example.orders.store::placed outcome =
                    await example.orders.store::place(db.share(), owned(&arguments, 2usize), quantity);
                await (move db).close();
                await say(report(outcome, word(&arguments, 2usize)));
                if (outcome.accepted == false) { return 65; }
            case Command::show:
                throw (given != 2usize) Usage {.code = 2u32};
                std.postgres::connection db = await example.orders.store::open();
                std.string::string shown = await example.orders.store::listing(db.share());
                await (move db).close();
                await std.console::print(move shown);
            case Command::totals:
                throw (given != 2usize) Usage {.code = 2u32};
                std.postgres::connection db = await example.orders.store::open();
                std.string::string shown = await example.orders.store::totals(db.share());
                await (move db).close();
                await std.console::print(move shown);
            case Command::dump:
                throw (given != 2usize) Usage {.code = 2u32};
                std.postgres::connection db = await example.orders.store::open();
                bytes csv = await example.orders.store::export_orders(db.share());
                await (move db).close();
                await std.console::print(std.string::from_utf8(csv.as_slice()));
            case Command::load:
                throw (given != 3usize) Usage {.code = 2u32};
                std.fs::path path = std.fs::path_from_utf8(word(&arguments, 2usize));
                bytes csv = await std.fs::read_file(&path, 16777216usize);
                std.postgres::connection db = await example.orders.store::open();
                u64 count = await example.orders.store::import_products(db.share(), move csv);
                await (move db).close();
                await say(f"imported {count}");
            case Command::watch:
                throw (given != 3usize) Usage {.code = 2u32};
                i64 count = number(&arguments, 2usize);
                std.postgres::connection db = await example.orders.store::open();
                await example.orders.store::watch(db.share(), count as u32);
                await (move db).close();
            case Command::demo:
                throw (given != 2usize) Usage {.code = 2u32};
                await demo();
            }
        } catch (Usage failure) {
            failure as void;
            await std.console::eprint(std.string::from_str(usage_text));
            return 64;
        } catch (std.convert::parse_error failure) {
            failure as void;
            await std.console::eprint(std.string::from_str(usage_text));
            return 64;
        } catch (std.postgres::pg_error failure) {
            str state = label_of(&failure);
            str text = failure.message;
            await std.console::eprintln(f"postgres: {state} {text}");
            return 69;
        } catch (std.tls::tls_error failure) {
            std.tls::error_code code = failure.code;
            str name = core::enum_name(code);
            await std.console::eprintln(f"tls: {name}");
            return 69;
        } catch (std.error::fault failure) {
            std.error::error error = std.error::from_fault(failure);
            std.string::string description = error.diagnostic();
            await std.console::eprintln(f"fault: {description}");
            return 71;
        }
    }
    return 0;
}
