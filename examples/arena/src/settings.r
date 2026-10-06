module example.arena.settings;
import std.config;
import std.time;

/* The settings of the server: its environment variables in one struct (Library
   R-SLIB-CONFIG-0004), and its tuning keys, declared with defaults and replaced by ARENA_*
   variables. */

struct Environment {
    @json(name = "PG_HOST") std.string::string pg_host;
    @json(name = "PG_PORT", optional) u16 pg_port = 5432u16;
    @json(name = "PG_DB_NAME") std.string::string database;
    @json(name = "PG_USER") std.string::string user;
    @json(name = "PG_PASSWORD") std.string::string password;
    @json(name = "API_KEY") std.string::string api_key;
    @json(name = "ENVIRONMENT") std.string::string environment;
    @json(name = "PROD") bool production;
    @json(name = "HOST_PORT", optional) u16 host_port = 8080u16;
    @json(name = "DD_SERVICE") std.string::string datadog_service;
    @json(name = "DD_ENV") std.string::string datadog_environment;
    @json(name = "DD_PROJECT") o<std.string::string> datadog_project;
};

struct Tuning {
    u64 pool_size;
    u64 request_limit;
    std.string::string time_zone;
    std.string::string season_end;
};

/* The end of the season as the operators write it, dd.MM.yyyy HH:mm in the zone of the rewards,
   shown as the log and the clients read dates. */
protected std.string::string season_end(str written) throws std.config::field_error, std.alloc::alloc_error {
    try {
        std.time::local_time end = std.time::parse_local(written, "dd.MM.yyyy HH:mm");
        return std.time::format_local(end, "yyyy-MM-dd HH:mm");
    } catch (std.convert::parse_error rejected) {
        rejected as void;
    } catch (std.time::time_error rejected) {
        rejected as void;
    }
    throw std.config::field_error {.code = std.config::error_code::invalid_value,
                                   .name = std.string::from_str("season_end")};
}

/* What the server would start with, the password and API key left out. */
std.string::string describe() throws std.config::field_error, std.config::config_error, std.error::fault {
    Environment found = std.config::from_environment::<Environment>("");
    str project = "arena";
    switch (found.datadog_project) {
    case variant o::some(named): project = named->as_str();
    case variant o::none: break;
    }
    std.config::config layers = std.config::config::create();
    layers.define("pool_size", "8", "database connections");
    layers.define("request_limit", "1048576", "largest request body");
    layers.define("time_zone", "UTC", "zone of the daily rewards");
    layers.define("season_end", "31.12.2026 23:59", "end of the season in the zone of the rewards");
    layers.load_environment("ARENA");
    Tuning tuning = layers.decode::<Tuning>();
    str user = found.user;
    str host = found.pg_host;
    u16 port = found.pg_port;
    str database = found.database;
    str environment = found.environment;
    str service = found.datadog_service;
    str datadog_environment = found.datadog_environment;
    str zone = tuning.time_zone;
    std.string::string season = season_end(tuning.season_end);
    return f"database {user}@{host}:{port}/{database}\nenvironment {environment} production={found.production}\nlisten on :{found.host_port}\ndatadog service={service} env={datadog_environment} project={project}\npool {tuning.pool_size} connections, requests up to {tuning.request_limit} bytes, rewards in {zone}\nseason ends {season}\n";
}
