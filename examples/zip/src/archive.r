module example.zip.archive;

import example.zip.bytes::{append, append_u16_le, append_u32_le};
import example.zip.error::{ZipError, ZipErrorCode};
import example.zip.model::{MAX_ARCHIVE_BYTES, MAX_ENTRY_BYTES};

protected const u32 LOCAL_SIGNATURE = 0x0403_4b50;
protected const u32 CENTRAL_SIGNATURE = 0x0201_4b50;
protected const u32 EOCD_SIGNATURE = 0x0605_4b50;
protected const u16 VERSION_MADE_BY = 0x0314;
protected const u16 VERSION_NEEDED = 20;
protected const u16 UTF8_FLAG = 0x0800;
protected const u16 STORED_METHOD = 0;
protected const u16 DOS_TIME = 0;
protected const u16 DOS_DATE = 0x0021;
protected const u32 REGULAR_ATTRIBUTES = 0x81a4_0000;
protected const u32 DIRECTORY_ATTRIBUTES = 0x41ed_0010;

protected bool can_add(usize current, usize amount) {
    if (current > MAX_ARCHIVE_BYTES) {
        return false;
    }
    usize remaining = MAX_ARCHIVE_BYTES - current;
    return amount <= remaining;
}

void append_local(bytes* output,
    usize archive_size,
    const u8[] name,
    const u8[] contents,
    u32 checksum,
    bool directory) throws ZipError {
    usize name_size = len(name);
    usize data_size = len(contents);
    throw ((name_size == 0) || (name_size > 65_535)) ZipError {
        .code = ZipErrorCode::UnsafePath,
        .offset = name_size,
        .message = "ZIP entry name does not fit ZIP32",
    };
    throw (data_size > MAX_ENTRY_BYTES) ZipError {
        .code = ZipErrorCode::LimitExceeded,
        .offset = data_size,
        .message = "input file exceeds the per-entry limit",
    };
    throw ((directory == true) && (data_size != 0)) ZipError {
        .code = ZipErrorCode::Internal,
        .offset = data_size,
        .message = "directory entry carries file contents",
    };

    usize local_record_size = 30 + name_size + data_size;
    bool local_fits = can_add(archive_size, local_record_size);
    throw (local_fits == false) ZipError {
        .code = ZipErrorCode::LimitExceeded,
        .offset = archive_size,
        .message = "ZIP archive exceeds its configured limit",
    };

    u16 name_size16 = name_size as u16;
    u32 data_size32 = data_size as u32;

    append_u32_le(output, LOCAL_SIGNATURE);
    append_u16_le(output, VERSION_NEEDED);
    append_u16_le(output, UTF8_FLAG);
    append_u16_le(output, STORED_METHOD);
    append_u16_le(output, DOS_TIME);
    append_u16_le(output, DOS_DATE);
    append_u32_le(output, checksum);
    append_u32_le(output, data_size32);
    append_u32_le(output, data_size32);
    append_u16_le(output, name_size16);
    append_u16_le(output, 0);
    append(output, name);
    append(output, contents);
}

void append_central(bytes* central,
    usize archive_end,
    usize central_size,
    usize local_offset,
    const u8[] name,
    usize data_size,
    u32 checksum,
    bool directory) throws ZipError {
    usize name_size = len(name);
    throw ((name_size == 0) || (name_size > 65_535) ||
           (data_size > MAX_ENTRY_BYTES)) ZipError {
        .code = ZipErrorCode::LimitExceeded,
        .offset = name_size,
        .message = "central directory entry does not fit ZIP32",
    };
    usize central_record_size = 46 + name_size;
    bool central_fits = can_add(archive_end, central_size);
    usize combined = archive_end;
    if (central_fits == true) {
        combined += central_size;
        central_fits = can_add(combined, central_record_size);
    }
    if (central_fits == true) {
        combined += central_record_size;
        central_fits = can_add(combined, 22);
    }
    throw (central_fits == false) ZipError {
        .code = ZipErrorCode::LimitExceeded,
        .offset = combined,
        .message = "ZIP central directory exceeds its configured limit",
    };

    u16 name_size16 = name_size as u16;
    u32 data_size32 = data_size as u32;
    u32 local_offset32 = local_offset as u32;
    u32 attributes = REGULAR_ATTRIBUTES;
    if (directory == true) {
        attributes = DIRECTORY_ATTRIBUTES;
    }

    append_u32_le(central, CENTRAL_SIGNATURE);
    append_u16_le(central, VERSION_MADE_BY);
    append_u16_le(central, VERSION_NEEDED);
    append_u16_le(central, UTF8_FLAG);
    append_u16_le(central, STORED_METHOD);
    append_u16_le(central, DOS_TIME);
    append_u16_le(central, DOS_DATE);
    append_u32_le(central, checksum);
    append_u32_le(central, data_size32);
    append_u32_le(central, data_size32);
    append_u16_le(central, name_size16);
    append_u16_le(central, 0);
    append_u16_le(central, 0);
    append_u16_le(central, 0);
    append_u16_le(central, 0);
    append_u32_le(central, attributes);
    append_u32_le(central, local_offset32);
    append(central, name);
}

void finish(bytes* output,
    const bytes* central,
    usize central_offset,
    usize entry_count) throws ZipError {
    throw (entry_count > 65_535) ZipError {
        .code = ZipErrorCode::LimitExceeded,
        .offset = entry_count,
        .message = "entry count does not fit ZIP32",
    };
    const u8[] central_bytes = central->as_slice();
    usize central_size = len(central_bytes);
    bool central_fits = can_add(central_offset, central_size);
    throw (central_fits == false) ZipError {
        .code = ZipErrorCode::LimitExceeded,
        .offset = central_offset,
        .message = "central directory exceeds the archive limit",
    };
    usize after_central = central_offset + central_size;
    bool eocd_fits = can_add(after_central, 22);
    throw (eocd_fits == false) ZipError {
        .code = ZipErrorCode::LimitExceeded,
        .offset = after_central,
        .message = "ZIP end record exceeds the archive limit",
    };

    append(output, central_bytes);
    u16 count16 = entry_count as u16;
    u32 central_size32 = central_size as u32;
    u32 central_offset32 = central_offset as u32;
    append_u32_le(output, EOCD_SIGNATURE);
    append_u16_le(output, 0);
    append_u16_le(output, 0);
    append_u16_le(output, count16);
    append_u16_le(output, count16);
    append_u32_le(output, central_size32);
    append_u32_le(output, central_offset32);
    append_u16_le(output, 0);
}
