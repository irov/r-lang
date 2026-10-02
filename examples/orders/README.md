# Orders in PostgreSQL

Keep products and orders in a PostgreSQL server with `std.postgres`, a client of the
frontend/backend protocol written in R over `std.net` and `std.tls` (Library
R-SLIB-PG-0001..0012). An order takes stock in a transaction that locks the row of its product,
a trigger announces every order on the channel `orders` with `NOTIFY`, and products and orders
move in and out as CSV with `COPY`.

The program connects to the server that the environment names, as libpq reads it: `PGHOST` (a
directory of the Unix-domain socket when it starts with `/`), `PGPORT`, `PGUSER`, `PGPASSWORD` and
`PGDATABASE`. The server may ask for SCRAM-SHA-256, md5 or a cleartext password. With
`PGSSLROOTCERT`, the program connects over TLS that trusts the authorities of that file and
checks that the certificate of the server names the host.

```sh
ctest --test-dir build/debug -R 'example_orders' --output-on-failure
build/debug/tests/codegen_example_orders init
build/debug/tests/codegen_example_orders stock tea 'green tea' 5
build/debug/tests/codegen_example_orders place tea 2
build/debug/tests/codegen_example_orders totals
build/debug/tests/codegen_example_orders watch 2
build/debug/tests/codegen_example_orders demo
```

`demo` runs every step in one program: two tasks order the same product at once through a pool
of two connections, a third order is refused for lack of stock, a listening connection hears both
orders, a long statement is cancelled from another task, and the orders leave as CSV:

```text
stock: green tea 5
two tasks ordered 2 each: 2 accepted
refused: only 1 of tea
announced on channel orders: 2
long statements cancelled: 1
1,tea,2
2,tea,2
1 green tea x2 (1 left)
2 green tea x2 (1 left)
```

[store.r](src/store.r) holds the database work. Every operation of a connection returns a task,
so a connection can be shared by several tasks, which take turns on it; `share()` gives another
handle. An order runs in a transaction:

```r
await db.begin();
std.postgres::rows found = await db.query("SELECT stock FROM products WHERE sku = $1 FOR UPDATE", move key);
...
(await db.execute("UPDATE products SET stock = stock - $1 WHERE sku = $2", move take)) as void;
std.postgres::rows made = await db.query("INSERT INTO orders(sku, quantity) VALUES ($1, $2) RETURNING id", move order);
await db.commit();
```

`totals` prepares one statement and runs it for every product; its first line names the columns
of the result. `watch COUNT` listens on the channel and prints each announcement, so another shell can place
orders and see them arrive. `std.postgres::cancel` sends the cancel request of the protocol over a
connection of its own; the cancelled statement fails with SQLSTATE 57014. A server error ends the
command with status 69 and its SQLSTATE and message on standard error; any other failure of
`std.postgres`, such as a refused certificate, is named by its error code, and a failure of the
transport, such as a server that is not there, ends with status 71 and its diagnostic.

The behaviour test (`tests/run_orders_examples.py`) starts a temporary cluster with `initdb` and
`pg_ctl`, checks the tables with `psql`, runs `watch` in another process while orders are placed,
and connects over TCP with SCRAM-SHA-256, over TLS with the certificates of `tests/fixtures/tls`
and over the Unix-domain socket.
