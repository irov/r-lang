# The back end of a mobile game

`arena` grows, stage by stage, into the back end of a mobile game: HTTP routes over PostgreSQL,
session tokens signed with ES256, an encrypted protocol, and the Google APIs for purchases and
configuration. This part issues and checks the tokens: ES256 session tokens and HS256
administrator tokens of `std.jwt`, the JWK Set of the server key, and OAuth 2.0 access tokens of
`std.oauth2` for a service account, the admin panel and a player's login; reads its settings;
keeps its users in PostgreSQL; and serves its HTTP API as an application with middleware (Library
R-SLIB-JWT-0001..0007, R-SLIB-OAUTH2-0001..0006, R-SLIB-CONFIG-0004, R-SLIB-PG-0013..0016,
R-SLIB-HTTP-0015..0018).

```sh
ctest --test-dir build/debug -R 'example_arena' --output-on-failure
openssl ecparam -name prime256v1 -genkey -noout -out key.pem
build/debug/tests/codegen_example_arena token key.pem 42 Ann ann@example.test ios apple 7
build/debug/tests/codegen_example_arena check key.pem TOKEN
build/debug/tests/codegen_example_arena jwks key.pem
build/debug/tests/codegen_example_arena admin 'an administrator secret of 32 by' operator 300
build/debug/tests/codegen_example_arena google service-account.json https://www.googleapis.com/auth/androidpublisher
```

Each command carries its usage form in a `@help` attribute of its enumerator (Core R-AGG-0013);
a wrong command or a wrong number of operands prints the usage built from them in declaration
order with `core::variant_attribute` and exits with 64.

`token` signs the claims the game clients read, in the field names they expect, with the server
key in PEM. The key may be the SEC1 `EC PRIVATE KEY` that `openssl ecparam` writes, or PKCS#8.
ES256 signatures are the deterministic ones of RFC 6979, so the same key and claims give the same
token:

```r
struct Session {
    @json(name = "AccountID") std.string::string account;
    @json(name = "NickName") std.string::string nick;
    ...
};

std.jwt::signer signer = std.jwt::signer::with_private_key(std.jwt::algorithm::es256, move key);
signer.set_key_id("arena-1");
std.string::string token = std.jwt::sign_claims(&signer, &session);
```

`check` verifies a token with the public key and reads the claims back into a `Session`; the
sessions of the game have no expiry:

```text
alg ES256 kid arena-1
account 42 nick Ann platform ios provider apple
```

A changed payload or another key ends with `rejected: invalid_signature` and status 65. `jwks`
prints the JWK Set of the public key that other services fetch. `admin` signs an HS256 token for
an operator, valid for the given seconds, with a shared secret of at least 32 bytes, and
`admin_check` verifies its issuer and expiry: a stale token is `rejected: expired`.

[google.r](src/google.r) gets access tokens. `google KEY_FILE SCOPE` reads a service account key
file of Google Cloud, signs the RS256 assertion of RFC 7523 for the token endpoint the file names,
and asks for a token three times through one `std.oauth2::token_source`: the second time the
token comes from the cache, the third time `renew` asks again:

```text
account robot@arena.test
first sa-1
again sa-1
renewed sa-2
```

`client ENDPOINT CLIENT SECRET` is the admin panel's client-credentials request with HTTP Basic,
and `login ENDPOINT CODE VERIFIER` exchanges a player's authorization code with its PKCE
verifier once, then refreshes the short token it got and follows the rotated refresh token.

The behaviour test makes the server key and the service account key with the Python
`cryptography` package, compares the session token byte for byte with an ES256 token made there,
checks the HS256 signature, and serves a token endpoint on loopback that verifies the RS256
assertion before it answers.

## Settings

[settings.r](src/settings.r) reads the environment variables of the server in one call into one
struct. The fields name the variables with `@json`; a field without a default is required,
`@json(optional)` keeps its initializer, and an `o<T>` field is none when its variable is unset:

```r
struct Environment {
    @json(name = "PG_HOST") std.string::string pg_host;
    @json(name = "PG_PORT", optional) u16 pg_port = 5432u16;
    @json(name = "PROD") bool production;
    @json(name = "DD_PROJECT") o<std.string::string> datadog_project;
    ...
};

Environment found = std.config::from_environment::<Environment>("");
```

The tuning keys of the server are declared with defaults in a `std.config::config`, replaced by
`ARENA_*` variables and read into a struct with `decode`. `config` prints what the server would
start with, without the password and the API key:

```text
database svc@db.internal:6432/game
environment eu1 production=true
listen on :8080
datadog service=arena-backend-eu1 env=eu1 project=arena
pool 16 connections, requests up to 1048576 bytes, rewards in Asia/Shanghai
season ends 2026-12-31 23:59
```

The end of the season is written by the operators as `dd.MM.yyyy HH:mm` (`ARENA_SEASON_END`,
`31.12.2026 23:59` by default); `std.time::parse_local` reads it by that pattern and
`std.time::format_local` writes it as `yyyy-MM-dd HH:mm`.

A missing required variable ends with `config: missing_value DD_ENV`, a value that does not fit
its field, such as a port above 65535, a `PROD` that is not a boolean or a season end of
`31.02.2027 06:00`, with `config: invalid_value PG_PORT` (or the name of the key), all with status
78.

## Users in PostgreSQL

[store.r](src/store.r) keeps the users of the game in its `users` table. The commands connect with
the libpq variables (`PGHOST`, `PGPORT`, `PGUSER`, `PGPASSWORD`, `PGDATABASE`);
`ctest -R arena_database` runs them against a temporary cluster when PostgreSQL 17 is installed.

`migrate` applies the numbered migrations that the database does not have yet, each in its own
transaction, under an advisory lock, and records them with a checksum in `r_schema_migrations`.
A second start applies none, and a migration that changed after it was applied is refused before
anything runs:

```r
array<std.postgres::migration> steps = [];
add_step(&steps, step(1u64, "users", "CREATE TABLE users (id bigserial PRIMARY KEY, ...)"));
add_step(&steps, step(2u64, "sessions", "ALTER TABLE users ADD COLUMN session uuid, ..."));
u64 applied = await std.postgres::migrate(&db, move steps);
```

```text
applied 4 migrations
applied 0 migrations
database: migration_mismatch version 2
```

`register ACCOUNT NICK EMAIL CREATED` writes a `NewUser` with the INSERT that
`insert_statement` makes from the JSON names of its fields, and the parameters of
`parameters_of` in the same order; a nickname that is taken is the unique violation of the
server, `database: server 23505`. `ban ID TYPE AT` updates the row by its id with the statement
of `update_statement`, which it prints. The table and the key come from attributes that the
program declares in [mapping.r](src/mapping.r) (Core R-AGG-0013) and reads at translation time
(R-REFL-0005), so no statement names them:

```r
@attribute(type) struct table { str name; };
@attribute(field) struct key {};

@table("users")
struct BanChange { @key i64 id; BanType ban_type; std.string::string last_ban_check; };

std.string::string sql = example.arena.mapping::update_statement::<BanChange>();
```

```text
UPDATE users SET "ban_type" = $2, "last_ban_check" = $3 WHERE "id" = $1
changed 1
```

`sync ID AT SESSION BADGES STATS` sends typed parameters: the instant of RFC 3339, the uuid of the
session, the badges as a `text[]` and new counters merged into the `jsonb` stats. `user ID` reads
them back through the typed reads of the columns, found by name:

```text
Ann joined 2024-03-10
last sync 2024-04-01T12:00:00Z session 0190f7e1-1234-7abc-8def-0123456789ab
badges [first win] [ёж "q"]
stats wins losses
```

`users` reads every row into a struct by the names of its columns, with `o<i64>` for the clan
that may be NULL, an enum stored as its name, the numeric gems as `f64` and a column that the
SELECT computes:

```r
std.postgres::rows found =
    await db.query("SELECT *, extract(epoch FROM created_at)::bigint AS joined FROM users ORDER BY id", []);
array<User> players = found.decode_all::<User>();
```

```text
1 42 Ann none gems 12.5 joined 1710054000 clan 7 badges first win,ёж "q" stats {"wins":3,"losses":1}
2 43 Борис suspicious gems 0 joined 1710181800 clan - badges - stats {"wins":0}
```

A row that does not fit the struct is refused: a NULL among the badges of an `array<string>` field
is `database: invalid_value: a row does not read as the struct`, and a table without the
`nickname` column `database: missing_column nickname`.

## The HTTP server

[server.r](src/server.r) serves the API of the game as a `std.http::app<Server, Call>`: `Server`
is the state every request shares (a `std.pool` of two database connections, the session keys
and the key of the protocol), and `Call` the context the application makes for each request,
which its middleware fills on the way in and reads on the way back:

```r
std.http::app<Server, Call> routes = std.http::app<Server, Call>::create(open_call);
routes.around("/", open_request, close_request);                 // request id, logger, one record
routes.before("/api", authenticate);                              // cookie or bearer token, else 401
routes.around("/api/secure", open_body, seal_body);               // AES-256-CBC bodies both ways
routes.around("/api/commands", begin_command, finish_command);    // a transaction per command
routes.route(std.http::method::get, "/api/users/{id}", player);
routes.route(std.http::method::get, "/api/users/me", me);         // wins over {id} in any order
routes.cors(move policy);                                         // https://game.example, credentials
routes.redirect_trailing_slash(true);
routes.method_not_allowed(false);                                 // 404 for a known path, wrong method
routes.on_panic(report_panic);                                    // the record of a panicking request
```

`server KEY CIPHER` connects with the libpq variables, listens on a loopback port, prints
`ready PORT` and serves with `std.http::serve_app` until SIGTERM or SIGINT, then prints what it
served. KEY is the PEM key of the session tokens and CIPHER the 32-byte AES key in hex.

Each hook takes the flow of the request and returns it: `authenticate` verifies the ES256 token
of the `arena_session` cookie or of `Authorization: Bearer` and writes the account into the
context, or answers 401, after which no later stage and no route runs; the after hooks of the
stages already passed still do. `begin_command` takes a connection from the pool, begins a
transaction and locks the player's row with `SELECT ... FOR UPDATE`; a command id
(`X-Command-ID`) that the player sent before gets the answer kept for it, marked `X-Replayed`, and
does not run again. The handler finds the connection in the context, and `finish_command` keeps
the answer in `client_commands` and commits, or rolls back after a 5xx answer or a failed
statement, such as a nickname that is taken (409).

Answers go through `std.json::escape_html`, so a nickname `<Hero&Co>` is sent as
`\u003cHero\u0026Co\u003e` and reads back unchanged.

`POST /api/commands/crash` changes the row and then panics. The application runs the flow of
each request as a task of its own and awaits it with `std.async::join`, so the panic ends that
request only: the client gets 500, the unwind drops the flow and gives the connection back to
the pool inside its transaction, the next command on it rolls that transaction back first, and
the kept-alive HTTP connection and the server go on:

```text
stopped: accepted 1, failed 0, requests 22, panics 1, free connections 2, log lines 23
```

### The log

The server writes one JSON record per line to standard error through a `std.log` logger whose
layout puts the level first, then the bound fields, the fields of the record, the time as
`timestamp` in RFC 3339 with the digits the clock gives, the place of the call as `caller`
(`core::location()`, passed to `logger::log_at`), and the message last:

```r
std.log::layout shape = std.log::layout::standard();
shape.time_name = std.string::from_str("timestamp");
shape.task_name = o::none;
shape.time_digits = 9u32;
shape.trim_time = true;
shape.omit_empty_message = true;
shape.caller_name = o::some(std.string::from_str("caller"));
shape.sequence = std.log::order::level_first;
base.set_layout(move shape);
```

`open_request` gives each request the identifier of its `X-Request-ID`, or a new UUID, echoes it
in the response and binds it to a child logger of the request (`logger::with`); `authenticate`
binds `usr.id` the same way, and `close_request` writes the record of the request at the level its
status calls for:

```text
{"level":"info","service":"arena","component":"http","request_id":"req-2","usr.id":"42","http.method":"GET","http.route":"/api/me","http.status_code":200,"timestamp":"2026-10-05T14:12:03.512881Z","caller":"example.arena.server:186","message":"http request completed"}
```

A request whose handler panics loses its flow and its logger, so `report_panic` writes its
record from the notes of the request (`std.http::notes`), which outlive the flow:

```text
{"level":"error","service":"arena","component":"http","request_id":"req-17","usr.id":"42","http.method":"POST","http.route":"/api/commands/crash","http.status_code":500,"panic":"explicit: the crash command","severity":"critical","timestamp":"…","caller":"example.arena.server:96","message":"panic recovered"}
```

`POST /api/background` answers 202 and leaves a detached task that panics. Nothing observes that
panic, so its report would be a line of the runtime on standard error; the server listens for such
reports with `std.log::panic_reports` and writes them as records too, with the place the runtime
names.

The behaviour test is a client written in Python on one kept-alive connection: it makes its own
ES256 tokens, seals and opens the bodies of the secure routes with the `cryptography` package,
and checks the rows with psql after each command; it then reads every record of the log as JSON
and checks the order of its members, the time and the fields of each request.
