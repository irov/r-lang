module example.unzip.path;

import example.unzip.error::{ZipError, ZipErrorCode};
import example.unzip.model::{MAX_PATH_BYTES};

protected bool ascii_only(const u8[] name) {
    usize size = len(name);
    usize index = 0;
    while (index < size) {
        if (name[index] > 0x7f) {
            return false;
        }
        index += 1;
    }
    return true;
}

void validate(const u8[] name,
    bool utf8_flag,
    bool is_directory,
    usize archive_offset) throws ZipError {
    usize size = len(name);
    throw ((size == 0) || (size > MAX_PATH_BYTES)) ZipError {
        .code = ZipErrorCode::UnsafePath,
        .offset = archive_offset,
        .message = "empty or overlong archive path",
    };

    bool utf8 = std.utf8::is_valid(name);
    throw (utf8 == false) ZipError {
        .code = ZipErrorCode::InvalidUtf8,
        .offset = archive_offset,
        .message = "entry path is not valid UTF-8",
    };

    if (utf8_flag == false) {
        bool ascii = ascii_only(name);
        throw (ascii == false) ZipError {
            .code = ZipErrorCode::InvalidUtf8,
            .offset = archive_offset,
            .message = "non-ASCII path lacks the ZIP UTF-8 flag",
        };
    }

    u8 first = name[0];
    throw ((first == 47) || (first == 92)) ZipError {
        .code = ZipErrorCode::UnsafePath,
        .offset = archive_offset,
        .message = "absolute archive path",
    };

    usize component_start = 0;
    usize index = 0;
    while (index <= size) {
        bool at_end = index == size;
        bool separator = false;
        if (at_end == false) {
            separator = name[index] == 47;
            u8 byte = name[index];
            if ((byte == 0) || (byte == 92) || (byte == 58) || (byte < 32)) {
                usize bad_offset = archive_offset + index;
                throw ZipError {
                    .code = ZipErrorCode::UnsafePath,
                    .offset = bad_offset,
                    .message = "path contains NUL, backslash, colon, or control byte",
                };
            }
        }

        if ((at_end == true) || (separator == true)) {
            usize component_size = index - component_start;
            if (component_size == 0) {
                bool trailing_directory_separator =
                    (at_end == true) && (is_directory == true) && (component_start == size);
                throw (trailing_directory_separator == false) ZipError {
                    .code = ZipErrorCode::UnsafePath,
                    .offset = archive_offset,
                    .message = "path contains an empty component",
                };
            } else {
                bool dot = false;
                bool dot_dot = false;
                if (component_size == 1) {
                    dot = name[component_start] == 46;
                }
                if (component_size == 2) {
                    usize second_index = component_start + 1;
                    dot_dot = (name[component_start] == 46) &&
                              (name[second_index] == 46);
                }
                throw ((dot == true) || (dot_dot == true)) ZipError {
                    .code = ZipErrorCode::UnsafePath,
                    .offset = archive_offset,
                    .message = "path contains dot traversal component",
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

protected usize identity_size(const u8[] source, bool is_directory) {
    usize size = len(source);
    if ((is_directory == true) && (size > 0)) {
        usize last = size - 1;
        if (source[last] == 47) {
            return last;
        }
    }
    return size;
}

bool same_identity(const u8[] left,
    bool left_directory,
    const u8[] right,
    bool right_directory) {
    usize left_size = identity_size(left, left_directory);
    usize right_size = identity_size(right, right_directory);
    if (left_size != right_size) {
        return false;
    }
    usize index = 0;
    while (index < left_size) {
        u8 left_byte = fold_ascii(left[index]);
        u8 right_byte = fold_ascii(right[index]);
        if (left_byte != right_byte) {
            return false;
        }
        index += 1;
    }
    return true;
}

bool is_parent_path(const u8[] parent,
    bool parent_directory,
    const u8[] child,
    bool child_directory) {
    usize parent_size = identity_size(parent, parent_directory);
    usize child_size = identity_size(child, child_directory);
    if (parent_size >= child_size) {
        return false;
    }

    usize index = 0;
    while (index < parent_size) {
        u8 parent_byte = fold_ascii(parent[index]);
        u8 child_byte = fold_ascii(child[index]);
        if (parent_byte != child_byte) {
            return false;
        }
        index += 1;
    }

    if (child[parent_size] != 47) {
        return false;
    }
    return true;
}
