module example.unzip.zip;

import example.unzip.bytes::{has_span, read_u16_le, read_u32_le, ranges_equal};
import example.unzip.error::{ZipError, ZipErrorCode};
import example.unzip.model::{Catalog, Entry, MAX_COMPRESSION_RATIO, MAX_ENTRIES,
                            MAX_ENTRY_BYTES, MAX_TOTAL_BYTES};
import example.unzip.path::{validate, same_identity, is_parent_path};

protected const u32 EOCD_SIGNATURE = 0x0605_4b50;
protected const u32 CENTRAL_SIGNATURE = 0x0201_4b50;
protected const u32 LOCAL_SIGNATURE = 0x0403_4b50;
protected const u32 DESCRIPTOR_SIGNATURE = 0x0807_4b50;
protected const usize EOCD_MIN_SIZE = 22;
protected const usize EOCD_SEARCH_WINDOW = 65_557;

protected usize find_eocd(const u8[] archive) throws ZipError {
    usize size = len(archive);
    throw (size < EOCD_MIN_SIZE) ZipError {
        .code = ZipErrorCode::Truncated,
        .offset = size,
        .message = "file is shorter than ZIP end record",
    };

    usize position = size - EOCD_MIN_SIZE;
    usize minimum = 0;
    if (size > EOCD_SEARCH_WINDOW) {
        minimum = size - EOCD_SEARCH_WINDOW;
    }

    while (true) {
        u32 signature = read_u32_le(archive, position);
        if (signature == EOCD_SIGNATURE) {
            usize comment_field = position + 20;
            u16 comment_size16 = read_u16_le(archive, comment_field);
            usize comment_size = comment_size16 as usize;
            usize after_fixed = position + EOCD_MIN_SIZE;
            usize remaining = size - after_fixed;
            if (comment_size == remaining) {
                return position;
            }
        }

        if (position == minimum) {
            break;
        }
        position -= 1;
    }

    throw ZipError {
        .code = ZipErrorCode::InvalidZip,
        .offset = minimum,
        .message = "ZIP end record was not found",
    };
}

protected bool is_directory_entry(const u8[] name, u16 version_made_by, u32 external_attributes) {
    usize name_size = len(name);
    bool trailing_slash = false;
    if (name_size > 0) {
        usize last = name_size - 1;
        trailing_slash = name[last] == 47;
    }

    u16 host = (version_made_by >> 8) as u16;
    bool unix_directory = false;
    if (host == 3) {
        u32 mode = (external_attributes >> 16) & 0xffff;
        u32 kind = mode & 0xf000;
        unix_directory = kind == 0x4000;
    }
    bool dos_directory = (external_attributes & 0x10) != 0;
    bool result = trailing_slash || unix_directory || dos_directory;
    return result;
}

protected void validate_kind(u16 version_made_by,
    u32 external_attributes,
    usize offset) throws ZipError {
    u16 host = (version_made_by >> 8) as u16;
    if (host == 3) {
        u32 mode = (external_attributes >> 16) & 0xffff;
        u32 kind = mode & 0xf000;
        throw (kind == 0xa000) ZipError {
            .code = ZipErrorCode::UnsafePath,
            .offset = offset,
            .message = "symbolic links are not extracted",
        };
        bool ordinary = (kind == 0) || (kind == 0x4000) || (kind == 0x8000);
        throw (ordinary == false) ZipError {
            .code = ZipErrorCode::UnsupportedMethod,
            .offset = offset,
            .message = "special filesystem entry is not supported",
        };
    }

}

protected bool exceeds_ratio(usize compressed_size, usize uncompressed_size) {
    if (uncompressed_size == 0) {
        return false;
    }
    if (compressed_size == 0) {
        return true;
    }

    usize quotient = uncompressed_size / MAX_COMPRESSION_RATIO;
    usize remainder = uncompressed_size % MAX_COMPRESSION_RATIO;
    usize minimum_compressed = quotient;
    if (remainder != 0) {
        minimum_compressed += 1;
    }
    return compressed_size < minimum_compressed;
}

protected usize descriptor_end(const u8[] archive,
    usize limit,
    usize offset,
    u32 expected_crc,
    u32 expected_compressed,
    u32 expected_uncompressed) throws ZipError {
    bool base_span = has_span(limit, offset, 12);
    throw (base_span == false) ZipError {
        .code = ZipErrorCode::Truncated,
        .offset = offset,
        .message = "ZIP data descriptor is truncated",
    };

    u32 first = read_u32_le(archive, offset);
    usize second_offset = offset + 4;
    usize third_offset = offset + 8;
    u32 second = read_u32_le(archive, second_offset);
    u32 third = read_u32_le(archive, third_offset);

    if (first == DESCRIPTOR_SIGNATURE) {
        bool signed_span = has_span(limit, offset, 16);
        if (signed_span == true) {
            usize fourth_offset = offset + 12;
            if ((second == expected_crc) && (third == expected_compressed) &&
                (read_u32_le(archive, fourth_offset) == expected_uncompressed)) {
                usize end = offset + 16;
                return end;
            }
        }
    }

    throw (first != expected_crc || second != expected_compressed ||
           third != expected_uncompressed) ZipError {
        .code = ZipErrorCode::InvalidZip,
        .offset = offset,
        .message = "ZIP data descriptor disagrees with central directory",
    };

    usize end = offset + 12;
    return end;
}

protected void validate_catalog_conflicts(const u8[] archive,
    const Catalog* catalog,
    const Entry* candidate) throws ZipError {
    usize candidate_name_end = candidate->name_offset + candidate->name_len;
    const u8[] candidate_name =
        archive[candidate->name_offset..candidate_name_end];
    usize index = 0;
    while (index < catalog->count) {
        const Entry* previous = &catalog->entries[index];
        usize previous_name_end = previous->name_offset + previous->name_len;
        const u8[] previous_name =
            archive[previous->name_offset..previous_name_end];

        bool same = same_identity(
            candidate_name,
            candidate->is_directory,
            previous_name,
            previous->is_directory);
        throw (same == true) ZipError {
            .code = ZipErrorCode::DuplicatePath,
            .offset = candidate->name_offset,
            .message = "duplicate portable output path",
        };

        bool previous_parent = is_parent_path(
            previous_name,
            previous->is_directory,
            candidate_name,
            candidate->is_directory);
        throw ((previous_parent == true) && (previous->is_directory == false)) ZipError {
            .code = ZipErrorCode::DuplicatePath,
            .offset = candidate->name_offset,
            .message = "file entry is used as a parent directory",
        };

        bool candidate_parent = is_parent_path(
            candidate_name,
            candidate->is_directory,
            previous_name,
            previous->is_directory);
        throw ((candidate_parent == true) && (candidate->is_directory == false)) ZipError {
            .code = ZipErrorCode::DuplicatePath,
            .offset = candidate->name_offset,
            .message = "file entry contains an existing child path",
        };

        bool overlaps = (candidate->record_offset < previous->record_end) &&
                        (previous->record_offset < candidate->record_end);
        throw (overlaps == true) ZipError {
            .code = ZipErrorCode::InvalidZip,
            .offset = candidate->record_offset,
            .message = "local ZIP entry records overlap",
        };

        index += 1;
    }

}

Catalog parse(const u8[] archive) throws ZipError {
    usize archive_size = len(archive);
    usize eocd = find_eocd(archive);

    u16 disk_number = read_u16_le(archive, eocd + 4);
    u16 central_disk = read_u16_le(archive, eocd + 6);
    u16 entries_on_disk = read_u16_le(archive, eocd + 8);
    u16 entry_count16 = read_u16_le(archive, eocd + 10);
    u32 central_size32 = read_u32_le(archive, eocd + 12);
    u32 central_offset32 = read_u32_le(archive, eocd + 16);

    throw ((disk_number != 0) || (central_disk != 0) ||
           (entries_on_disk != entry_count16)) ZipError {
        .code = ZipErrorCode::UnsupportedZip64,
        .offset = eocd,
        .message = "multi-disk ZIP archives are not supported",
    };
    throw ((entry_count16 == 0xffff) || (central_size32 == 0xffff_ffff) ||
           (central_offset32 == 0xffff_ffff)) ZipError {
        .code = ZipErrorCode::UnsupportedZip64,
        .offset = eocd,
        .message = "ZIP64 archive is outside this bounded example profile",
    };

    usize entry_count = entry_count16 as usize;
    throw (entry_count > MAX_ENTRIES) ZipError {
        .code = ZipErrorCode::LimitExceeded,
        .offset = entry_count,
        .message = "archive has too many entries",
    };

    usize central_size = central_size32 as usize;
    usize central_offset = central_offset32 as usize;
    bool central_span = has_span(archive_size, central_offset, central_size);
    throw (central_span == false) ZipError {
        .code = ZipErrorCode::Truncated,
        .offset = central_offset,
        .message = "central directory is outside archive",
    };
    usize central_end = central_offset + central_size;
    throw (central_end != eocd) ZipError {
        .code = ZipErrorCode::InvalidZip,
        .offset = central_end,
        .message = "central directory does not end at EOCD",
    };

    Catalog catalog = {};
    usize cursor = central_offset;
    usize index = 0;
    while (index < entry_count) {
        bool fixed_header = has_span(central_end, cursor, 46);
        throw (fixed_header == false) ZipError {
            .code = ZipErrorCode::Truncated,
            .offset = cursor,
            .message = "truncated central directory entry",
        };
        u32 signature = read_u32_le(archive, cursor);
        throw (signature != CENTRAL_SIGNATURE) ZipError {
            .code = ZipErrorCode::InvalidZip,
            .offset = cursor,
            .message = "invalid central directory signature",
        };

        u16 version_made_by = read_u16_le(archive, cursor + 4);
        u16 flags = read_u16_le(archive, cursor + 8);
        u16 method = read_u16_le(archive, cursor + 10);
        u32 expected_crc = read_u32_le(archive, cursor + 16);
        u32 compressed32 = read_u32_le(archive, cursor + 20);
        u32 uncompressed32 = read_u32_le(archive, cursor + 24);
        u16 name_size16 = read_u16_le(archive, cursor + 28);
        u16 extra_size16 = read_u16_le(archive, cursor + 30);
        u16 comment_size16 = read_u16_le(archive, cursor + 32);
        u16 start_disk = read_u16_le(archive, cursor + 34);
        u32 external_attributes = read_u32_le(archive, cursor + 38);
        u32 local_offset32 = read_u32_le(archive, cursor + 42);

        throw ((compressed32 == 0xffff_ffff) ||
               (uncompressed32 == 0xffff_ffff) ||
               (local_offset32 == 0xffff_ffff) ||
               (start_disk != 0)) ZipError {
            .code = ZipErrorCode::UnsupportedZip64,
            .offset = cursor,
            .message = "ZIP64 or split entry is not supported",
        };

        throw ((flags & 1) != 0) ZipError {
            .code = ZipErrorCode::UnsupportedEncryption,
            .offset = cursor,
            .message = "encrypted ZIP entry is not supported",
        };
        u16 allowed_flags = 0x080e;
        u16 unsupported_flags = (flags & (~allowed_flags)) as u16;
        throw (unsupported_flags != 0) ZipError {
            .code = ZipErrorCode::InvalidZip,
            .offset = cursor,
            .message = "unsupported general-purpose ZIP flags",
        };
        throw ((method != 0) && (method != 8)) ZipError {
            .code = ZipErrorCode::UnsupportedMethod,
            .offset = cursor,
            .message = "only stored and deflate methods are supported",
        };
        throw ((method == 0) && ((flags & 6) != 0)) ZipError {
            .code = ZipErrorCode::InvalidZip,
            .offset = cursor,
            .message = "stored entry uses deflate option bits",
        };

        usize name_size = name_size16 as usize;
        usize extra_size = extra_size16 as usize;
        usize comment_size = comment_size16 as usize;
        usize variable_size = name_size + extra_size + comment_size;
        bool variable_span = has_span(central_end, cursor + 46, variable_size);
        throw (variable_span == false) ZipError {
            .code = ZipErrorCode::Truncated,
            .offset = cursor,
            .message = "central directory variable fields are truncated",
        };

        usize name_offset = cursor + 46;
        usize name_end = name_offset + name_size;
        const u8[] name = archive[name_offset..name_end];
        validate_kind(version_made_by, external_attributes, cursor);
        bool directory = is_directory_entry(name, version_made_by, external_attributes);
        bool utf8_flag = (flags & 0x0800) != 0;
        validate(name, utf8_flag, directory, name_offset);

        usize compressed_size = compressed32 as usize;
        usize uncompressed_size = uncompressed32 as usize;
        throw ((directory == true) &&
               ((uncompressed_size != 0) || (expected_crc != 0))) ZipError {
            .code = ZipErrorCode::InvalidZip,
            .offset = cursor,
            .message = "directory entry declares output bytes or a nonzero CRC",
        };
        throw (uncompressed_size > MAX_ENTRY_BYTES) ZipError {
            .code = ZipErrorCode::LimitExceeded,
            .offset = cursor,
            .message = "entry exceeds per-entry output limit",
        };
        bool bomb_ratio = exceeds_ratio(compressed_size, uncompressed_size);
        throw (bomb_ratio == true) ZipError {
            .code = ZipErrorCode::LimitExceeded,
            .offset = cursor,
            .message = "entry exceeds compression-ratio limit",
        };
        throw (catalog.total_uncompressed > MAX_TOTAL_BYTES) ZipError {
            .code = ZipErrorCode::Internal,
            .offset = cursor,
            .message = "catalog total escaped configured limit",
        };
        usize total_remaining = MAX_TOTAL_BYTES - catalog.total_uncompressed;
        throw (uncompressed_size > total_remaining) ZipError {
            .code = ZipErrorCode::LimitExceeded,
            .offset = cursor,
            .message = "archive exceeds total output limit",
        };

        usize local_offset = local_offset32 as usize;
        bool local_fixed = has_span(central_offset, local_offset, 30);
        throw (local_fixed == false) ZipError {
            .code = ZipErrorCode::Truncated,
            .offset = local_offset,
            .message = "local file header is truncated",
        };
        u32 local_signature = read_u32_le(archive, local_offset);
        u16 local_flags = read_u16_le(archive, local_offset + 6);
        u16 local_method = read_u16_le(archive, local_offset + 8);
        u16 local_name_size16 = read_u16_le(archive, local_offset + 26);
        u16 local_extra_size16 = read_u16_le(archive, local_offset + 28);
        throw ((local_signature != LOCAL_SIGNATURE) ||
               (local_flags != flags) || (local_method != method)) ZipError {
            .code = ZipErrorCode::InvalidZip,
            .offset = local_offset,
            .message = "local and central headers disagree",
        };
        throw (local_name_size16 != name_size16) ZipError {
            .code = ZipErrorCode::InvalidZip,
            .offset = local_offset,
            .message = "local and central path lengths differ",
        };

        usize local_name_offset = local_offset + 30;
        bool local_name_span = has_span(central_offset, local_name_offset, name_size);
        throw (local_name_span == false) ZipError {
            .code = ZipErrorCode::Truncated,
            .offset = local_name_offset,
            .message = "local path is truncated",
        };
        usize local_name_end = local_name_offset + name_size;
        const u8[] local_name = archive[local_name_offset..local_name_end];
        bool names_match = ranges_equal(name, local_name);
        throw (names_match == false) ZipError {
            .code = ZipErrorCode::InvalidZip,
            .offset = local_name_offset,
            .message = "local and central paths differ",
        };

        bool has_descriptor = (flags & 8) != 0;
        if (has_descriptor == false) {
            u32 local_crc = read_u32_le(archive, local_offset + 14);
            u32 local_compressed = read_u32_le(archive, local_offset + 18);
            u32 local_uncompressed = read_u32_le(archive, local_offset + 22);
            throw ((local_crc != expected_crc) ||
                   (local_compressed != compressed32) ||
                   (local_uncompressed != uncompressed32)) ZipError {
                .code = ZipErrorCode::InvalidZip,
                .offset = local_offset,
                .message = "local sizes or CRC differ from central directory",
            };
        }

        usize local_extra_size = local_extra_size16 as usize;
        usize data_offset = local_name_end + local_extra_size;
        bool data_span = has_span(central_offset, data_offset, compressed_size);
        throw (data_span == false) ZipError {
            .code = ZipErrorCode::Truncated,
            .offset = data_offset,
            .message = "compressed entry data is truncated",
        };

        usize data_end = data_offset + compressed_size;
        usize record_end = data_end;
        if (has_descriptor == true) {
            record_end = descriptor_end(
                archive,
                central_offset,
                data_end,
                expected_crc,
                compressed32,
                uncompressed32);
        }

        Entry entry = {
            .record_offset = local_offset,
            .record_end = record_end,
            .data_offset = data_offset,
            .compressed_size = compressed_size,
            .uncompressed_size = uncompressed_size,
            .crc32 = expected_crc,
            .method = method,
            .name_offset = name_offset,
            .name_len = name_size,
            .is_directory = directory,
        };
        validate_catalog_conflicts(archive, &catalog, &entry);

        catalog.entries[catalog.count] = entry;
        catalog.count += 1;
        catalog.total_uncompressed += uncompressed_size;

        cursor += 46 + variable_size;
        index += 1;
    }

    throw (cursor != central_end) ZipError {
        .code = ZipErrorCode::InvalidZip,
        .offset = cursor,
        .message = "central directory size does not match its entries",
    };

    return catalog;
}
