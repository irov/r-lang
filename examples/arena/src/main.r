module example.arena.main;
import std.console;
import std.crypto;
import std.jwt;
import std.oauth2;
import std.http;
import std.tls;
import example.arena.sessions;
import example.arena.google;
import example.arena.settings;
import example.arena.store;
import example.arena.server;
import std.config;
import std.encoding;
import std.postgres;
import std.uuid;
import std.json;
import std.time;

/* arena is the back end of a mobile game: session tokens signed with ES256, administrator
   tokens with HS256, access tokens for the APIs it calls, its settings, its users in PostgreSQL
   and its HTTP API. */
/* The form of a command in the usage text; the last command of a line ends it (Core R-AGG-0013). */
@attribute(variant) struct help { str form; bool ends_line = false; };

enum Command {
    @help("token KEY ACCOUNT NICK EMAIL PLATFORM PROVIDER AVATAR") token,
    @help("check KEY TOKEN") check,
    @help("jwks KEY", true) jwks,
    @help("admin SECRET SUBJECT SECONDS") admin,
    @help("admin_check SECRET TOKEN", true) admin_check,
    @help("google KEY_FILE SCOPE") google,
    @help("client ENDPOINT CLIENT SECRET") client,
    @help("login ENDPOINT CODE VERIFIER", true) login,
    @help("config", true) config,
    @help("migrate") migrate,
    @help("register ACCOUNT NICK EMAIL CREATED") register,
    @help("ban ID TYPE AT") ban,
    @help("sync ID AT SESSION BADGES STATS", true) sync,
    @help("users") users,
    @help("user ID") user,
    @help("server KEY CIPHER", true) server,
    @help("buy ACCOUNT OFFER COUNT") buy,
    @help("stocks ACCOUNT") stocks,
    @help("restock OFFER COUNT ACCOUNT...", true) restock
};

error Usage { u32 code; };

/* The usage text from the help of every command, in declaration order (R-REFL-0005). */
protected std.string::string usage_text() throws std.alloc::alloc_error {
    std.string::string text = std.string::create();
    bool line_start = true;
    for (usize index = 0usize; index < core::enum_count::<Command>(); index += 1usize) {
        o<Command> command = core::enum_at::<Command>(index);
        switch (command) {
        case variant o::some(found):
            o<help> shown = core::variant_attribute::<help, Command>(*found);
            switch (shown) {
            case variant o::some(entry):
                str separator = line_start == true ? "arena " : " | ";
                text.append(separator);
                text.append(entry->form);
                line_start = entry->ends_line;
                if (line_start == true) { text.append("\n"); }
            case variant o::none: break;
            }
        case variant o::none: break;
        }
    }
    return move text;
}

protected str word(const array<std.string::string>* arguments, usize index) {
    return (*arguments)[index];
}

protected std.string::string take(array<std.string::string>* arguments, usize index) {
    return core::replace(&(*arguments)[index], std.string::create());
}

protected async bytes read_text(std.string::string name) throws std.error::fault {
    std.fs::path path = std.fs::path_from_utf8(name);
    return await path.read_file(1048576usize);
}

protected async void say(std.string::string text) throws std.error::fault {
    await std.console::print(move text);
}

protected usize key_size(const bytes* key) {
    const u8[] given = key->as_slice();
    return len(given);
}

/* The commands of the users in PostgreSQL run in their own task: each command of the server
   keeps its locals in the frame of the function that runs it, so this frame stays small. */
protected bool database_command(Command selected) {
    switch (selected) {
    case Command::migrate: return true;
    case Command::register: return true;
    case Command::ban: return true;
    case Command::sync: return true;
    case Command::users: return true;
    case Command::user: return true;
    case Command::server: return true;
    case Command::buy: return true;
    case Command::stocks: return true;
    case Command::restock: return true;
    default: return false;
    }
}

protected async i32 database(Command selected, array<std.string::string> arguments) throws std.error::fault {
    usize given = len(arguments);
    try {
        switch (selected) {
        case Command::migrate:
            throw (given != 2usize) Usage {.code = 2u32};
            await say(await example.arena.store::migrate());
        case Command::register:
            throw (given != 6usize) Usage {.code = 2u32};
            example.arena.store::NewUser fresh = {
                .account_id = take(&arguments, 2usize), .nickname = take(&arguments, 3usize),
                .email = take(&arguments, 4usize), .avatar = std.string::from_str("1_avatar"),
                .created_at = take(&arguments, 5usize)};
            await say(await example.arena.store::register(move fresh));
        case Command::ban:
            throw (given != 5usize) Usage {.code = 2u32};
            o<example.arena.store::BanType> kind = core::enum_from_name::<example.arena.store::BanType>(word(&arguments, 3usize));
            switch (kind) {
            case variant o::some(found):
                example.arena.store::BanChange change = {
                    .id = std.convert::parse_i64(word(&arguments, 2usize), 10u32), .ban_type = *found,
                    .last_ban_check = take(&arguments, 4usize)};
                await say(await example.arena.store::ban(move change));
            case variant o::none: throw Usage {.code = 2u32};
            }
        case Command::sync:
            throw (given != 7usize) Usage {.code = 2u32};
            std.time::system_time at = std.time::parse_rfc3339(word(&arguments, 3usize));
            std.uuid::uuid session = std.uuid::parse(word(&arguments, 4usize));
            std.json::value stats = std.json::parse(arguments[6]);
            await say(await example.arena.store::sync(std.convert::parse_i64(word(&arguments, 2usize), 10u32), at, session,
                                                      take(&arguments, 5usize), move stats));
        case Command::users:
            throw (given != 2usize) Usage {.code = 2u32};
            await say(await example.arena.store::users());
        case Command::user:
            throw (given != 3usize) Usage {.code = 2u32};
            i64 id = std.convert::parse_i64(word(&arguments, 2usize), 10u32);
            try {
                await say(await example.arena.store::user(id));
            } catch (example.arena.store::NoUser failure) {
                i64 missing = failure.id;
                await std.console::eprintln(f"no user {missing}");
                return 65;
            }
        case Command::server:
            throw (given != 4usize) Usage {.code = 2u32};
            bytes key = await read_text(take(&arguments, 2usize));
            bytes cipher = std.encoding::decode_hex(word(&arguments, 3usize));
            throw (key_size(&cipher) != 32usize) Usage {.code = 2u32};
            std.jwt::key_set keys = example.arena.sessions::keys_of(key.as_slice());
            await say(await example.arena.server::run(move keys, move cipher));
        case Command::buy:
            throw (given != 5usize) Usage {.code = 2u32};
            i64 count = std.convert::parse_i64(word(&arguments, 4usize), 10u32);
            throw (count < 1i64) Usage {.code = 2u32};
            await say(await example.arena.store::buy(take(&arguments, 2usize), take(&arguments, 3usize), count));
        case Command::stocks:
            throw (given != 3usize) Usage {.code = 2u32};
            await say(await example.arena.store::stocks(take(&arguments, 2usize)));
        case Command::restock:
            throw (given < 5usize) Usage {.code = 2u32};
            i64 stock = std.convert::parse_i64(word(&arguments, 3usize), 10u32);
            array<std.string::string> accounts = [];
            for (usize index = 4usize; index < given; index += 1usize) {
                try {
                    accounts.push(take(&arguments, index));
                } catch (std.array::push_error<std.string::string> rejected) {
                    (move rejected) as void;
                    throw std.alloc::alloc_error::out_of_memory;
                }
            }
            await say(await example.arena.store::restock(take(&arguments, 2usize), stock, move accounts));
        default: throw Usage {.code = 2u32};
        }
        return 0;
    } catch (Usage failure) {
        failure as void;
        await std.console::eprint(usage_text());
        return 64;
    } catch (std.convert::parse_error failure) {
        failure as void;
        await std.console::eprint(usage_text());
        return 64;
    } catch (std.json::error failure) {
        (move failure) as void;
        await std.console::eprintln(std.string::from_str("stats: not JSON"));
        return 65;
    } catch (std.crypto::crypto_error failure) {
        std.crypto::error_code code = failure.code;
        await std.console::eprintln(f"key: {code}");
        return 65;
    } catch (std.jwt::jwt_error failure) {
        std.jwt::error_code code = failure.code;
        await std.console::eprintln(f"key: {code}");
        return 65;
    } catch (std.postgres::pg_error failure) {
        /* A server error names its SQLSTATE, such as 23505 for a nickname that is taken, and the
           constraint it violated; the refusals of the client name what they refused. */
        std.postgres::error_code code = failure.code;
        if (code == std.postgres::error_code::connection) {
            await std.console::eprintln(f"database: {code}");
            return 69;
        }
        if (code == std.postgres::error_code::server) {
            str state = failure.sqlstate;
            if (failure.constraint.len() == 0usize) {
                await std.console::eprintln(f"database: server {state}");
                return 65;
            }
            /* A violated constraint is named, such as the unique nickname of a user. */
            str violated = failure.constraint;
            await std.console::eprintln(f"database: server {state} {violated}");
            return 65;
        }
        const u8[] detail_bytes = failure.detail;
        if (len(detail_bytes) == 0usize) {
            str message = failure.message;
            await std.console::eprintln(f"database: {code}: {message}");
            return 65;
        }
        str detail = failure.detail;
        await std.console::eprintln(f"database: {code} {detail}");
        return 65;
    }
}

async i32 main() {
    array<std.string::string> arguments = std.env::arguments();
    usize given = len(arguments);
    o<Command> command = o::none;
    if (given >= 2usize) { command = core::enum_from_name::<Command>(arguments[1]); }
    switch (command) {
    case variant o::none:
        await std.console::eprint(usage_text());
        if (given == 1usize) { return 0; }
        return 64;
    case variant o::some(selected):
        if (database_command(*selected) == true) { return await database(*selected, move arguments); }
        try {
            switch (*selected) {
            case Command::token:
                throw (given != 9usize) Usage {.code = 2u32};
                bytes key = await read_text(take(&arguments, 2usize));
                example.arena.sessions::Session session = {
                    .account = take(&arguments, 3usize), .nick = take(&arguments, 4usize),
                    .email = take(&arguments, 5usize), .platform = take(&arguments, 6usize),
                    .provider = take(&arguments, 7usize), .avatar = take(&arguments, 8usize)};
                std.string::string token = example.arena.sessions::issue(key.as_slice(), &session);
                token.append("\n");
                await say(move token);
            case Command::check:
                throw (given != 4usize) Usage {.code = 2u32};
                bytes key = await read_text(take(&arguments, 2usize));
                try {
                    example.arena.sessions::Session session =
                        example.arena.sessions::check(key.as_slice(), word(&arguments, 3usize));
                    std.string::string head = example.arena.sessions::describe(word(&arguments, 3usize));
                    str account = session.account;
                    str nick = session.nick;
                    str platform = session.platform;
                    str provider = session.provider;
                    await say(f"{head}\naccount {account} nick {nick} platform {platform} provider {provider}\n");
                } catch (std.jwt::jwt_error failure) {
                    std.jwt::error_code code = failure.code;
                    await say(f"rejected: {code}\n");
                    return 65;
                }
            case Command::jwks:
                throw (given != 3usize) Usage {.code = 2u32};
                bytes key = await read_text(take(&arguments, 2usize));
                std.string::string set = example.arena.sessions::jwks(key.as_slice());
                set.append("\n");
                await say(move set);
            case Command::admin:
                throw (given != 5usize) Usage {.code = 2u32};
                i64 seconds = std.convert::parse_i64(word(&arguments, 4usize), 10u32);
                std.string::string token = example.arena.sessions::admin_token(
                    word(&arguments, 2usize), word(&arguments, 3usize), std.time::system_now(), seconds);
                token.append("\n");
                await say(move token);
            case Command::admin_check:
                throw (given != 4usize) Usage {.code = 2u32};
                try {
                    std.string::string claims = example.arena.sessions::admin(
                        word(&arguments, 2usize), word(&arguments, 3usize), std.time::system_now());
                    claims.append("\n");
                    await say(move claims);
                } catch (std.jwt::jwt_error failure) {
                    std.jwt::error_code code = failure.code;
                    await say(f"rejected: {code}\n");
                    return 65;
                }
            case Command::google:
                throw (given != 4usize) Usage {.code = 2u32};
                bytes file = await read_text(take(&arguments, 2usize));
                std.string::string text = std.string::from_utf8(file.as_slice());
                await say(await example.arena.google::service_tokens(move text, take(&arguments, 3usize)));
            case Command::client:
                throw (given != 5usize) Usage {.code = 2u32};
                await say(await example.arena.google::client_token(take(&arguments, 2usize), take(&arguments, 3usize),
                                                                   take(&arguments, 4usize)));
            case Command::config:
                throw (given != 2usize) Usage {.code = 2u32};
                try {
                    await say(example.arena.settings::describe());
                } catch (std.config::field_error failure) {
                    std.config::error_code code = failure.code;
                    str name = failure.name;
                    await std.console::eprintln(f"config: {code} {name}");
                    return 78;
                }
            case Command::login:
                throw (given != 5usize) Usage {.code = 2u32};
                await say(await example.arena.google::login(take(&arguments, 2usize), take(&arguments, 3usize),
                                                            take(&arguments, 4usize)));
            default: throw Usage {.code = 2u32};
            }
            return 0;
        } catch (Usage failure) {
            failure as void;
            await std.console::eprint(usage_text());
            return 64;
        } catch (std.config::config_error failure) {
            std.config::error_code code = failure.code;
            await std.console::eprintln(f"config: {code}");
            return 78;
        } catch (std.convert::parse_error failure) {
            failure as void;
            await std.console::eprint(usage_text());
            return 64;
        } catch (std.string::string_error failure) {
            failure as void;
            await std.console::eprintln(std.string::from_str("not UTF-8"));
            return 65;
        } catch (std.crypto::crypto_error failure) {
            std.crypto::error_code code = failure.code;
            await std.console::eprintln(f"key: {code}");
            return 65;
        } catch (std.jwt::jwt_error failure) {
            std.jwt::error_code code = failure.code;
            await std.console::eprintln(f"token: {code}");
            return 65;
        } catch (std.json::error failure) {
            (move failure) as void;
            await std.console::eprintln(std.string::from_str("claims: not JSON"));
            return 65;
        } catch (std.oauth2::oauth2_error failure) {
            std.oauth2::error_code code = failure.code;
            u16 status = failure.status;
            await std.console::eprintln(f"oauth2: {code} {status}");
            return 69;
        } catch (std.http::http_error failure) {
            std.http::error_code code = failure.code;
            await std.console::eprintln(f"http: {code}");
            return 69;
        } catch (std.tls::tls_error failure) {
            std.tls::error_code code = failure.code;
            await std.console::eprintln(f"tls: {code}");
            return 69;
        }
    }
    return 70;
}
