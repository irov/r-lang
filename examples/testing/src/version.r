module example.testing.version;

/* A version MAJOR.MINOR.PATCH of three decimal numbers. */
struct version {
    u32 major;
    u32 minor;
    u32 patch;
};

/* The part of a version that a release raises. */
enum part { major, minor, patch };

/* Reads MAJOR.MINOR.PATCH; a text of another shape reports the index of the first byte that
   does not fit, like std.convert. */
version parse(str input) throws std.convert::parse_error {
    const u8[] bytes = input;
    u32[3] numbers = {0u32, 0u32, 0u32};
    usize count = 0usize;
    usize start = 0usize;
    for (usize index = 0usize; index <= len(bytes); index += 1usize) {
        if (index == len(bytes) || bytes[index] == 46u8) {
            throw (count == 3usize)
                std.convert::parse_error {.code = std.convert::parse_error_code::trailing_character,
                                          .index = index};
            try {
                str piece = std.utf8::validate(bytes[start..index]);
                numbers[count] = std.convert::parse_u32(piece, 10u32);
            } catch (core::utf8_error failure) {
                failure as void;
                throw std.convert::parse_error {
                    .code = std.convert::parse_error_code::invalid_digit, .index = start};
            } catch (std.convert::parse_error failure) {
                /* The index of the whole text, not of the number. */
                throw std.convert::parse_error {.code = failure.code, .index = start + failure.index};
            }
            count += 1usize;
            start = index + 1usize;
        }
    }
    throw (count != 3usize)
        std.convert::parse_error {.code = std.convert::parse_error_code::empty,
                                  .index = len(bytes)};
    return version {.major = numbers[0], .minor = numbers[1], .patch = numbers[2]};
}

/* -1, 0 or 1 as the first version is older than, equal to or newer than the second. */
i32 compare(const version* left, const version* right) {
    if (left->major != right->major) {
        if (left->major < right->major) { return -1; }
        return 1;
    }
    if (left->minor != right->minor) {
        if (left->minor < right->minor) { return -1; }
        return 1;
    }
    if (left->patch != right->patch) {
        if (left->patch < right->patch) { return -1; }
        return 1;
    }
    return 0;
}

/* The next release: the raised part plus one, the parts after it zero; the parts before it
   come from the current version through a struct update (R-INIT-0004). */
version bump(version current, part raised) {
    switch (raised) {
    case part::major: return version {.major = current.major + 1u32, .minor = 0u32, .patch = 0u32};
    case part::minor: return version {.minor = current.minor + 1u32, .patch = 0u32, ...current};
    case part::patch: return version {.patch = current.patch + 1u32, ...current};
    }
}

/* MAJOR.MINOR.PATCH as text. */
std.string::string text(const version* value) throws std.alloc::alloc_error {
    return f"{value->major}.{value->minor}.{value->patch}";
}
