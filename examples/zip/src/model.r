module example.zip.model;

const usize MAX_ARCHIVE_BYTES = 33_554_432;
const usize MAX_ENTRY_BYTES = 16_777_216;
const usize MAX_ENTRIES = 1_024;
const usize MAX_PATH_BYTES = 4_096;
const usize INITIAL_ARCHIVE_CAPACITY = 4_096;
const usize INITIAL_CENTRAL_CAPACITY = 2_048;
