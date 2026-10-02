module example.unzip.bitstream;

import example.unzip.error::{ZipError, ZipErrorCode};

u32 read_zip_bits(const u8[] input,
    std.bits::lsb_reader* reader,
    u8 width) throws ZipError {
    throw (width > 32) ZipError {
        .code = ZipErrorCode::Internal,
        .offset = reader->byte_index,
        .message = "DEFLATE requested more than 32 bits",
    };
    try {
        u64 value = std.bits::read(input, reader, width);
        return value as u32;
    } catch (std.bits::read_error error) {
        throw (error.code == std.bits::read_error_code::invalid_width) ZipError {
            .code = ZipErrorCode::Internal,
            .offset = error.byte_index,
            .message = "invalid DEFLATE bit width",
        };
        throw ZipError {
            .code = ZipErrorCode::InvalidDeflate,
            .offset = error.byte_index,
            .message = "truncated DEFLATE bitstream",
        };
    }
}
