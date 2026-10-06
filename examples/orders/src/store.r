module example.orders.store;
import std.postgres;
import std.pool;
import std.console;
import std.fs;
import std.tls;

/* Orders kept in PostgreSQL (Library R-SLIB-PG-0001..0012): products with their stock, orders
   that take stock in a transaction, and a trigger that announces every order on the channel
   orders with NOTIFY. The server is the one that the environment names, as libpq reads it. */

const str schema_sql =
    "DROP TABLE IF EXISTS orders; DROP TABLE IF EXISTS products;"
    "CREATE TABLE products(sku text PRIMARY KEY, name text NOT NULL, stock int NOT NULL CHECK (stock >= 0));"
    "CREATE TABLE orders(id bigserial PRIMARY KEY, sku text NOT NULL REFERENCES products, quantity int NOT NULL);"
    "CREATE OR REPLACE FUNCTION announce_order() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN "
    "PERFORM pg_notify('orders', NEW.id || ' ' || NEW.sku || ' ' || NEW.quantity); RETURN NEW; END $$;"
    "CREATE TRIGGER announce AFTER INSERT ON orders FOR EACH ROW EXECUTE FUNCTION announce_order();";

array<std.postgres::value> values() {
    array<std.postgres::value> none = [];
    return move none;
}

void add(array<std.postgres::value>* target, std.postgres::value item) throws std.alloc::alloc_error {
    try {
        target->push(move item);
    } catch (std.array::push_error<std.postgres::value> rejected) {
        (move rejected) as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

/* A connection to the server of the environment; with PGSSLROOTCERT, as libpq reads it, over TLS
   that trusts the authorities of that file and checks the name of the server. */
async std.postgres::connection open() throws std.postgres::pg_error, std.tls::tls_error, std.error::fault {
    std.postgres::options settings = std.postgres::options::from_environment();
    o<std.string::string> authority = std.env::get("PGSSLROOTCERT");
    switch (move authority) {
    case variant o::some(move file):
        std.fs::path path = std.fs::path_from_utf8(file);
        bytes certificates = await std.fs::read_file(&path, 1048576usize);
        std.tls::config trust = std.tls::client_config();
        trust.add_authority(certificates.as_slice());
        arc std.tls::config shared = new arc std.tls::config(move trust);
        return await std.postgres::connect_tls(move settings, move shared);
    case variant o::none: break;
    }
    return await std.postgres::connect(move settings);
}

async void init(std.postgres::connection db) throws std.postgres::pg_error, std.error::fault {
    await db.execute_script(schema_sql);
}

/* Sets the stock of a product, adding it when it is new. */
async void stock(std.postgres::connection db, std.string::string sku, std.string::string name, i64 count)
    throws std.postgres::pg_error, std.error::fault {
    array<std.postgres::value> parameters = values();
    add(&parameters, std.postgres::value::text(move sku));
    add(&parameters, std.postgres::value::text(move name));
    add(&parameters, std.postgres::value::integer(count));
    (await db.execute("INSERT INTO products(sku, name, stock) VALUES ($1, $2, $3) "
                      "ON CONFLICT (sku) DO UPDATE SET name = EXCLUDED.name, stock = EXCLUDED.stock",
                      move parameters)) as void;
}

/* What a placement did: the number of the order, or the stock that was too small; a product
   that does not exist has no stock, -1. */
struct placed { bool accepted; i64 order_id; i64 available; };

/* Places an order in a transaction: the row of the product is locked, its stock checked and
   taken, and the order inserted; any failure rolls the transaction back. */
async placed place(std.postgres::connection db, std.string::string sku, i64 quantity)
    throws std.postgres::pg_error, std.error::fault {
    await db.begin();
    try {
        array<std.postgres::value> key = values();
        add(&key, std.postgres::value::of_text(sku));
        std.postgres::rows found = await db.query("SELECT stock FROM products WHERE sku = $1 FOR UPDATE", move key);
        if (len(found.items) == 0usize) {
            await db.rollback();
            return placed {.accepted = false, .order_id = 0i64, .available = -1i64};
        }
        i64 available = found.items[0usize].integer(0usize);
        if (available < quantity) {
            await db.rollback();
            return placed {.accepted = false, .order_id = 0i64, .available = available};
        }
        array<std.postgres::value> take = values();
        add(&take, std.postgres::value::integer(quantity));
        add(&take, std.postgres::value::of_text(sku));
        (await db.execute("UPDATE products SET stock = stock - $1 WHERE sku = $2", move take)) as void;
        array<std.postgres::value> order = values();
        add(&order, std.postgres::value::of_text(sku));
        add(&order, std.postgres::value::integer(quantity));
        std.postgres::rows made = await db.query("INSERT INTO orders(sku, quantity) VALUES ($1, $2) RETURNING id", move order);
        i64 order_id = made.items[0usize].integer(0usize);
        await db.commit();
        return placed {.accepted = true, .order_id = order_id, .available = available - quantity};
    } catch (std.postgres::pg_error failure) {
        if (db.transaction_status() != std.postgres::transaction_status::idle) { await db.rollback(); }
        throw move failure;
    }
}

/* The orders with the names of their products, one per line. */
async std.string::string listing(std.postgres::connection db) throws std.postgres::pg_error, std.error::fault {
    std.postgres::rows found = await db.query(
        "SELECT o.id, p.name, o.quantity, p.stock FROM orders o JOIN products p USING (sku) ORDER BY o.id", values());
    std.string::string out = std.string::create();
    for (usize index = 0usize; index < len(found.items); index += 1usize) {
        const std.postgres::row* item = &found.items[index];
        i64 id = item->integer(0usize);
        str name = item->text(1usize);
        i64 quantity = item->integer(2usize);
        i64 left = item->integer(3usize);
        std.string::string line = f"{id} {name} x{quantity} ({left} left)\n";
        out.append(line);
    }
    return move out;
}

/* The orders as CSV, from COPY TO STDOUT. */
async bytes export_orders(std.postgres::connection db) throws std.postgres::pg_error, std.error::fault {
    return await db.copy_out("COPY (SELECT id, sku, quantity FROM orders ORDER BY id) TO STDOUT WITH (FORMAT csv)");
}

/* Products from CSV lines sku,name,stock, with COPY FROM STDIN; returns their number. */
async u64 import_products(std.postgres::connection db, bytes csv) throws std.postgres::pg_error, std.error::fault {
    return await db.copy_in("COPY products(sku, name, stock) FROM STDIN WITH (FORMAT csv)", move csv);
}

/* Waits for count announcements of new orders and prints each; ready is printed once the
   connection listens, so that another process can start ordering. */
async void watch(std.postgres::connection db, u32 count) throws std.postgres::pg_error, std.error::fault {
    await db.listen("orders");
    await std.console::println(std.string::from_str("listening"));
    for (u32 seen = 0u32; seen < count; seen += 1u32) {
        std.postgres::notification heard = await db.wait_notification();
        str payload = heard.payload;
        await std.console::println(f"order {payload}");
    }
}

/* The quantity ordered of every product, with a statement prepared once and run per product;
   the first line names the columns of the result. */
async std.string::string totals(std.postgres::connection db) throws std.postgres::pg_error, std.error::fault {
    std.postgres::rows products = await db.query("SELECT sku FROM products ORDER BY sku", values());
    std.postgres::statement total = await db.prepare(
        "SELECT sku, coalesce(sum(quantity), 0) AS ordered FROM products LEFT JOIN orders USING (sku) "
        "WHERE sku = $1 GROUP BY sku");
    std.string::string out = std.string::create();
    for (usize index = 0usize; index < len(products.items); index += 1usize) {
        array<std.postgres::value> key = values();
        add(&key, std.postgres::value::of_text(products.items[index].text(0usize)));
        std.postgres::rows found = await total.query(move key);
        if (index == 0usize) {
            const std.postgres::column[] heading = std.array::as_slice(&found.columns);
            str first = heading[0usize].name;
            str second = heading[1usize].name;
            std.string::string line = f"{first} {second}\n";
            out.append(line);
        }
        str sku = found.items[0usize].text(0usize);
        i64 ordered = found.items[0usize].integer(1usize);
        std.string::string line = f"{sku} {ordered}\n";
        out.append(line);
    }
    await (move total).close();
    return move out;
}
