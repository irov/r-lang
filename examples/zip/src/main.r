module example.zip.main;

import example.zip.archive::{append_central, append_local, finish};
import example.zip.bytes::{append, append_u16_le, create};
import example.zip.error::{ZipError, ZipErrorCode};
import example.zip.host::{path_bytes, print_error, print_usage, read_entry,
                         write_output};
import example.zip.model::{INITIAL_ARCHIVE_CAPACITY,
                          INITIAL_CENTRAL_CAPACITY,
                          MAX_ENTRIES};
import example.zip.name::{conflicts, is_directory, validate};

protected u16 read_u16_le(const u8[] bytes, usize offset) {
    u16 low = bytes[offset] as u16;
    usize high_index = offset + 1;
    u16 high = bytes[high_index] as u16;
    return (low | (high << 8)) as u16;
}

protected usize read_record_length(const u8[] bytes, usize offset) {
    u16 encoded = read_u16_le(bytes, offset);
    return encoded as usize;
}

protected void append_entry_record(bytes* storage,
    const u8[] entry) throws ZipError {
    usize entry_length = len(entry);
    throw (entry_length > 65_535) ZipError {
        .code = ZipErrorCode::UnsafePath,
        .offset = entry_length,
        .message = "ZIP entry path does not fit ZIP32",
    };
    u16 encoded_length = entry_length as u16;
    append_u16_le(storage, encoded_length);
    append(storage, entry);
}

protected async i32 finish_error(ZipError error,
    i32 exit_code,
    bool usage) {
    if (usage == true) {
        try {
            await print_usage();
        } catch (std.async::start_error start_error) {
            start_error as void;
        } catch (ZipError diagnostic_error) {
            diagnostic_error as void;
        }
    } else {
        try {
            await print_error(error);
        } catch (std.async::start_error start_error) {
            start_error as void;
        } catch (ZipError diagnostic_error) {
            diagnostic_error as void;
        }
    }
    return exit_code;
}

protected async i32 report(ZipError error, i32 exit_code, bool usage) {
    try {
        i32 status = await finish_error(error, exit_code, usage);
        return status;
    } catch (std.async::start_error start_error) {
        start_error as void;
        return exit_code;
    }
}

protected async i32 report_or_fallback(ZipError error,
    i32 exit_code,
    bool usage) {
    try {
        i32 status = await report(error, exit_code, usage);
        return status;
    } catch (std.async::start_error start_error) {
        start_error as void;
        return exit_code;
    }
}

async i32 main(const str[] args) {
    try {
        usize argument_count = len(args);
        throw ((argument_count < 3) || ((argument_count - 3) > MAX_ENTRIES)) ZipError {
            .code = ZipErrorCode::InvalidArguments,
            .offset = argument_count,
            .message =
                "expected output directory, archive name, and at most 1024 entries",
        };

        str archive_text = args[2];
        const u8[] archive_argument = archive_text;
        bool archive_directory = is_directory(archive_argument);
        throw (archive_directory == true) ZipError {
            .code = ZipErrorCode::InvalidArguments,
            .offset = 2,
            .message = "archive name must identify a file",
        };
        validate(archive_argument, false);

        bytes argument_bytes = create(
            INITIAL_ARCHIVE_CAPACITY,
            "cannot allocate owned argument storage");
        usize output_offset = len(argument_bytes);
        str output_text = args[1];
        const u8[] output_argument = output_text;
        usize output_length = len(output_argument);
        append(&argument_bytes, output_argument);

        usize archive_offset = len(argument_bytes);
        usize archive_length = len(archive_argument);
        append(&argument_bytes, archive_argument);

        usize entry_count = argument_count - 3;
        usize entry_records_offset = len(argument_bytes);
        usize entry_index = 0;
        while (entry_index < entry_count) {
            usize argument_index = entry_index + 3;
            str entry_text = args[argument_index];
            const u8[] entry_argument = entry_text;
            bool directory = is_directory(entry_argument);
            validate(entry_argument, directory);

            usize previous = 0;
            while (previous < entry_index) {
                usize previous_argument = previous + 3;
                str other_text = args[previous_argument];
                bool collision = conflicts(other_text, entry_text);
                throw (collision == true) ZipError {
                    .code = ZipErrorCode::DuplicatePath,
                    .offset = argument_index,
                    .message = "duplicate or conflicting ZIP entry path",
                };
                previous += 1;
            }

            append_entry_record(&argument_bytes, entry_argument);
            entry_index += 1;
        }

        bytes output = create(
            INITIAL_ARCHIVE_CAPACITY,
            "cannot allocate ZIP output");
        bytes central = create(
            INITIAL_CENTRAL_CAPACITY,
            "cannot allocate ZIP central directory");

        usize entry_cursor = entry_records_offset;
        usize entry_index_2 = 0;
        while (entry_index_2 < entry_count) {
            const u8[] argument_view = argument_bytes.as_slice();
            usize name_length = read_record_length(argument_view, entry_cursor);
            usize name_offset = entry_cursor + 2;
            usize name_end = name_offset + name_length;
            entry_cursor = name_end;
            const u8[] entry_name = argument_view[name_offset..name_end];
            bool directory = is_directory(entry_name);
            usize output_size = len(output);

            if (directory == true) {
                const u8[] empty = entry_name[0..0];
                u32 checksum = std.hash::crc32(empty);
                append_local(
                    &output,
                    output_size,
                    entry_name,
                    empty,
                    checksum,
                    true);
                usize archive_end = len(output);
                usize central_size = len(central);
                append_central(
                    &central,
                    archive_end,
                    central_size,
                    output_size,
                    entry_name,
                    0,
                    checksum,
                    true);
            } else {
                std.fs::path input_path = path_bytes(
                    entry_name,
                    "input path is not representable by std.fs");
                bytes contents = await read_entry(move input_path);

                const u8[] names_after = argument_bytes.as_slice();
                const u8[] stable_name = names_after[name_offset..name_end];
                const u8[] data = contents.as_slice();
                usize data_size = len(data);
                u32 checksum = std.hash::crc32(data);
                append_local(
                    &output,
                    output_size,
                    stable_name,
                    data,
                    checksum,
                    false);
                usize archive_end = len(output);
                usize central_size = len(central);
                append_central(
                    &central,
                    archive_end,
                    central_size,
                    output_size,
                    stable_name,
                    data_size,
                    checksum,
                    false);
            }
            entry_index_2 += 1;
        }

        usize central_offset = len(output);
        finish(&output, &central, central_offset, entry_count);

        const u8[] all_arguments = argument_bytes.as_slice();
        usize output_end = output_offset + output_length;
        const u8[] output_name = all_arguments[output_offset..output_end];
        std.fs::path output_path = path_bytes(
            output_name,
            "output path is not representable by std.fs");
        usize archive_end = archive_offset + archive_length;
        const u8[] archive_name = all_arguments[archive_offset..archive_end];
        std.fs::path archive_path = path_bytes(
            archive_name,
            "archive name is not representable by std.fs");

        await write_output(
            move output_path,
            move archive_path,
            move output);
        return 0;
    } catch (ZipError error) {
        bool usage = error.code == ZipErrorCode::InvalidArguments;
        i32 exit_code = 1;
        if (usage == true) {
            exit_code = 2;
        }
        i32 status = await report_or_fallback(error, exit_code, usage);
        return status;
    }
}
