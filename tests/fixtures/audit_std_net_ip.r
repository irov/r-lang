module audit.std_net_ip;

protected std.net::address_error_code expected_out_of_range() {
    return std.net::address_error_code::out_of_range;
}

i32 sync_probe() throws std.net::address_error, std.alloc::alloc_error {
    std.net::ip_address address = std.net::parse_ip("127.0.0.1");
    std.string::string text = std.net::format_ip(address);
    if (std.string::len(&text) != 9usize) {
        drop text;
        return 1;
    }
    drop text;
    return 0;
}

async i32 main() {
    try {
        i32 sync_status = sync_probe();
        if (sync_status != 0) {
            return sync_status;
        }

        std.net::ip_address loopback = std.net::parse_ip("::1");
        std.string::string text = std.net::format_ip(loopback);
        if (std.string::len(&text) != 3usize) {
            drop text;
            return 2;
        }
        drop text;

        try {
            std.net::ip_address invalid = std.net::parse_ip("1.2.256.4");
            invalid as void;
            return 3;
        } catch (std.net::address_error failure) {
            if (failure.code != expected_out_of_range()) {
                return 4;
            }
            if (failure.index != 6usize) {
                return 5;
            }
        }
        return 0;
    } catch (std.net::address_error failure) {
        failure as void;
        return 6;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 7;
    }
}
