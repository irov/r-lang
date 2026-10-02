module example.zip.host;

import example.zip.bytes::{append, copy};
import example.zip.error::{ZipError, ZipErrorCode};
import example.zip.model::{MAX_ENTRY_BYTES};

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

std.fs::path path_bytes(const u8[] text,
    constexpr str failure_message) throws ZipError {
    try {
        std.fs::path path = std.fs::path_from_utf8_bytes(text);
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

async bytes read_entry(std.fs::path input_path) throws ZipError {
    try {
        bytes contents = await input_path.read_file(MAX_ENTRY_BYTES);
        usize size = len(contents);
        throw (size > MAX_ENTRY_BYTES) ZipError {
            .code = ZipErrorCode::Internal,
            .offset = size,
            .message = "std.fs read exceeded the entry limit",
        };
        return move contents;
    } catch (std.async::start_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::Async,
            .offset = 0,
            .message = "cannot start input-file read",
        };
    } catch (std.fs::fs_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::Io,
            .offset = 0,
            .message = "cannot read input file",
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

async void publish(arc std.fs::directory root,
    std.fs::path archive_path,
    bytes archive) throws ZipError {
    const std.fs::directory* root_view = &*root;
    try {
        std.fs::write_file_result completed = await root_view->write_file_atomic_no_replace_beneath(&archive_path,
                move archive);
        switch (move completed) {
            case variant std.fs::write_file_result::committed(move returned):
                drop returned;
                break;
            case variant std.fs::write_file_result::failed(move failure):
                throw ZipError {
                    .code = ZipErrorCode::Io,
                    .offset = 0,
                    .message = (failure.error.code ==
                        std.fs::error_code::already_exists)
                        ? "output archive already exists"
                        : "cannot atomically publish output archive",
                };
        }
    } catch (std.async::start_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::Async,
            .offset = 0,
            .message = "cannot start archive publication",
        };
    }
}

async void write_output(std.fs::path output_path,
    std.fs::path archive_path,
    bytes archive) throws ZipError {
    try {
        std.fs::directory directory = await open_output_root(move output_path);
        arc std.fs::directory root = new arc std.fs::directory(move directory);
        await publish(move root, move archive_path, move archive);
    } catch (std.async::start_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::Async,
            .offset = 0,
            .message = "cannot start output task",
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
                usize written = failure.written;
                drop failure;
                throw ZipError {
                    .code = ZipErrorCode::Io,
                    .offset = written,
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
        "usage: zip <existing-output-directory> <archive-name> <entry>...\n"
        "directory entries must end with '/'\n";
    str text_view = text;
    const u8[] bytes = text_view;
    bytes buffer = copy(bytes, "cannot allocate usage diagnostic");
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
        std.string::string diagnostic = f"zip: {error.message}\n";
        bytes buffer = (move diagnostic).into_bytes();
        await write_stderr(move buffer);
    } catch (std.alloc::alloc_error allocation_error) {
        throw ZipError {
            .code = ZipErrorCode::Allocation,
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
