module example.unzip.tests.deflate_validation;

import example.unzip.deflate::{decode};
import example.unzip.error::{ZipError, ZipErrorCode};

bool rejects_trailing_bytes() {
    u8[11] compressed = {
        0x01, 0x05, 0x00, 0xfa, 0xff,
        0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x00,
    };
    u8[5] output = {};
    const u8[] input = &compressed;
    u8[] destination = &output;
    try {
        usize size = decode(input, destination, 5);
        size as void;
        return false;
    } catch (ZipError error) {
        if (error.code != ZipErrorCode::InvalidDeflate) {
            return false;
        }
        if (error.offset != 10) {
            return false;
        }
        return true;
    }
}
