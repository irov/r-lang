module example.unzip.host;

import example.unzip.error::{ZipError, ZipErrorCode};
import example.unzip.model::{MAX_ARCHIVE_BYTES};

// This is the only host-facing module. Every potentially blocking operation
// uses the native asynchronous std.fs or std.io surface.

protected bytes owned_bytes(const u8[] source,
    constexpr str failure_message) throws ZipError {
    usize size = len(source);
    try {
        bytes destination = std.bytes::with_capacity(size);
        destination.append(source);
        return move destination;
    } catch (std.alloc::alloc_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::Io,
            .offset = size,
            .message = failure_message,
        };
    }
}

protected void append_bytes(bytes* destination,
    const u8[] source,
    constexpr str failure_message) throws ZipError {
    usize size = len(source);
    try {
        destination->append(source);
    } catch (std.alloc::alloc_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::Io,
            .offset = size,
            .message = failure_message,
        };
    }
}

std.fs::path path_argument(str text,
    constexpr str failure_message) throws ZipError {
    try {
        std.fs::path path = std.fs::path_from_utf8(text);
        return move path;
    } catch (std.fs::path_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::InvalidArguments,
            .offset = 0,
            .message = failure_message,
        };
    }
}

std.fs::path relative_path(const u8[] bytes,
    usize archive_offset) throws ZipError {
    try {
        std.fs::path path = std.fs::path_from_utf8_bytes(bytes);
        return move path;
    } catch (std.fs::path_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::UnsafePath,
            .offset = archive_offset,
            .message = "validated ZIP path cannot be represented by std.fs",
        };
    }
}

async bytes read_archive(std.fs::path archive_path) throws ZipError {
    try {
        bytes archive = await archive_path.read_file(MAX_ARCHIVE_BYTES);
        usize size = len(archive);
        throw (size > MAX_ARCHIVE_BYTES) ZipError {
            .code = ZipErrorCode::Internal,
            .offset = size,
            .message = "std.fs read exceeded the archive limit",
        };
        return move archive;
    } catch (std.async::start_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::Async,
            .offset = 0,
            .message = "cannot start archive read",
        };
    } catch (std.fs::fs_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::Io,
            .offset = 0,
            .message = "cannot read input archive",
        };
    }
}

async std.fs::directory open_output_root(
    std.fs::path output_path) throws ZipError {
    try {
        std.fs::directory directory = await output_path.open_directory();
        return move directory;
    } catch (std.async::start_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::Async,
            .offset = 0,
            .message = "cannot start output-directory open",
        };
    } catch (std.fs::fs_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::Io,
            .offset = 0,
            .message = "cannot open output directory capability",
        };
    }
}

async void create_directory(
    arc std.fs::directory root,
    std.fs::path relative) throws ZipError {
    const std.fs::directory* root_view = &*root;
    try {
        await root_view->create_directory_beneath(&relative,
                true);
    } catch (std.async::start_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::Async,
            .offset = 0,
            .message = "cannot start output-directory creation",
        };
    } catch (std.fs::fs_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::Io,
            .offset = 0,
            .message = "cannot create output directory beneath root",
        };
    }
}

async void write_file(
    arc std.fs::directory root,
    std.fs::path relative,
    bytes contents) throws ZipError {
    const std.fs::directory* root_view = &*root;
    try {
        std.fs::write_file_result completed = await root_view->write_file_atomic_no_replace_beneath(&relative,
                move contents);
        switch (move completed) {
            case variant std.fs::write_file_result::committed(move data):
                drop data;
                break;
            case variant std.fs::write_file_result::failed(move failure):
                bool collision = failure.error.code ==
                    std.fs::error_code::already_exists;
                throw (collision == true) ZipError {
                    .code = ZipErrorCode::DuplicatePath,
                    .offset = 0,
                    .message = "output file already exists",
                };
                throw ZipError {
                    .code = ZipErrorCode::Io,
                    .offset = 0,
                    .message = "cannot atomically write output file beneath root",
                };
        }
    } catch (std.async::start_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::Async,
            .offset = 0,
            .message = "cannot start output-file write",
        };
    }
}

protected async void write_stderr(bytes buffer) throws ZipError {
    std.io::output stream = std.io::stderr();
    try {
        std.io::write_all_result completed = await stream.write_all(move buffer);
        switch (move completed) {
            case variant std.io::write_all_result::written(move returned):
                drop returned;
                break;
            case variant std.io::write_all_result::failed(move failure):
                failure.error as void;
                throw ZipError {
                    .code = ZipErrorCode::Io,
                    .offset = failure.written,
                    .message = "cannot write diagnostic to stderr",
                };
        }
    } catch (std.async::start_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::Async,
            .offset = 0,
            .message = "cannot start stderr write",
        };
    }

    try {
        await stream.flush();
    } catch (std.async::start_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::Async,
            .offset = 0,
            .message = "cannot start stderr flush",
        };
    } catch (std.io::io_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::Io,
            .offset = 0,
            .message = "cannot flush stderr",
        };
    }
}

async void print_usage() throws ZipError {
    constexpr str text =
        "usage: unzip <archive.zip> <existing-output-directory>\n";
    str text_view = text;
    const u8[] bytes = text_view;
    bytes buffer = owned_bytes(
        bytes,
        "cannot allocate usage diagnostic");
    try {
        await write_stderr(move buffer);
    } catch (std.async::start_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::Async,
            .offset = 0,
            .message = "cannot start usage diagnostic",
        };
    }
}

async void print_error(ZipError error) throws ZipError {
    try {
        std.string::string diagnostic = f"unzip: {error.message}\n";
        bytes buffer = (move diagnostic).into_bytes();
        await write_stderr(move buffer);
    } catch (std.alloc::alloc_error allocation_error) {
        throw ZipError {
            .code = ZipErrorCode::Io,
            .offset = 0,
            .message = "cannot allocate error diagnostic",
        };
    } catch (std.async::start_error start_error) {
        start_error as void;
        throw ZipError {
            .code = ZipErrorCode::Async,
            .offset = 0,
            .message = "cannot start error diagnostic",
        };
    }
}
