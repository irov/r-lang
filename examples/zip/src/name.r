module example.zip.name;

import example.zip.error::{ZipError, ZipErrorCode};
import example.zip.model::{MAX_PATH_BYTES};

bool is_directory(const u8[] entry) {
    usize size = len(entry);
    if (size == 0) {
        return false;
    }
    usize last = size - 1;
    return entry[last] == 47;
}

void validate(const u8[] entry, bool directory) throws ZipError {
    usize size = len(entry);
    throw ((size == 0) || (size > MAX_PATH_BYTES) || (size > 65_535)) ZipError {
        .code = ZipErrorCode::UnsafePath,
        .offset = size,
        .message = "empty or overlong ZIP entry path",
    };
    bool utf8 = std.utf8::is_valid(entry);
    throw (utf8 == false) ZipError {
        .code = ZipErrorCode::UnsafePath,
        .offset = 0,
        .message = "ZIP entry path is not valid UTF-8",
    };

    u8 first = entry[0];
    throw ((first == 47) || (first == 92)) ZipError {
        .code = ZipErrorCode::UnsafePath,
        .offset = 0,
        .message = "ZIP entry path must be relative",
    };

    usize component_start = 0;
    usize index = 0;
    while (index <= size) {
        bool at_end = index == size;
        bool separator = false;
        if (at_end == false) {
            u8 byte = entry[index];
            separator = byte == 47;
            throw ((byte == 0) || (byte == 92) || (byte == 58) || (byte < 32)) ZipError {
                .code = ZipErrorCode::UnsafePath,
                .offset = index,
                .message = "ZIP entry path contains an unsafe byte",
            };
        }

        if ((at_end == true) || (separator == true)) {
            usize component_size = index - component_start;
            if (component_size == 0) {
                bool trailing = (at_end == true) && (directory == true) &&
                                (component_start == size);
                throw (trailing == false) ZipError {
                    .code = ZipErrorCode::UnsafePath,
                    .offset = index,
                    .message = "ZIP entry path contains an empty component",
                };
            } else {
                bool dot = false;
                bool dot_dot = false;
                if (component_size == 1) {
                    dot = entry[component_start] == 46;
                }
                if (component_size == 2) {
                    usize second = component_start + 1;
                    dot_dot = (entry[component_start] == 46) &&
                              (entry[second] == 46);
                }
                throw ((dot == true) || (dot_dot == true)) ZipError {
                    .code = ZipErrorCode::UnsafePath,
                    .offset = component_start,
                    .message = "ZIP entry path contains a dot component",
                };
            }
            component_start = index + 1;
        }
        if (at_end == true) {
            break;
        }
        index += 1;
    }
}

protected u8 fold_ascii(u8 byte) {
    if ((byte >= 65) && (byte <= 90)) {
        u8 folded = (byte + 32) as u8;
        return folded;
    }
    return byte;
}

protected bool prefix(const u8[] shorter, const u8[] longer) {
    usize shorter_size = len(shorter);
    usize longer_size = len(longer);
    if (shorter_size > longer_size) {
        return false;
    }
    usize index = 0;
    while (index < shorter_size) {
        u8 shorter_byte = fold_ascii(shorter[index]);
        u8 longer_byte = fold_ascii(longer[index]);
        if (shorter_byte != longer_byte) {
            return false;
        }
        index += 1;
    }
    return true;
}

bool conflicts(str left_text, str right_text) {
    const u8[] left = left_text;
    const u8[] right = right_text;
    usize left_size = len(left);
    usize right_size = len(right);
    bool left_directory = is_directory(left);
    bool right_directory = is_directory(right);
    usize left_canonical = left_size;
    usize right_canonical = right_size;
    if (left_directory == true) {
        left_canonical -= 1;
    }
    if (right_directory == true) {
        right_canonical -= 1;
    }

    if (left_canonical == right_canonical) {
        const u8[] left_name = left[0..left_canonical];
        const u8[] right_name = right[0..right_canonical];
        bool same = prefix(left_name, right_name);
        if (same == true) {
            return true;
        }
    }

    if ((left_directory == false) && (left_canonical < right_canonical)) {
        const u8[] left_name = left[0..left_canonical];
        const u8[] right_prefix = right[0..left_canonical];
        bool same = prefix(left_name, right_prefix);
        if (same == true) {
            if (right[left_canonical] == 47) {
                return true;
            }
        }
    }
    if ((right_directory == false) && (right_canonical < left_canonical)) {
        const u8[] right_name = right[0..right_canonical];
        const u8[] left_prefix = left[0..right_canonical];
        bool same = prefix(right_name, left_prefix);
        if (same == true) {
            if (left[right_canonical] == 47) {
                return true;
            }
        }
    }
    return false;
}
