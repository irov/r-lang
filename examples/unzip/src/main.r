module example.unzip.main;

import example.unzip.error::{ZipError, ZipErrorCode};
import example.unzip.host::{open_output_root, path_argument, print_error,
                           print_usage, read_archive};
import example.unzip.model::{ArchiveImage, Catalog, Range};
import example.unzip.worker::{extract_range};
import example.unzip.zip::{parse};

protected Catalog parse_archive(const bytes* bytes) throws ZipError {
    const u8[] archive = std.array::as_slice(bytes);
    Catalog catalog = parse(archive);
    return catalog;
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

protected async void run(
    std.fs::path archive_path,
    std.fs::path output_path) throws ZipError {
    try {
        bytes archive_bytes = await read_archive(move archive_path);

        std.fs::directory output_directory = await open_output_root(move output_path);

        Catalog parsed_catalog = parse_archive(&archive_bytes);
        usize midpoint = (parsed_catalog.count + 1) / 2;
        Range first = { .begin = 0, .end = midpoint };
        Range second = {
            .begin = midpoint,
            .end = parsed_catalog.count,
        };
        arc ArchiveImage image = new arc ArchiveImage {
            .bytes = move archive_bytes,
        };
        arc Catalog catalog = new arc Catalog(parsed_catalog);
        arc std.fs::directory output_root =
            new arc std.fs::directory(move output_directory);

        arc ArchiveImage first_image = image.clone();
        arc Catalog first_catalog = catalog.clone();
        arc std.fs::directory first_root = output_root.clone();
        await extract_range(
            move first_image,
            move first_catalog,
            first,
            move first_root);

        await extract_range(
            move image,
            move catalog,
            second,
            move output_root);
    } catch (std.async::start_error start_error) {
        start_error as void;
        throw ZipError {
            .code = ZipErrorCode::Async,
            .offset = 0,
            .message = "cannot start an extraction task",
        };
    }
}

protected async i32 execute_and_report(
    std.fs::path archive_path,
    std.fs::path output_path) {
    try {
        await run(move archive_path, move output_path);
        return 0;
    } catch (std.async::start_error start_error) {
        start_error as void;
        ZipError error = {
            .code = ZipErrorCode::Async,
            .offset = 0,
            .message = "cannot start extraction root task",
        };
        try {
            i32 status = await report(error, 1, false);
            return status;
        } catch (std.async::start_error report_start_error) {
            report_start_error as void;
            return 1;
        }
    } catch (ZipError error) {
        try {
            i32 status = await report(error, 1, false);
            return status;
        } catch (std.async::start_error report_start_error) {
            report_start_error as void;
            return 1;
        }
    }
}

async i32 main(const str[] args) {
    usize argument_count = len(args);
    if (argument_count != 3) {
        ZipError error = {
            .code = ZipErrorCode::InvalidArguments,
            .offset = argument_count,
            .message = "expected an archive and an existing output directory",
        };
        i32 status = await report(error, 2, true);
        return status;
    }

    try {
        str archive_text = args[1];
        std.fs::path archive_path = path_argument(
            archive_text,
            "archive path is not representable by std.fs");
        str output_text = args[2];
        std.fs::path output_path = path_argument(
            output_text,
            "output path is not representable by std.fs");
        i32 status = await execute_and_report(
            move archive_path,
            move output_path);
        return status;
    } catch (ZipError error) {
        i32 status = await report(error, 2, false);
        return status;
    }
}
