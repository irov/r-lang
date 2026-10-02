module codegen.core_adopt_named_standard_copy;

core::memory_order copy_001(core::memory_order value, core::memory_order* output) {
    *output = value;
    return value;
}

void round_trip_001(raw core::memory_order* pointer) {
    unsafe {
        own core::memory_order* owner = core::adopt(pointer);
        raw core::memory_order* returned = core::release(move owner);
        returned as void;
    }
}

core::recursion_error copy_002(core::recursion_error value, core::recursion_error* output) {
    *output = value;
    return value;
}

void round_trip_002(raw core::recursion_error* pointer) {
    unsafe {
        own core::recursion_error* owner = core::adopt(pointer);
        raw core::recursion_error* returned = core::release(move owner);
        returned as void;
    }
}

core::utf8_error copy_003(core::utf8_error value, core::utf8_error* output) {
    *output = value;
    return value;
}

void round_trip_003(raw core::utf8_error* pointer) {
    unsafe {
        own core::utf8_error* owner = core::adopt(pointer);
        raw core::utf8_error* returned = core::release(move owner);
        returned as void;
    }
}

std.alloc::alloc_error copy_004(std.alloc::alloc_error value, std.alloc::alloc_error* output) {
    *output = value;
    return value;
}

void round_trip_004(raw std.alloc::alloc_error* pointer) {
    unsafe {
        own std.alloc::alloc_error* owner = core::adopt(pointer);
        raw std.alloc::alloc_error* returned = core::release(move owner);
        returned as void;
    }
}

std.async::start_error copy_005(std.async::start_error value, std.async::start_error* output) {
    *output = value;
    return value;
}

void round_trip_005(raw std.async::start_error* pointer) {
    unsafe {
        own std.async::start_error* owner = core::adopt(pointer);
        raw std.async::start_error* returned = core::release(move owner);
        returned as void;
    }
}

std.bits::lsb_reader copy_006(std.bits::lsb_reader value, std.bits::lsb_reader* output) {
    *output = value;
    return value;
}

void round_trip_006(raw std.bits::lsb_reader* pointer) {
    unsafe {
        own std.bits::lsb_reader* owner = core::adopt(pointer);
        raw std.bits::lsb_reader* returned = core::release(move owner);
        returned as void;
    }
}

std.bits::read_error copy_007(std.bits::read_error value, std.bits::read_error* output) {
    *output = value;
    return value;
}

void round_trip_007(raw std.bits::read_error* pointer) {
    unsafe {
        own std.bits::read_error* owner = core::adopt(pointer);
        raw std.bits::read_error* returned = core::release(move owner);
        returned as void;
    }
}

std.bits::read_error_code copy_008(std.bits::read_error_code value, std.bits::read_error_code* output) {
    *output = value;
    return value;
}

void round_trip_008(raw std.bits::read_error_code* pointer) {
    unsafe {
        own std.bits::read_error_code* owner = core::adopt(pointer);
        raw std.bits::read_error_code* returned = core::release(move owner);
        returned as void;
    }
}

std.bytes::bytes_error copy_009(std.bytes::bytes_error value, std.bytes::bytes_error* output) {
    *output = value;
    return value;
}

void round_trip_009(raw std.bytes::bytes_error* pointer) {
    unsafe {
        own std.bytes::bytes_error* owner = core::adopt(pointer);
        raw std.bytes::bytes_error* returned = core::release(move owner);
        returned as void;
    }
}

std.c::runtime_error copy_010(std.c::runtime_error value, std.c::runtime_error* output) {
    *output = value;
    return value;
}

void round_trip_010(raw std.c::runtime_error* pointer) {
    unsafe {
        own std.c::runtime_error* owner = core::adopt(pointer);
        raw std.c::runtime_error* returned = core::release(move owner);
        returned as void;
    }
}

std.c::string_error copy_011(std.c::string_error value, std.c::string_error* output) {
    *output = value;
    return value;
}

void round_trip_011(raw std.c::string_error* pointer) {
    unsafe {
        own std.c::string_error* owner = core::adopt(pointer);
        raw std.c::string_error* returned = core::release(move owner);
        returned as void;
    }
}

std.c::target_info copy_012(std.c::target_info value, std.c::target_info* output) {
    *output = value;
    return value;
}

void round_trip_012(raw std.c::target_info* pointer) {
    unsafe {
        own std.c::target_info* owner = core::adopt(pointer);
        raw std.c::target_info* returned = core::release(move owner);
        returned as void;
    }
}

std.convert::parse_error copy_013(std.convert::parse_error value, std.convert::parse_error* output) {
    *output = value;
    return value;
}

void round_trip_013(raw std.convert::parse_error* pointer) {
    unsafe {
        own std.convert::parse_error* owner = core::adopt(pointer);
        raw std.convert::parse_error* returned = core::release(move owner);
        returned as void;
    }
}

std.convert::parse_error_code copy_014(std.convert::parse_error_code value, std.convert::parse_error_code* output) {
    *output = value;
    return value;
}

void round_trip_014(raw std.convert::parse_error_code* pointer) {
    unsafe {
        own std.convert::parse_error_code* owner = core::adopt(pointer);
        raw std.convert::parse_error_code* returned = core::release(move owner);
        returned as void;
    }
}

std.convert::range_error copy_015(std.convert::range_error value, std.convert::range_error* output) {
    *output = value;
    return value;
}

void round_trip_015(raw std.convert::range_error* pointer) {
    unsafe {
        own std.convert::range_error* owner = core::adopt(pointer);
        raw std.convert::range_error* returned = core::release(move owner);
        returned as void;
    }
}

std.env::env_error copy_017(std.env::env_error value, std.env::env_error* output) {
    *output = value;
    return value;
}

void round_trip_017(raw std.env::env_error* pointer) {
    unsafe {
        own std.env::env_error* owner = core::adopt(pointer);
        raw std.env::env_error* returned = core::release(move owner);
        returned as void;
    }
}

std.env::error_code copy_018(std.env::error_code value, std.env::error_code* output) {
    *output = value;
    return value;
}

void round_trip_018(raw std.env::error_code* pointer) {
    unsafe {
        own std.env::error_code* owner = core::adopt(pointer);
        raw std.env::error_code* returned = core::release(move owner);
        returned as void;
    }
}

std.error::domain copy_019(std.error::domain value, std.error::domain* output) {
    *output = value;
    return value;
}

void round_trip_019(raw std.error::domain* pointer) {
    unsafe {
        own std.error::domain* owner = core::adopt(pointer);
        raw std.error::domain* returned = core::release(move owner);
        returned as void;
    }
}

std.error::error copy_020(std.error::error value, std.error::error* output) {
    *output = value;
    return value;
}

void round_trip_020(raw std.error::error* pointer) {
    unsafe {
        own std.error::error* owner = core::adopt(pointer);
        raw std.error::error* returned = core::release(move owner);
        returned as void;
    }
}

std.format::format_error copy_021(std.format::format_error value, std.format::format_error* output) {
    *output = value;
    return value;
}

void round_trip_021(raw std.format::format_error* pointer) {
    unsafe {
        own std.format::format_error* owner = core::adopt(pointer);
        raw std.format::format_error* returned = core::release(move owner);
        returned as void;
    }
}

std.fs::access copy_022(std.fs::access value, std.fs::access* output) {
    *output = value;
    return value;
}

void round_trip_022(raw std.fs::access* pointer) {
    unsafe {
        own std.fs::access* owner = core::adopt(pointer);
        raw std.fs::access* returned = core::release(move owner);
        returned as void;
    }
}

std.fs::create_mode copy_023(std.fs::create_mode value, std.fs::create_mode* output) {
    *output = value;
    return value;
}

void round_trip_023(raw std.fs::create_mode* pointer) {
    unsafe {
        own std.fs::create_mode* owner = core::adopt(pointer);
        raw std.fs::create_mode* returned = core::release(move owner);
        returned as void;
    }
}

std.fs::error_code copy_024(std.fs::error_code value, std.fs::error_code* output) {
    *output = value;
    return value;
}

void round_trip_024(raw std.fs::error_code* pointer) {
    unsafe {
        own std.fs::error_code* owner = core::adopt(pointer);
        raw std.fs::error_code* returned = core::release(move owner);
        returned as void;
    }
}

std.fs::file_kind copy_025(std.fs::file_kind value, std.fs::file_kind* output) {
    *output = value;
    return value;
}

void round_trip_025(raw std.fs::file_kind* pointer) {
    unsafe {
        own std.fs::file_kind* owner = core::adopt(pointer);
        raw std.fs::file_kind* returned = core::release(move owner);
        returned as void;
    }
}

std.fs::fs_error copy_026(std.fs::fs_error value, std.fs::fs_error* output) {
    *output = value;
    return value;
}

void round_trip_026(raw std.fs::fs_error* pointer) {
    unsafe {
        own std.fs::fs_error* owner = core::adopt(pointer);
        raw std.fs::fs_error* returned = core::release(move owner);
        returned as void;
    }
}

std.fs::lock_kind copy_027(std.fs::lock_kind value, std.fs::lock_kind* output) {
    *output = value;
    return value;
}

void round_trip_027(raw std.fs::lock_kind* pointer) {
    unsafe {
        own std.fs::lock_kind* owner = core::adopt(pointer);
        raw std.fs::lock_kind* returned = core::release(move owner);
        returned as void;
    }
}

std.fs::metadata copy_028(std.fs::metadata value, std.fs::metadata* output) {
    *output = value;
    return value;
}

void round_trip_028(raw std.fs::metadata* pointer) {
    unsafe {
        own std.fs::metadata* owner = core::adopt(pointer);
        raw std.fs::metadata* returned = core::release(move owner);
        returned as void;
    }
}

std.fs::open_file_options copy_029(std.fs::open_file_options value, std.fs::open_file_options* output) {
    *output = value;
    return value;
}

void round_trip_029(raw std.fs::open_file_options* pointer) {
    unsafe {
        own std.fs::open_file_options* owner = core::adopt(pointer);
        raw std.fs::open_file_options* returned = core::release(move owner);
        returned as void;
    }
}

std.fs::path_error copy_030(std.fs::path_error value, std.fs::path_error* output) {
    *output = value;
    return value;
}

void round_trip_030(raw std.fs::path_error* pointer) {
    unsafe {
        own std.fs::path_error* owner = core::adopt(pointer);
        raw std.fs::path_error* returned = core::release(move owner);
        returned as void;
    }
}

std.fs::seek_origin copy_031(std.fs::seek_origin value, std.fs::seek_origin* output) {
    *output = value;
    return value;
}

void round_trip_031(raw std.fs::seek_origin* pointer) {
    unsafe {
        own std.fs::seek_origin* owner = core::adopt(pointer);
        raw std.fs::seek_origin* returned = core::release(move owner);
        returned as void;
    }
}

std.fs::sync_level copy_032(std.fs::sync_level value, std.fs::sync_level* output) {
    *output = value;
    return value;
}

void round_trip_032(raw std.fs::sync_level* pointer) {
    unsafe {
        own std.fs::sync_level* owner = core::adopt(pointer);
        raw std.fs::sync_level* returned = core::release(move owner);
        returned as void;
    }
}

std.hash::md5_digest copy_033(std.hash::md5_digest value, std.hash::md5_digest* output) {
    *output = value;
    return value;
}

void round_trip_033(raw std.hash::md5_digest* pointer) {
    unsafe {
        own std.hash::md5_digest* owner = core::adopt(pointer);
        raw std.hash::md5_digest* returned = core::release(move owner);
        returned as void;
    }
}

std.hash::sha1_digest copy_034(std.hash::sha1_digest value, std.hash::sha1_digest* output) {
    *output = value;
    return value;
}

void round_trip_034(raw std.hash::sha1_digest* pointer) {
    unsafe {
        own std.hash::sha1_digest* owner = core::adopt(pointer);
        raw std.hash::sha1_digest* returned = core::release(move owner);
        returned as void;
    }
}

std.hash::sha256_digest copy_035(std.hash::sha256_digest value, std.hash::sha256_digest* output) {
    *output = value;
    return value;
}

void round_trip_035(raw std.hash::sha256_digest* pointer) {
    unsafe {
        own std.hash::sha256_digest* owner = core::adopt(pointer);
        raw std.hash::sha256_digest* returned = core::release(move owner);
        returned as void;
    }
}

std.hash::sha512_digest copy_036(std.hash::sha512_digest value, std.hash::sha512_digest* output) {
    *output = value;
    return value;
}

void round_trip_036(raw std.hash::sha512_digest* pointer) {
    unsafe {
        own std.hash::sha512_digest* owner = core::adopt(pointer);
        raw std.hash::sha512_digest* returned = core::release(move owner);
        returned as void;
    }
}

std.io::error_code copy_037(std.io::error_code value, std.io::error_code* output) {
    *output = value;
    return value;
}

void round_trip_037(raw std.io::error_code* pointer) {
    unsafe {
        own std.io::error_code* owner = core::adopt(pointer);
        raw std.io::error_code* returned = core::release(move owner);
        returned as void;
    }
}

std.io::io_error copy_038(std.io::io_error value, std.io::io_error* output) {
    *output = value;
    return value;
}

void round_trip_038(raw std.io::io_error* pointer) {
    unsafe {
        own std.io::io_error* owner = core::adopt(pointer);
        raw std.io::io_error* returned = core::release(move owner);
        returned as void;
    }
}

std.json::error_code copy_039(std.json::error_code value, std.json::error_code* output) {
    *output = value;
    return value;
}

void round_trip_039(raw std.json::error_code* pointer) {
    unsafe {
        own std.json::error_code* owner = core::adopt(pointer);
        raw std.json::error_code* returned = core::release(move owner);
        returned as void;
    }
}

std.json::feed_result copy_040(std.json::feed_result value, std.json::feed_result* output) {
    *output = value;
    return value;
}

void round_trip_040(raw std.json::feed_result* pointer) {
    unsafe {
        own std.json::feed_result* owner = core::adopt(pointer);
        raw std.json::feed_result* returned = core::release(move owner);
        returned as void;
    }
}

std.json::feed_state copy_041(std.json::feed_state value, std.json::feed_state* output) {
    *output = value;
    return value;
}

void round_trip_041(raw std.json::feed_state* pointer) {
    unsafe {
        own std.json::feed_state* owner = core::adopt(pointer);
        raw std.json::feed_state* returned = core::release(move owner);
        returned as void;
    }
}

std.json::mode copy_042(std.json::mode value, std.json::mode* output) {
    *output = value;
    return value;
}

void round_trip_042(raw std.json::mode* pointer) {
    unsafe {
        own std.json::mode* owner = core::adopt(pointer);
        raw std.json::mode* returned = core::release(move owner);
        returned as void;
    }
}

std.json::options copy_043(std.json::options value, std.json::options* output) {
    *output = value;
    return value;
}

void round_trip_043(raw std.json::options* pointer) {
    unsafe {
        own std.json::options* owner = core::adopt(pointer);
        raw std.json::options* returned = core::release(move owner);
        returned as void;
    }
}

std.json::value_kind copy_044(std.json::value_kind value, std.json::value_kind* output) {
    *output = value;
    return value;
}

void round_trip_044(raw std.json::value_kind* pointer) {
    unsafe {
        own std.json::value_kind* owner = core::adopt(pointer);
        raw std.json::value_kind* returned = core::release(move owner);
        returned as void;
    }
}

std.math::binary_parts_c_double copy_045(std.math::binary_parts_c_double value, std.math::binary_parts_c_double* output) {
    *output = value;
    return value;
}

void round_trip_045(raw std.math::binary_parts_c_double* pointer) {
    unsafe {
        own std.math::binary_parts_c_double* owner = core::adopt(pointer);
        raw std.math::binary_parts_c_double* returned = core::release(move owner);
        returned as void;
    }
}

std.math::binary_parts_c_float copy_046(std.math::binary_parts_c_float value, std.math::binary_parts_c_float* output) {
    *output = value;
    return value;
}

void round_trip_046(raw std.math::binary_parts_c_float* pointer) {
    unsafe {
        own std.math::binary_parts_c_float* owner = core::adopt(pointer);
        raw std.math::binary_parts_c_float* returned = core::release(move owner);
        returned as void;
    }
}

std.math::binary_parts_c_long_double copy_047(std.math::binary_parts_c_long_double value, std.math::binary_parts_c_long_double* output) {
    *output = value;
    return value;
}

void round_trip_047(raw std.math::binary_parts_c_long_double* pointer) {
    unsafe {
        own std.math::binary_parts_c_long_double* owner = core::adopt(pointer);
        raw std.math::binary_parts_c_long_double* returned = core::release(move owner);
        returned as void;
    }
}

std.math::binary_parts_f32 copy_048(std.math::binary_parts_f32 value, std.math::binary_parts_f32* output) {
    *output = value;
    return value;
}

void round_trip_048(raw std.math::binary_parts_f32* pointer) {
    unsafe {
        own std.math::binary_parts_f32* owner = core::adopt(pointer);
        raw std.math::binary_parts_f32* returned = core::release(move owner);
        returned as void;
    }
}

std.math::binary_parts_f64 copy_049(std.math::binary_parts_f64 value, std.math::binary_parts_f64* output) {
    *output = value;
    return value;
}

void round_trip_049(raw std.math::binary_parts_f64* pointer) {
    unsafe {
        own std.math::binary_parts_f64* owner = core::adopt(pointer);
        raw std.math::binary_parts_f64* returned = core::release(move owner);
        returned as void;
    }
}

std.math::complex_f32 copy_050(std.math::complex_f32 value, std.math::complex_f32* output) {
    *output = value;
    return value;
}

void round_trip_050(raw std.math::complex_f32* pointer) {
    unsafe {
        own std.math::complex_f32* owner = core::adopt(pointer);
        raw std.math::complex_f32* returned = core::release(move owner);
        returned as void;
    }
}

std.math::complex_f64 copy_051(std.math::complex_f64 value, std.math::complex_f64* output) {
    *output = value;
    return value;
}

void round_trip_051(raw std.math::complex_f64* pointer) {
    unsafe {
        own std.math::complex_f64* owner = core::adopt(pointer);
        raw std.math::complex_f64* returned = core::release(move owner);
        returned as void;
    }
}

std.math::error_code copy_052(std.math::error_code value, std.math::error_code* output) {
    *output = value;
    return value;
}

void round_trip_052(raw std.math::error_code* pointer) {
    unsafe {
        own std.math::error_code* owner = core::adopt(pointer);
        raw std.math::error_code* returned = core::release(move owner);
        returned as void;
    }
}

std.math::fraction_parts_c_double copy_053(std.math::fraction_parts_c_double value, std.math::fraction_parts_c_double* output) {
    *output = value;
    return value;
}

void round_trip_053(raw std.math::fraction_parts_c_double* pointer) {
    unsafe {
        own std.math::fraction_parts_c_double* owner = core::adopt(pointer);
        raw std.math::fraction_parts_c_double* returned = core::release(move owner);
        returned as void;
    }
}

std.math::fraction_parts_c_float copy_054(std.math::fraction_parts_c_float value, std.math::fraction_parts_c_float* output) {
    *output = value;
    return value;
}

void round_trip_054(raw std.math::fraction_parts_c_float* pointer) {
    unsafe {
        own std.math::fraction_parts_c_float* owner = core::adopt(pointer);
        raw std.math::fraction_parts_c_float* returned = core::release(move owner);
        returned as void;
    }
}

std.math::fraction_parts_c_long_double copy_055(std.math::fraction_parts_c_long_double value, std.math::fraction_parts_c_long_double* output) {
    *output = value;
    return value;
}

void round_trip_055(raw std.math::fraction_parts_c_long_double* pointer) {
    unsafe {
        own std.math::fraction_parts_c_long_double* owner = core::adopt(pointer);
        raw std.math::fraction_parts_c_long_double* returned = core::release(move owner);
        returned as void;
    }
}

std.math::fraction_parts_f32 copy_056(std.math::fraction_parts_f32 value, std.math::fraction_parts_f32* output) {
    *output = value;
    return value;
}

void round_trip_056(raw std.math::fraction_parts_f32* pointer) {
    unsafe {
        own std.math::fraction_parts_f32* owner = core::adopt(pointer);
        raw std.math::fraction_parts_f32* returned = core::release(move owner);
        returned as void;
    }
}

std.math::fraction_parts_f64 copy_057(std.math::fraction_parts_f64 value, std.math::fraction_parts_f64* output) {
    *output = value;
    return value;
}

void round_trip_057(raw std.math::fraction_parts_f64* pointer) {
    unsafe {
        own std.math::fraction_parts_f64* owner = core::adopt(pointer);
        raw std.math::fraction_parts_f64* returned = core::release(move owner);
        returned as void;
    }
}

std.math::math_error copy_058(std.math::math_error value, std.math::math_error* output) {
    *output = value;
    return value;
}

void round_trip_058(raw std.math::math_error* pointer) {
    unsafe {
        own std.math::math_error* owner = core::adopt(pointer);
        raw std.math::math_error* returned = core::release(move owner);
        returned as void;
    }
}

std.net::address_error copy_059(std.net::address_error value, std.net::address_error* output) {
    *output = value;
    return value;
}

void round_trip_059(raw std.net::address_error* pointer) {
    unsafe {
        own std.net::address_error* owner = core::adopt(pointer);
        raw std.net::address_error* returned = core::release(move owner);
        returned as void;
    }
}

std.net::address_error_code copy_060(std.net::address_error_code value, std.net::address_error_code* output) {
    *output = value;
    return value;
}

void round_trip_060(raw std.net::address_error_code* pointer) {
    unsafe {
        own std.net::address_error_code* owner = core::adopt(pointer);
        raw std.net::address_error_code* returned = core::release(move owner);
        returned as void;
    }
}

std.net::datagram copy_061(std.net::datagram value, std.net::datagram* output) {
    *output = value;
    return value;
}

void round_trip_061(raw std.net::datagram* pointer) {
    unsafe {
        own std.net::datagram* owner = core::adopt(pointer);
        raw std.net::datagram* returned = core::release(move owner);
        returned as void;
    }
}

std.net::error_code copy_062(std.net::error_code value, std.net::error_code* output) {
    *output = value;
    return value;
}

void round_trip_062(raw std.net::error_code* pointer) {
    unsafe {
        own std.net::error_code* owner = core::adopt(pointer);
        raw std.net::error_code* returned = core::release(move owner);
        returned as void;
    }
}

std.net::family copy_063(std.net::family value, std.net::family* output) {
    *output = value;
    return value;
}

void round_trip_063(raw std.net::family* pointer) {
    unsafe {
        own std.net::family* owner = core::adopt(pointer);
        raw std.net::family* returned = core::release(move owner);
        returned as void;
    }
}

std.net::ip_address copy_064(std.net::ip_address value, std.net::ip_address* output) {
    *output = value;
    return value;
}

void round_trip_064(raw std.net::ip_address* pointer) {
    unsafe {
        own std.net::ip_address* owner = core::adopt(pointer);
        raw std.net::ip_address* returned = core::release(move owner);
        returned as void;
    }
}

std.net::listen_options copy_065(std.net::listen_options value, std.net::listen_options* output) {
    *output = value;
    return value;
}

void round_trip_065(raw std.net::listen_options* pointer) {
    unsafe {
        own std.net::listen_options* owner = core::adopt(pointer);
        raw std.net::listen_options* returned = core::release(move owner);
        returned as void;
    }
}

std.net::net_error copy_066(std.net::net_error value, std.net::net_error* output) {
    *output = value;
    return value;
}

void round_trip_066(raw std.net::net_error* pointer) {
    unsafe {
        own std.net::net_error* owner = core::adopt(pointer);
        raw std.net::net_error* returned = core::release(move owner);
        returned as void;
    }
}

std.net::peer_credentials copy_067(std.net::peer_credentials value, std.net::peer_credentials* output) {
    *output = value;
    return value;
}

void round_trip_067(raw std.net::peer_credentials* pointer) {
    unsafe {
        own std.net::peer_credentials* owner = core::adopt(pointer);
        raw std.net::peer_credentials* returned = core::release(move owner);
        returned as void;
    }
}

std.net::shutdown_direction copy_068(std.net::shutdown_direction value, std.net::shutdown_direction* output) {
    *output = value;
    return value;
}

void round_trip_068(raw std.net::shutdown_direction* pointer) {
    unsafe {
        own std.net::shutdown_direction* owner = core::adopt(pointer);
        raw std.net::shutdown_direction* returned = core::release(move owner);
        returned as void;
    }
}

std.net::socket_address copy_069(std.net::socket_address value, std.net::socket_address* output) {
    *output = value;
    return value;
}

void round_trip_069(raw std.net::socket_address* pointer) {
    unsafe {
        own std.net::socket_address* owner = core::adopt(pointer);
        raw std.net::socket_address* returned = core::release(move owner);
        returned as void;
    }
}

std.net::tcp_options copy_070(std.net::tcp_options value, std.net::tcp_options* output) {
    *output = value;
    return value;
}

void round_trip_070(raw std.net::tcp_options* pointer) {
    unsafe {
        own std.net::tcp_options* owner = core::adopt(pointer);
        raw std.net::tcp_options* returned = core::release(move owner);
        returned as void;
    }
}

std.net::udp_options copy_071(std.net::udp_options value, std.net::udp_options* output) {
    *output = value;
    return value;
}

void round_trip_071(raw std.net::udp_options* pointer) {
    unsafe {
        own std.net::udp_options* owner = core::adopt(pointer);
        raw std.net::udp_options* returned = core::release(move owner);
        returned as void;
    }
}

std.net::unix_message copy_072(std.net::unix_message value, std.net::unix_message* output) {
    *output = value;
    return value;
}

void round_trip_072(raw std.net::unix_message* pointer) {
    unsafe {
        own std.net::unix_message* owner = core::adopt(pointer);
        raw std.net::unix_message* returned = core::release(move owner);
        returned as void;
    }
}

std.process::error_code copy_073(std.process::error_code value, std.process::error_code* output) {
    *output = value;
    return value;
}

void round_trip_073(raw std.process::error_code* pointer) {
    unsafe {
        own std.process::error_code* owner = core::adopt(pointer);
        raw std.process::error_code* returned = core::release(move owner);
        returned as void;
    }
}

std.process::exit_status copy_074(std.process::exit_status value, std.process::exit_status* output) {
    *output = value;
    return value;
}

void round_trip_074(raw std.process::exit_status* pointer) {
    unsafe {
        own std.process::exit_status* owner = core::adopt(pointer);
        raw std.process::exit_status* returned = core::release(move owner);
        returned as void;
    }
}

std.process::pipe_mode copy_075(std.process::pipe_mode value, std.process::pipe_mode* output) {
    *output = value;
    return value;
}

void round_trip_075(raw std.process::pipe_mode* pointer) {
    unsafe {
        own std.process::pipe_mode* owner = core::adopt(pointer);
        raw std.process::pipe_mode* returned = core::release(move owner);
        returned as void;
    }
}

std.process::process_error copy_076(std.process::process_error value, std.process::process_error* output) {
    *output = value;
    return value;
}

void round_trip_076(raw std.process::process_error* pointer) {
    unsafe {
        own std.process::process_error* owner = core::adopt(pointer);
        raw std.process::process_error* returned = core::release(move owner);
        returned as void;
    }
}

std.process::stdio copy_077(std.process::stdio value, std.process::stdio* output) {
    *output = value;
    return value;
}

void round_trip_077(raw std.process::stdio* pointer) {
    unsafe {
        own std.process::stdio* owner = core::adopt(pointer);
        raw std.process::stdio* returned = core::release(move owner);
        returned as void;
    }
}

std.process::termination_kind copy_078(std.process::termination_kind value, std.process::termination_kind* output) {
    *output = value;
    return value;
}

void round_trip_078(raw std.process::termination_kind* pointer) {
    unsafe {
        own std.process::termination_kind* owner = core::adopt(pointer);
        raw std.process::termination_kind* returned = core::release(move owner);
        returned as void;
    }
}

std.signal::kind copy_079(std.signal::kind value, std.signal::kind* output) {
    *output = value;
    return value;
}

void round_trip_079(raw std.signal::kind* pointer) {
    unsafe {
        own std.signal::kind* owner = core::adopt(pointer);
        raw std.signal::kind* returned = core::release(move owner);
        returned as void;
    }
}

std.string::boundary_error copy_080(std.string::boundary_error value, std.string::boundary_error* output) {
    *output = value;
    return value;
}

void round_trip_080(raw std.string::boundary_error* pointer) {
    unsafe {
        own std.string::boundary_error* owner = core::adopt(pointer);
        raw std.string::boundary_error* returned = core::release(move owner);
        returned as void;
    }
}

std.string::string_error copy_081(std.string::string_error value, std.string::string_error* output) {
    *output = value;
    return value;
}

void round_trip_081(raw std.string::string_error* pointer) {
    unsafe {
        own std.string::string_error* owner = core::adopt(pointer);
        raw std.string::string_error* returned = core::release(move owner);
        returned as void;
    }
}

std.sync::barrier_error copy_082(std.sync::barrier_error value, std.sync::barrier_error* output) {
    *output = value;
    return value;
}

void round_trip_082(raw std.sync::barrier_error* pointer) {
    unsafe {
        own std.sync::barrier_error* owner = core::adopt(pointer);
        raw std.sync::barrier_error* returned = core::release(move owner);
        returned as void;
    }
}

std.sync::barrier_wait_result copy_083(std.sync::barrier_wait_result value, std.sync::barrier_wait_result* output) {
    *output = value;
    return value;
}

void round_trip_083(raw std.sync::barrier_wait_result* pointer) {
    unsafe {
        own std.sync::barrier_wait_result* owner = core::adopt(pointer);
        raw std.sync::barrier_wait_result* returned = core::release(move owner);
        returned as void;
    }
}

std.thread::thread_error copy_084(std.thread::thread_error value, std.thread::thread_error* output) {
    *output = value;
    return value;
}

void round_trip_084(raw std.thread::thread_error* pointer) {
    unsafe {
        own std.thread::thread_error* owner = core::adopt(pointer);
        raw std.thread::thread_error* returned = core::release(move owner);
        returned as void;
    }
}

std.time::duration copy_085(std.time::duration value, std.time::duration* output) {
    *output = value;
    return value;
}

void round_trip_085(raw std.time::duration* pointer) {
    unsafe {
        own std.time::duration* owner = core::adopt(pointer);
        raw std.time::duration* returned = core::release(move owner);
        returned as void;
    }
}

std.time::duration_error copy_086(std.time::duration_error value, std.time::duration_error* output) {
    *output = value;
    return value;
}

void round_trip_086(raw std.time::duration_error* pointer) {
    unsafe {
        own std.time::duration_error* owner = core::adopt(pointer);
        raw std.time::duration_error* returned = core::release(move owner);
        returned as void;
    }
}

std.time::error_code copy_087(std.time::error_code value, std.time::error_code* output) {
    *output = value;
    return value;
}

void round_trip_087(raw std.time::error_code* pointer) {
    unsafe {
        own std.time::error_code* owner = core::adopt(pointer);
        raw std.time::error_code* returned = core::release(move owner);
        returned as void;
    }
}

std.time::instant copy_088(std.time::instant value, std.time::instant* output) {
    *output = value;
    return value;
}

void round_trip_088(raw std.time::instant* pointer) {
    unsafe {
        own std.time::instant* owner = core::adopt(pointer);
        raw std.time::instant* returned = core::release(move owner);
        returned as void;
    }
}

std.time::system_time copy_089(std.time::system_time value, std.time::system_time* output) {
    *output = value;
    return value;
}

void round_trip_089(raw std.time::system_time* pointer) {
    unsafe {
        own std.time::system_time* owner = core::adopt(pointer);
        raw std.time::system_time* returned = core::release(move owner);
        returned as void;
    }
}

std.time::time_error copy_090(std.time::time_error value, std.time::time_error* output) {
    *output = value;
    return value;
}

void round_trip_090(raw std.time::time_error* pointer) {
    unsafe {
        own std.time::time_error* owner = core::adopt(pointer);
        raw std.time::time_error* returned = core::release(move owner);
        returned as void;
    }
}

std.time::utc_datetime copy_091(std.time::utc_datetime value, std.time::utc_datetime* output) {
    *output = value;
    return value;
}

void round_trip_091(raw std.time::utc_datetime* pointer) {
    unsafe {
        own std.time::utc_datetime* owner = core::adopt(pointer);
        raw std.time::utc_datetime* returned = core::release(move owner);
        returned as void;
    }
}

i32 main() {
    return 0;
}
