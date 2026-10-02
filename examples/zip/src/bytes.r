module example.zip.bytes;

import example.zip.error::{ZipError, ZipErrorCode};

bytes create(usize capacity, constexpr str failure_message) throws ZipError {
    try {
        bytes storage = std.bytes::with_capacity(capacity);
        return move storage;
    } catch (std.alloc::alloc_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::Allocation,
            .offset = capacity,
            .message = failure_message,
        };
    }
}

void append_u8(bytes* target, u8 value) throws ZipError {
    try {
        std.bytes::append_u8(target, value);
    } catch (std.alloc::alloc_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::Allocation,
            .offset = 0,
            .message = "cannot grow ZIP output buffer",
        };
    }
}

void append_u16_le(bytes* target, u16 value) throws ZipError {
    try {
        target->append_u16_le(value);
    } catch (std.alloc::alloc_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::Allocation,
            .offset = 0,
            .message = "cannot grow ZIP output buffer",
        };
    }
}

void append_u32_le(bytes* target, u32 value) throws ZipError {
    try {
        target->append_u32_le(value);
    } catch (std.alloc::alloc_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::Allocation,
            .offset = 0,
            .message = "cannot grow ZIP output buffer",
        };
    }
}

void append(bytes* target, const u8[] source) throws ZipError {
    try {
        target->append(source);
    } catch (std.alloc::alloc_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::Allocation,
            .offset = 0,
            .message = "cannot grow ZIP output buffer",
        };
    }
}

bytes copy(const u8[] source, constexpr str failure_message) throws ZipError {
    usize size = len(source);
    bytes destination = create(size, failure_message);
    try {
        destination.append(source);
        return move destination;
    } catch (std.alloc::alloc_error error) {
        error as void;
        throw ZipError {
            .code = ZipErrorCode::Allocation,
            .offset = size,
            .message = failure_message,
        };
    }
}
