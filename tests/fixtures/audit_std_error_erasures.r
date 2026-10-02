module audit.std_error_erasures;

std.error::error erase_address(std.net::address_error value) {
    return std.error::from_address(value);
}

std.error::error erase_alloc(std.alloc::alloc_error value) {
    return std.error::from_alloc(value);
}

std.error::error erase_async(std.async::start_error value) {
    return std.error::from_async(value);
}

std.error::error erase_barrier(std.sync::barrier_error value) {
    return std.error::from_barrier(value);
}

std.error::error erase_boundary(std.string::boundary_error value) {
    return std.error::from_boundary(value);
}

std.error::error erase_bytes(std.bytes::bytes_error value) {
    return std.error::from_bytes(value);
}

std.error::error erase_duration(std.time::duration_error value) {
    return std.error::from_duration(value);
}

std.error::error erase_format(std.format::format_error value) {
    return std.error::from_format(value);
}

std.error::error erase_parse(std.convert::parse_error value) {
    return std.error::from_parse(value);
}

std.error::error erase_path(std.fs::path_error value) {
    return std.error::from_path(value);
}

std.error::error erase_range(std.convert::range_error value) {
    return std.error::from_range(value);
}

std.error::error erase_string(std.string::string_error value) {
    return std.error::from_string(value);
}

std.error::error erase_thread(std.thread::thread_error value) {
    return std.error::from_thread(value);
}

std.error::error erase_c_runtime(std.c::runtime_error value) {
    return std.c::runtime_as_error(value);
}

std.error::error erase_c_string(std.c::string_error value) {
    return std.c::string_as_error(value);
}

std.error::error erase_environment(std.env::env_error value) {
    return std.env::as_error(value);
}

std.error::error erase_network(std.net::net_error value) {
    return std.net::as_error(value);
}

std.error::error erase_process(std.process::process_error value) {
    return std.process::as_error(value);
}

async std.error::error erase_c_runtime_async(std.c::runtime_error value) {
    return std.c::runtime_as_error(value);
}

async std.error::error erase_address_async(std.net::address_error value) {
    return std.error::from_address(value);
}

async std.error::error erase_alloc_async(std.alloc::alloc_error value) {
    return std.error::from_alloc(value);
}

async std.error::error erase_async_async(std.async::start_error value) {
    return std.error::from_async(value);
}

async std.error::error erase_barrier_async(std.sync::barrier_error value) {
    return std.error::from_barrier(value);
}

async std.error::error erase_boundary_async(std.string::boundary_error value) {
    return std.error::from_boundary(value);
}

async std.error::error erase_bytes_async(std.bytes::bytes_error value) {
    return std.error::from_bytes(value);
}

async std.error::error erase_duration_async(std.time::duration_error value) {
    return std.error::from_duration(value);
}

async std.error::error erase_format_async(std.format::format_error value) {
    return std.error::from_format(value);
}

async std.error::error erase_parse_async(std.convert::parse_error value) {
    return std.error::from_parse(value);
}

async std.error::error erase_path_async(std.fs::path_error value) {
    return std.error::from_path(value);
}

async std.error::error erase_range_async(std.convert::range_error value) {
    return std.error::from_range(value);
}

async std.error::error erase_string_async(std.string::string_error value) {
    return std.error::from_string(value);
}

async std.error::error erase_thread_async(std.thread::thread_error value) {
    return std.error::from_thread(value);
}

async std.error::error erase_c_string_async(std.c::string_error value) {
    return std.c::string_as_error(value);
}

async std.error::error erase_environment_async(std.env::env_error value) {
    return std.env::as_error(value);
}

async std.error::error erase_network_async(std.net::net_error value) {
    return std.net::as_error(value);
}

async std.error::error erase_process_async(std.process::process_error value) {
    return std.process::as_error(value);
}

async i32 main() {
    try {
        try {
            u8 value = std.convert::parse_u8("256", 10);
            value as void;
            return 1;
        } catch (std.convert::parse_error failure) {
            std.error::error erased = std.error::from_parse(failure);
            constexpr str name = std.error::name(erased);
            if (len(name) == 0usize) {
                return 2;
            }
            std.string::string diagnostic = std.error::diagnostic(erased);
            if (std.string::len(&diagnostic) == 0usize) {
                drop diagnostic;
                return 3;
            }
            drop diagnostic;
        }
        return 0;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 4;
    }
}
