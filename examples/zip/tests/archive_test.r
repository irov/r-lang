module example.zip.tests.archive;

import example.zip.archive::{append_central, append_local, finish};
import example.zip.bytes::{create};
import example.zip.error::{ZipError, ZipErrorCode};
import example.zip.name::{conflicts};

protected u16 read_u16(const u8[] bytes, usize offset) {
    u16 low = bytes[offset] as u16;
    usize high_index = offset + 1;
    u16 high = bytes[high_index] as u16;
    return (low | (high << 8)) as u16;
}

protected u32 read_u32(const u8[] bytes, usize offset) {
    u32 b0 = bytes[offset] as u32;
    usize i1 = offset + 1;
    usize i2 = offset + 2;
    usize i3 = offset + 3;
    u32 b1 = bytes[i1] as u32;
    u32 b2 = bytes[i2] as u32;
    u32 b3 = bytes[i3] as u32;
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

protected void verify_empty_archive(
    bytes* output,
    const bytes* central) throws ZipError {
    finish(output, central, 0, 0);

    u8[] bytes = output->as_slice_mut();
    usize archive_size = len(bytes);
    throw (archive_size != 22) ZipError {
        .code = ZipErrorCode::Internal,
        .offset = archive_size,
        .message = "empty ZIP size mismatch",
    };
    u32 signature = read_u32(bytes, 0);
    u16 disk_entries = read_u16(bytes, 8);
    u16 total_entries = read_u16(bytes, 10);
    u32 central_size = read_u32(bytes, 12);
    u32 central_offset = read_u32(bytes, 16);
    u16 comment_size = read_u16(bytes, 20);
    throw ((signature != 0x0605_4b50) ||
           (disk_entries != 0) ||
           (total_entries != 0) ||
           (central_size != 0) ||
           (central_offset != 0) ||
           (comment_size != 0)) ZipError {
        .code = ZipErrorCode::Internal,
        .offset = signature as usize,
        .message = "empty ZIP end record mismatch",
    };
}

protected void empty_archive() throws ZipError {
    bytes output = create(32, "empty ZIP allocation failed");
    bytes central = create(0, "empty central allocation failed");
    verify_empty_archive(&output, &central);
}

protected void verify_stored_file(
    bytes* output,
    bytes* central) throws ZipError {
    u8[5] name_storage = {97, 46, 98, 105, 110};
    const u8[] name = name_storage[0..5];
    u8[4] payload = {0, 1, 2, 255};
    const u8[] contents = payload[0..4];
    u32 checksum = std.hash::crc32(contents);
    append_local(
        output,
        0,
        name,
        contents,
        checksum,
        false);

    u8[] local_bytes = output->as_slice_mut();
    usize archive_end = len(local_bytes);
    append_central(
        central,
        archive_end,
        0,
        0,
        name,
        4,
        checksum,
        false);

    finish(output, &(*central), archive_end, 1);

    u8[] bytes = output->as_slice_mut();
    usize archive_size = len(bytes);
    throw (archive_size != 112) ZipError {
        .code = ZipErrorCode::Internal,
        .offset = archive_size,
        .message = "stored ZIP size mismatch",
    };

    u32 local_signature = read_u32(bytes, 0);
    u16 local_method = read_u16(bytes, 8);
    u32 local_checksum = read_u32(bytes, 14);
    u32 local_compressed_size = read_u32(bytes, 18);
    u32 local_size = read_u32(bytes, 22);
    u16 local_name_size = read_u16(bytes, 26);
    throw ((local_signature != 0x0403_4b50) ||
           (local_method != 0) ||
           (local_checksum != checksum) ||
           (local_compressed_size != 4) ||
           (local_size != 4) ||
           (local_name_size != 5) ||
           (bytes[30] != 97) ||
           (bytes[31] != 46) ||
           (bytes[32] != 98) ||
           (bytes[33] != 105) ||
           (bytes[34] != 110) ||
           (bytes[35] != 0) ||
           (bytes[36] != 1) ||
           (bytes[37] != 2) ||
           (bytes[38] != 255)) ZipError {
        .code = ZipErrorCode::Internal,
        .offset = local_signature as usize,
        .message = "stored ZIP local record mismatch",
    };

    u32 central_signature = read_u32(bytes, 39);
    u16 central_method = read_u16(bytes, 49);
    u32 central_checksum = read_u32(bytes, 55);
    u32 central_compressed_size = read_u32(bytes, 59);
    u32 central_file_size = read_u32(bytes, 63);
    u16 central_name_size = read_u16(bytes, 67);
    u32 local_offset = read_u32(bytes, 81);
    throw ((central_signature != 0x0201_4b50) ||
           (central_method != 0) ||
           (central_checksum != checksum) ||
           (central_compressed_size != 4) ||
           (central_file_size != 4) ||
           (central_name_size != 5) ||
           (local_offset != 0) ||
           (bytes[85] != 97) ||
           (bytes[86] != 46) ||
           (bytes[87] != 98) ||
           (bytes[88] != 105) ||
           (bytes[89] != 110)) ZipError {
        .code = ZipErrorCode::Internal,
        .offset = central_signature as usize,
        .message = "stored ZIP central record mismatch",
    };

    u32 end_signature = read_u32(bytes, 90);
    u16 disk_entries = read_u16(bytes, 98);
    u16 total_entries = read_u16(bytes, 100);
    u32 central_size = read_u32(bytes, 102);
    u32 central_offset = read_u32(bytes, 106);
    u16 comment_size = read_u16(bytes, 110);
    throw ((end_signature != 0x0605_4b50) ||
           (disk_entries != 1) ||
           (total_entries != 1) ||
           (central_size != 51) ||
           (central_offset != 39) ||
           (comment_size != 0)) ZipError {
        .code = ZipErrorCode::Internal,
        .offset = end_signature as usize,
        .message = "stored ZIP end record mismatch",
    };
}

protected void stored_file() throws ZipError {
    bytes output = create(128, "local allocation failed");
    bytes central = create(64, "central allocation failed");
    verify_stored_file(&output, &central);
}

protected void crc_all_bytes() throws ZipError {
    bytes values = create(256, "CRC fixture allocation failed");
    usize index = 0;
    while (index < 256) {
        u8 value = index as u8;
        try {
            std.bytes::append_u8(&values, value);
        } catch (std.alloc::alloc_error error) {
            error as void;
            throw ZipError {
                .code = ZipErrorCode::Allocation,
                .offset = index,
                .message = "CRC fixture allocation failed",
            };
        }
        index += 1;
    }
    const u8[] bytes = values.as_slice();
    u32 checksum = std.hash::crc32(bytes);
    throw (checksum != 0x2905_8c73) ZipError {
        .code = ZipErrorCode::Internal,
        .offset = checksum as usize,
        .message = "CRC32 vector mismatch",
    };
}

protected void name_conflicts() throws ZipError {
    constexpr str case_left_text = "Readme.txt";
    constexpr str case_right_text = "README.TXT";
    constexpr str parent_text = "assets";
    constexpr str child_text = "assets/icon.bin";
    str case_left = case_left_text;
    str case_right = case_right_text;
    str parent = parent_text;
    str child = child_text;
    bool case_collision = conflicts(case_left, case_right);
    bool parent_collision = conflicts(parent, child);
    throw ((case_collision == false) || (parent_collision == false)) ZipError {
        .code = ZipErrorCode::Internal,
        .offset = 0,
        .message = "ZIP name conflict detection mismatch",
    };
}

void run() throws ZipError {
    empty_archive();
    stored_file();
    crc_all_bytes();
    name_conflicts();
}
