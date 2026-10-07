module example.arena.store;
import std.postgres;
import example.arena.mapping::{table, key};
import example.arena.mapping;
import std.time;
import std.uuid;
import std.text;

/* The users of the game in their PostgreSQL table. Rows
   read into structs and structs write rows by the JSON names of their fields (Library
   R-SLIB-PG-0015), the typed columns go through the typed reads and parameters
   (R-SLIB-PG-0013, R-SLIB-PG-0014), and numbered migrations make the schema (R-SLIB-PG-0016). */

@derive(format)
enum BanType { none, suspicious, ban };

/* What registration writes; the database fills the id, the defaults and the later columns. */
@table("users")
struct NewUser {
    std.string::string account_id;
    std.string::string nickname;
    std.string::string email;
    std.string::string avatar;
    std.string::string created_at;
};

/* A ban, written by an UPDATE keyed by the id. */
@table("users")
struct BanChange {
    @key i64 id;
    BanType ban_type;
    std.string::string last_ban_check;
};

/* A user as the list reads it: a field takes the column of its JSON name, clan_id may be NULL,
   gems is numeric and joined is computed by the SELECT. */
struct User {
    i64 id;
    @json(name = "account_id") std.string::string account;
    std.string::string nickname;
    std.string::string email;
    o<i64> clan_id;
    BanType ban_type;
    bool is_deleted;
    std.json::value stats;
    array<std.string::string> badges;
    f64 gems;
    i64 joined;
};

error NoUser { i64 id; };

protected std.postgres::migration step(u64 version, str name, str sql) throws std.alloc::alloc_error {
    return std.postgres::migration {.version = version, .name = std.string::from_str(name), .sql = std.string::from_str(sql)};
}

protected void add_step(array<std.postgres::migration>* steps, std.postgres::migration item) throws std.alloc::alloc_error {
    try {
        steps->push(move item);
    } catch (std.array::push_error<std.postgres::migration> rejected) {
        (move rejected) as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

/* The schema in the order it grew; an applied migration is never edited, a change is a new one. */
protected array<std.postgres::migration> migrations() throws std.alloc::alloc_error {
    array<std.postgres::migration> steps = [];
    add_step(&steps, step(1u64, "users",
                          "CREATE TABLE users (id bigserial PRIMARY KEY, account_id text NOT NULL UNIQUE, "
                          "nickname text UNIQUE, email text NOT NULL, avatar text NOT NULL DEFAULT '', clan_id bigint, "
                          "ban_type text NOT NULL DEFAULT 'none', last_ban_check timestamptz, "
                          "is_deleted boolean NOT NULL DEFAULT false, stats jsonb NOT NULL DEFAULT '{}', "
                          "created_at timestamptz NOT NULL DEFAULT now())"));
    add_step(&steps, step(2u64, "sessions",
                          "ALTER TABLE users ADD COLUMN session uuid, ADD COLUMN last_sync timestamptz, "
                          "ADD COLUMN badges text[] NOT NULL DEFAULT '{}'"));
    add_step(&steps, step(3u64, "gems",
                          "ALTER TABLE users ADD COLUMN gems numeric(12, 2) NOT NULL DEFAULT 0; "
                          "CREATE INDEX users_clan ON users (clan_id)"));
    add_step(&steps, step(4u64, "commands",
                          "CREATE TABLE client_commands (account_id text NOT NULL, command_id text NOT NULL, "
                          "status integer NOT NULL, body text NOT NULL, PRIMARY KEY (account_id, command_id))"));
    add_step(&steps, step(5u64, "stocks",
                          "CREATE TABLE user_stocks (id bigserial UNIQUE, account_id text NOT NULL, "
                          "offer_id text NOT NULL, stock bigint NOT NULL, PRIMARY KEY (account_id, offer_id))"));
    return move steps;
}

protected void add(array<std.postgres::value>* target, std.postgres::value item) throws std.alloc::alloc_error {
    try {
        target->push(move item);
    } catch (std.array::push_error<std.postgres::value> rejected) {
        (move rejected) as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

/* Whether the options already name the zone of the session, as PGTZ makes from_environment do. */
protected bool has_zone(const std.postgres::options* settings) {
    for (usize index = 0usize; index < len(settings->settings); index += 1usize) {
        const std.postgres::setting* item = &settings->settings[index];
        if (std.text::equal_ignore_ascii_case(item->name, "TimeZone") == true) { return true; }
    }
    return false;
}

/* The session counts days in the zone of the rewards (ARENA_TIME_ZONE; without it the zone of
   PGTZ, else UTC): the startup message sets it as TimeZone, so DATE(created_at) is a day of that
   zone whatever the zone of the server. */
protected async std.postgres::connection open_database() throws std.postgres::pg_error, std.error::fault {
    std.postgres::options settings = std.postgres::options::from_environment();
    o<std.string::string> zone = std.env::get("ARENA_TIME_ZONE");
    switch (move zone) {
    case variant o::some(move named):
        settings.set("TimeZone", named);
        drop named;
    case variant o::none:
        if (has_zone(&settings) == false) { settings.set("TimeZone", "UTC"); }
    }
    return await std.postgres::connect(move settings);
}

async std.string::string migrate() throws std.postgres::pg_error, std.error::fault {
    std.postgres::connection db = await open_database();
    u64 applied = await std.postgres::migrate(&db, migrations());
    await (move db).close();
    return f"applied {applied} migrations\n";
}

async std.string::string register(NewUser fresh) throws std.postgres::pg_error, std.error::fault {
    std.postgres::connection db = await open_database();
    std.string::string sql = example.arena.mapping::insert_statement::<NewUser>();
    sql.append(" RETURNING id");
    std.postgres::rows made = await db.query(sql, std.postgres::parameters_of(&fresh));
    i64 id = made.items[0usize].integer(0usize);
    await (move db).close();
    return f"user {id}\n";
}

async std.string::string ban(BanChange change) throws std.postgres::pg_error, std.error::fault {
    std.postgres::connection db = await open_database();
    std.string::string sql = example.arena.mapping::update_statement::<BanChange>();
    u64 changed = await db.execute(sql, std.postgres::parameters_of(&change));
    await (move db).close();
    return f"{sql}\nchanged {changed}\n";
}

/* The badges of a comma-separated list as the elements of a text[] parameter. */
protected std.postgres::value badge_list(str text) throws std.alloc::alloc_error {
    array<std.postgres::value> items = [];
    const u8[] raw_text = text;
    if (len(raw_text) > 0usize) {
        for (str piece in std.text::split(text, ",")) { add(&items, std.postgres::value::of_text(piece)); }
    }
    try {
        return std.postgres::value::of_array(&items);
    } catch (std.postgres::pg_error rejected) {
        (move rejected) as void;
    }
    throw std.alloc::alloc_error::out_of_memory;
}

/* A sync of the client: the time, its session, its badges and new counters merged into stats. */
async std.string::string sync(i64 id, std.time::system_time at, std.uuid::uuid session, std.string::string badges,
                              std.json::value stats) throws std.postgres::pg_error, std.error::fault {
    std.postgres::connection db = await open_database();
    array<std.postgres::value> given = [];
    add(&given, std.postgres::value::integer(id));
    add(&given, std.postgres::value::of_time(at));
    add(&given, std.postgres::value::of_uuid(&session));
    add(&given, badge_list(badges));
    add(&given, std.postgres::value::of_json(&stats));
    u64 changed = await db.execute(
        "UPDATE users SET last_sync = $2, session = $3, badges = $4, stats = stats || $5 WHERE id = $1", move given);
    await (move db).close();
    return f"synced {changed}\n";
}

protected void put_list(std.string::string* out, const array<std.string::string>* items) throws std.alloc::alloc_error {
    if (len(*items) == 0usize) {
        out->append("-");
        return;
    }
    for (usize index = 0usize; index < len(*items); index += 1usize) {
        if (index > 0usize) { out->append(","); }
        out->append((*items)[index]);
    }
}

/* Every user, read into User by the names of the columns. */
async std.string::string users() throws std.postgres::pg_error, std.json::error, std.error::fault {
    std.postgres::connection db = await open_database();
    std.postgres::rows found =
        await db.query("SELECT *, extract(epoch FROM created_at)::bigint AS joined FROM users ORDER BY id", []);
    await (move db).close();
    array<User> players = found.decode_all::<User>();
    std.string::string out = std.string::create();
    for (usize index = 0usize; index < len(players); index += 1usize) {
        i64 id = players[index].id;
        str account = players[index].account;
        str nick = players[index].nickname;
        BanType ban_type = players[index].ban_type;
        f64 gems = players[index].gems;
        i64 joined = players[index].joined;
        std.string::string line = f"{id} {account} {nick} {ban_type} gems {gems} joined {joined} clan ";
        out.append(line);
        switch (players[index].clan_id) {
        case variant o::some(clan):
            i64 shown = *clan;
            std.string::string number = f"{shown}";
            out.append(number);
        case variant o::none: out.append("-");
        }
        out.append(" badges ");
        put_list(&out, &players[index].badges);
        out.append(" stats ");
        std.string::string stats = std.json::stringify(&players[index].stats);
        out.append(stats);
        out.append("\n");
    }
    return move out;
}

protected void put_two(std.string::string* out, u8 number) throws std.alloc::alloc_error {
    if (number < 10u8) { out->append("0"); }
    std.string::string digits = f"{number}";
    out->append(digits);
}

/* A time of the database in RFC 3339; the server writes no year past 9999, which RFC 3339
   could not hold. */
protected std.string::string shown_time(std.time::system_time at) throws std.alloc::alloc_error {
    try {
        return std.time::format_rfc3339(at, 0u32);
    } catch (std.time::time_error rejected) {
        rejected as void;
    }
    return std.string::from_str("(outside RFC 3339)");
}

/* One user through the typed reads of its columns, found by name. */
async std.string::string user(i64 id) throws NoUser, std.postgres::pg_error, std.json::error, std.error::fault {
    std.postgres::connection db = await open_database();
    array<std.postgres::value> key = [];
    add(&key, std.postgres::value::integer(id));
    std.postgres::rows found = await db.query(
        "SELECT nickname, DATE(created_at) AS joined_on, last_sync, session, badges, stats "
        "FROM users WHERE id = $1",
        move key);
    await (move db).close();
    throw (len(found.items) == 0usize) NoUser {.id = id};
    const std.postgres::row* first = &found.items[0usize];
    str nick = first->text(found.column_index("nickname"));
    std.time::utc_datetime day = first->date(found.column_index("joined_on"));
    i32 year = day.year;
    std.string::string out = f"{nick} joined {year}-";
    put_two(&out, day.month);
    out.append("-");
    put_two(&out, day.day);
    out.append("\n");
    usize synced = found.column_index("last_sync");
    if (first->is_null(synced) == true) {
        out.append("never synced\n");
        return move out;
    }
    std.string::string at = shown_time(first->time(synced));
    std.uuid::uuid session = first->uuid(found.column_index("session"));
    std.string::string line = f"last sync {at} session {session}\n";
    out.append(line);
    out.append("badges");
    array<o<std.string::string>> badges = first->text_array(found.column_index("badges"));
    for (usize index = 0usize; index < len(badges); index += 1usize) {
        switch (badges[index]) {
        case variant o::some(badge):
            out.append(" [");
            out.append(*badge);
            out.append("]");
        case variant o::none: out.append(" NULL");
        }
    }
    std.json::value stats = first->json(found.column_index("stats"));
    out.append("\nstats");
    for (usize index = 0usize; index < std.json::len(&stats); index += 1usize) {
        out.append(" ");
        out.append(std.json::key_at(&stats, index));
    }
    out.append("\n");
    return move out;
}

/* What an account may still buy of an offer. The table, its key of two columns and the id the
   database fills come from the attribute of std.postgres (Library R-SLIB-PG-0017), so the
   operations below name no column. */
@std.postgres::table(name = "user_stocks", key = "account_id, offer_id", generated = "id")
struct UserStock {
    i64 id = 0i64;
    std.string::string account_id;
    std.string::string offer_id;
    i64 stock;
};

protected array<std.postgres::value> stock_key(str account, str offer) throws std.alloc::alloc_error {
    array<std.postgres::value> key = [];
    add(&key, std.postgres::value::of_text(account));
    add(&key, std.postgres::value::of_text(offer));
    return move key;
}

protected UserStock stock_of(str account, str offer, i64 stock) throws std.alloc::alloc_error {
    return UserStock {.account_id = std.string::from_str(account), .offer_id = std.string::from_str(offer), .stock = stock};
}

/* A purchase in one transaction: the first purchase of an offer opens its stock with five items;
   the decrement is written after a savepoint and rolled back to it when the stock is short. */
async std.string::string buy(std.string::string account, std.string::string offer, i64 count)
    throws std.postgres::pg_error, std.error::fault {
    std.postgres::connection db = await open_database();
    await db.begin();
    i64 have = 5i64;
    i64 id = 0i64;
    o<UserStock> found = await db.find::<UserStock>(stock_key(account, offer));
    switch (found) {
    case variant o::some(row):
        have = row->stock;
        id = row->id;
    case variant o::none: break;
    }
    if (found is variant o::none) {
        UserStock opened = stock_of(account, offer, have);
        UserStock created = await db.insert(&opened);
        id = created.id;
    }
    drop found;
    await db.savepoint("purchase");
    i64 left = have - count;
    UserStock after = stock_of(account, offer, left);
    after.id = id;
    u64 changed = await db.update(&after);
    changed as void;
    std.string::string answer = std.string::create();
    if (have < count) {
        await db.rollback_to("purchase");
        std.string::string refused = f"{offer} is out of stock for {account}: {have} left\n";
        answer.append(refused);
    } else {
        std.string::string bought = f"{account} bought {count} of {offer}, {left} left\n";
        answer.append(bought);
    }
    await db.release("purchase");
    await db.commit();
    await (move db).close();
    return move answer;
}

/* The stocks of an account, in the order of their offers. */
async std.string::string stocks(std.string::string account) throws std.postgres::pg_error, std.error::fault {
    std.postgres::connection db = await open_database();
    array<std.postgres::value> given = [];
    add(&given, std.postgres::value::of_text(account));
    array<UserStock> found = await db.select::<UserStock>("WHERE account_id = $1 ORDER BY offer_id", move given);
    await (move db).close();
    std.string::string out = std.string::create();
    for (usize index = 0usize; index < len(found); index += 1usize) {
        str offer = found[index].offer_id;
        i64 stock = found[index].stock;
        std.string::string line = f"{offer} {stock}\n";
        out.append(line);
    }
    return move out;
}

/* New stocks of an offer for several accounts: their old rows go, the new ones are written in
   one statement. */
async std.string::string restock(std.string::string offer, i64 stock, array<std.string::string> accounts)
    throws std.postgres::pg_error, std.error::fault {
    std.postgres::connection db = await open_database();
    array<UserStock> fresh = [];
    for (usize index = 0usize; index < len(accounts); index += 1usize) {
        UserStock old = stock_of(accounts[index], offer, 0i64);
        u64 removed = await db.remove(&old);
        removed as void;
        try {
            fresh.push(stock_of(accounts[index], offer, stock));
        } catch (std.array::push_error<UserStock> rejected) {
            (move rejected) as void;
            throw std.alloc::alloc_error::out_of_memory;
        }
    }
    u64 written = await db.insert_all(&fresh);
    await (move db).close();
    return f"restocked {written} of {offer}\n";
}
