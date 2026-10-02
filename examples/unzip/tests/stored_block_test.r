module example.unzip.tests.stored_block;

import example.unzip.deflate::{decode};
import example.unzip.error::{ZipError};

bool stored_block_decodes_and_matches_crc() {
    // Raw final stored DEFLATE block for "hello".
    u8[10] compressed = {
        0x01, 0x05, 0x00, 0xfa, 0xff,
        0x68, 0x65, 0x6c, 0x6c, 0x6f,
    };
    u8[5] output = {};
    const u8[] input = &compressed;
    u8[] destination = &output;
    try {
        usize size = decode(input, destination, 5);
        if (size != 5) {
            return false;
        }
        const u8[] output_view = &output;
        u32 crc = std.hash::crc32(output_view);
        if (crc != 0x3610_a686) {
            return false;
        }
        return true;
    } catch (ZipError error) {
        error as void;
        return false;
    }
}
