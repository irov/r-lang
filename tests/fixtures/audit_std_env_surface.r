module audit.std_env_surface;

usize sync_argument_count() throws std.env::env_error {
    array<std.string::string> arguments = std.env::arguments();
    usize count = len(arguments);
    drop arguments;

    dict<std.string::string, std.string::string> variables = std.env::variables();
    drop variables;
    return count;
}

async i32 main() {
    constexpr str name = "R_FRONTEND_ENV_SURFACE_TEST_87E53312";
    try {
        if (sync_argument_count() == 0usize) {
            return 1;
        }

        std.env::set(name, "ok");
        o<std.string::string> present = std.env::get(name);
        switch (move present) {
        case variant o::some(move value):
            if (std.string::len(&value) != 2usize) {
                drop value;
                return 2;
            }
            drop value;
            break;
        case variant o::none:
            return 3;
        }

        std.env::remove(name);
        o<std.string::string> absent = std.env::get(name);
        switch (move absent) {
        case variant o::some(move value):
            drop value;
            return 4;
        case variant o::none:
            break;
        }

        array<std.string::string> arguments = std.env::arguments();
        drop arguments;
        dict<std.string::string, std.string::string> variables = std.env::variables();
        drop variables;
        return 0;
    } catch (std.env::env_error failure) {
        failure as void;
        return 5;
    }
}
