module example.unzip.tests.main;

import example.unzip.tests.deflate_validation::{rejects_trailing_bytes};
import example.unzip.tests.dynamic_huffman::{dynamic_huffman_decodes_and_matches_crc};
import example.unzip.tests.fixed_huffman::{fixed_huffman_decodes_and_matches_crc};
import example.unzip.tests.stored_block::{stored_block_decodes_and_matches_crc};

protected bool all_tests_pass() {
    bool stored = stored_block_decodes_and_matches_crc();
    if (stored == false) { return false; }
    bool fixed = fixed_huffman_decodes_and_matches_crc();
    if (fixed == false) { return false; }
    bool dynamic = dynamic_huffman_decodes_and_matches_crc();
    if (dynamic == false) { return false; }
    return rejects_trailing_bytes();
}

i32 main() {
    bool passed = all_tests_pass();
    if (passed == false) {
        return 1;
    }
    return 0;
}
