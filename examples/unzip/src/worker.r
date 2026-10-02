module example.unzip.worker;

import example.unzip.deflate::{decode};
import example.unzip.error::{ZipError, ZipErrorCode};
import example.unzip.host::{create_directory, relative_path, write_file};
import example.unzip.model::{ArchiveImage, Catalog, Entry, Range, Scratch};

protected bytes owned_bytes(const u8[] source,
    usize archive_offset) throws ZipError {
    usize size = len(source);
    try {
        bytes destination = std.bytes::with_capacity(size);
        destination.append(source);
        return move destination;
    } catch (std.alloc::alloc_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::Io,
            .offset = archive_offset,
            .message = "cannot allocate or fill owned output buffer",
        };
    }
}

protected std.fs::path entry_path(
    const ArchiveImage* image,
    const Entry* entry) throws ZipError {
    const u8[] archive = std.array::as_slice(&image->bytes);
    usize path_length = entry->name_len;
    if ((entry->is_directory == true) && (path_length > 0)) {
        usize final_index = path_length - 1;
        usize final_offset = entry->name_offset + final_index;
        if (archive[final_offset] == 47) {
            path_length -= 1;
        }
    }
    throw (path_length == 0) ZipError {
        .code = ZipErrorCode::UnsafePath,
        .offset = entry->name_offset,
        .message = "entry path is empty after directory-marker normalization",
    };
    usize name_end = entry->name_offset + path_length;
    const u8[] name = archive[entry->name_offset..name_end];
    std.fs::path path = relative_path(name, entry->name_offset);
    return move path;
}

protected o<std.fs::path> entry_parent_path(
    const ArchiveImage* image,
    const Entry* entry) throws ZipError {
    const u8[] archive = std.array::as_slice(&image->bytes);
    usize name_end = entry->name_offset + entry->name_len;
    const u8[] name = archive[entry->name_offset..name_end];
    usize index = len(name);
    while (index > 0) {
        index -= 1;
        if (name[index] == 47) {
            const u8[] parent_bytes = name[0..index];
            std.fs::path parent = relative_path(
                parent_bytes,
                entry->name_offset);
            o<std.fs::path> found = o::some(move parent);
            return move found;
        }
    }
    return o::none;
}

protected void validate_directory_entry(
    const ArchiveImage* image,
    const Entry* entry,
    Scratch* scratch) throws ZipError {
    throw ((entry->uncompressed_size != 0) || (entry->crc32 != 0)) ZipError {
        .code = ZipErrorCode::InvalidZip,
        .offset = entry->data_offset,
        .message = "directory entry carries file data",
    };
    if (entry->method == 0) {
        throw (entry->compressed_size != 0) ZipError {
            .code = ZipErrorCode::InvalidZip,
            .offset = entry->data_offset,
            .message = "stored directory entry carries compressed data",
        };
        return;
    }

    const u8[] archive = std.array::as_slice(&image->bytes);
    usize directory_data_end = entry->data_offset + entry->compressed_size;
    const u8[] directory_data =
        archive[entry->data_offset..directory_data_end];
    u8[] empty_output = scratch->bytes[0..0];
    usize decoded = decode(directory_data, empty_output, 0);
    throw (decoded != 0) ZipError {
        .code = ZipErrorCode::InvalidDeflate,
        .offset = entry->data_offset,
        .message = "directory deflate stream produced data",
    };
}

protected bytes prepare_file_contents(
    const ArchiveImage* image,
    const Entry* entry,
    Scratch* scratch) throws ZipError {
    const u8[] archive = std.array::as_slice(&image->bytes);
    usize data_end = entry->data_offset + entry->compressed_size;
    const u8[] compressed = archive[entry->data_offset..data_end];
    if (entry->method == 0) {
        throw (entry->compressed_size != entry->uncompressed_size) ZipError {
            .code = ZipErrorCode::InvalidZip,
            .offset = entry->data_offset,
            .message = "stored entry has different compressed and uncompressed sizes",
        };
        u32 actual_crc = std.hash::crc32(compressed);
        throw (actual_crc != entry->crc32) ZipError {
            .code = ZipErrorCode::CrcMismatch,
            .offset = entry->data_offset,
            .message = "stored entry CRC-32 mismatch",
        };
        bytes stored = owned_bytes(compressed, entry->data_offset);
        return move stored;
    }

    throw (entry->method != 8) ZipError {
        .code = ZipErrorCode::UnsupportedMethod,
        .offset = entry->data_offset,
        .message = "entry compression method changed after validation",
    };

    usize output_size = entry->uncompressed_size;
    u8[] output = scratch->bytes[0..output_size];
    usize decoded = decode(compressed, output, output_size);
    throw (decoded != output_size) ZipError {
        .code = ZipErrorCode::InvalidDeflate,
        .offset = entry->data_offset,
        .message = "deflate decoder returned an unexpected size",
    };

    const u8[] output_view = output;
    u32 actual_crc = std.hash::crc32(output_view);
    throw (actual_crc != entry->crc32) ZipError {
        .code = ZipErrorCode::CrcMismatch,
        .offset = entry->data_offset,
        .message = "deflated entry CRC-32 mismatch",
    };

    bytes contents = owned_bytes(output_view, entry->data_offset);
    return move contents;
}

async void extract_range(
    arc ArchiveImage image,
    arc Catalog catalog,
    Range range,
    arc std.fs::directory output_root) throws ZipError {
    throw ((range.begin > range.end) || (range.end > catalog->count)) ZipError {
        .code = ZipErrorCode::Internal,
        .offset = range.end,
        .message = "worker range is outside catalog",
    };

    own Scratch* scratch = new Scratch{};
    usize index = range.begin;
    while (index < range.end) {
        bool directory = catalog->entries[index].is_directory;
        if (directory == true) {
            validate_directory_entry(
                &*image,
                &catalog->entries[index],
                &*scratch);
            std.fs::path relative = entry_path(
                &*image,
                &catalog->entries[index]);
            arc std.fs::directory root = output_root.clone();
            try {
                await create_directory(move root, move relative);
            } catch (std.async::start_error error) {
                error as void;
                throw ZipError {
                    .code = ZipErrorCode::Async,
                    .offset = index,
                    .message = "cannot start directory extraction",
                };
            }
        } else {
            o<std.fs::path> parent = entry_parent_path(
                &*image,
                &catalog->entries[index]);
            switch (move parent) {
                case variant o::some(move parent_path):
                    arc std.fs::directory parent_root =
                        output_root.clone();
                    try {
                        await create_directory(
                                move parent_root,
                                move parent_path);
                    } catch (std.async::start_error error) {
                        error as void;
                        throw ZipError {
                            .code = ZipErrorCode::Async,
                            .offset = index,
                            .message = "cannot start parent-directory creation",
                        };
                    }
                    break;
                case variant o::none:
                    break;
            }
            bytes contents = prepare_file_contents(
                &*image,
                &catalog->entries[index],
                &*scratch);
            std.fs::path relative = entry_path(
                &*image,
                &catalog->entries[index]);
            arc std.fs::directory root = output_root.clone();
            try {
                await write_file(move root, move relative, move contents);
            } catch (std.async::start_error error) {
                error as void;
                throw ZipError {
                    .code = ZipErrorCode::Async,
                    .offset = index,
                    .message = "cannot start file extraction",
                };
            }
        }
        index += 1;
    }
}
