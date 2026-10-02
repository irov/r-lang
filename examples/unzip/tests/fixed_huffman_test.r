module example.unzip.tests.fixed_huffman;

import example.unzip.deflate::{decode};
import example.unzip.error::{ZipError};

bool fixed_huffman_decodes_and_matches_crc() {
    // Raw fixed-Huffman DEFLATE stream for "hello hello hello!".
    u8[11] compressed = {
        0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57,
        0xc8, 0x40, 0x90, 0x8a, 0x00,
    };
    u8[18] output = {};
    const u8[] input = &compressed;
    u8[] destination = &output;
    try {
        usize size = decode(input, destination, 18);
        if (size != 18) {
            return false;
        }
        const u8[] output_view = &output;
        u32 crc = std.hash::crc32(output_view);
        if (crc != 0x7336_857b) {
            return false;
        }
        return true;
    } catch (ZipError error) {
        error as void;
        return false;
    }
}
