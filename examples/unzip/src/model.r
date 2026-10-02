module example.unzip.model;

const usize MAX_ARCHIVE_BYTES = 33_554_432;
const usize MAX_ENTRY_BYTES = 16_777_216;
const usize MAX_TOTAL_BYTES = 67_108_864;
const usize MAX_ENTRIES = 1_024;
const usize MAX_PATH_BYTES = 4_096;
const usize MAX_COMPRESSION_RATIO = 200;

struct ArchiveImage {
    bytes bytes;
};

struct Entry {
    usize record_offset;
    usize record_end;
    usize data_offset;
    usize compressed_size;
    usize uncompressed_size;
    u32 crc32;
    u16 method;
    usize name_offset;
    usize name_len;
    bool is_directory;
};

struct Catalog {
    Entry[MAX_ENTRIES] entries;
    usize count;
    usize total_uncompressed;
};

struct Range {
    usize begin;
    usize end;
};

struct Scratch {
    u8[MAX_ENTRY_BYTES] bytes;
};
