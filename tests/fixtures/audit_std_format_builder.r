module audit.std_format_builder;

i32 exercise() {
    try {
        std.format::builder direct = std.format::create();
        std.format::append_str(&direct, "prefix");
        std.format::append_char(&direct, ':');
        std.format::append_f32(&direct, 1.5f32);
        std.format::append_f64(&direct, 2.25);
        std.format::append_i32(&direct, -42, 10);
        std.format::append_u64(&direct, 18446744073709551615u64, 16);

        c_float small = std.c::checked_c_float(3.5f32);
        c_double medium = std.c::checked_c_double(4.75);
        c_long_double large = std.c::checked_c_long_double(5.125);
        std.format::append_c_float(&direct, small);
        std.format::append_c_double(&direct, medium);
        std.format::append_c_long_double(&direct, large);

        str observed = std.format::as_str(&direct);
        if (len(observed) == 0usize) {
            drop direct;
            return 1;
        }
        observed as void;
        std.format::clear(&direct);
        str cleared = std.format::as_str(&direct);
        if (len(cleared) != 0usize) {
            drop direct;
            return 2;
        }
        cleared as void;
        std.format::append_str(&direct, "done");
        std.string::string completed = std.format::finish(move direct);
        const u8[] completed_bytes = std.string::as_bytes(&completed);
        if (len(completed_bytes) != 4usize) {
            drop completed;
            return 3;
        }
        drop completed;

        std.format::builder reserved = std.format::with_capacity(32usize);
        std.format::append_str(&reserved, "reserved");
        std.string::string reserved_text = std.format::finish(move reserved);
        const u8[] reserved_bytes = std.string::as_bytes(&reserved_text);
        if (len(reserved_bytes) != 8usize) {
            drop reserved_text;
            return 4;
        }
        drop reserved_text;
        return 0;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 5;
    } catch (std.format::format_error failure) {
        failure as void;
        return 6;
    } catch (std.convert::range_error failure) {
        failure as void;
        return 7;
    }
}

async i32 main() {
    if (exercise() != 0) {
        return 1;
    }
    try {
        std.format::builder builder = std.format::with_capacity(16usize);
        std.format::append_str(&builder, "async");
        std.format::append_char(&builder, '!');
        std.format::append_f64(&builder, 6.5);
        std.format::append_i64(&builder, -9223372036854775808i64, 10);
        std.string::string text = std.format::finish(move builder);
        const u8[] bytes_view = std.string::as_bytes(&text);
        if (len(bytes_view) == 0usize) {
            drop text;
            return 2;
        }
        drop text;
        return 0;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 3;
    } catch (std.format::format_error failure) {
        failure as void;
        return 4;
    }
}
