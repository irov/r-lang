module runtime.strings;
import runtime.types;
import runtime.types::{TypeInfo, Array, ArrayStatus, Allocator, String, StringStatus};
import runtime.arrays;
import runtime.utf8;

// Owned UTF-8 strings (r_runtime_string.h): an array of bytes that holds well-formed UTF-8.
@header("r_runtime_string.h")
extern "C" {
}

protected TypeInfo byte_type() {
    return TypeInfo {.size = 1usize as c_size, .alignment = 1usize as c_size,
                     .move_initialize = null, .drop_value = null};
}

protected StringStatus from_array(ArrayStatus status) {
    switch (status) {
    case ArrayStatus::R_RUNTIME_ARRAY_OK:
        return StringStatus::R_RUNTIME_STRING_OK;
    case ArrayStatus::R_RUNTIME_ARRAY_ALLOCATION_FAILED:
        return StringStatus::R_RUNTIME_STRING_ALLOCATION_FAILED;
    case ArrayStatus::R_RUNTIME_ARRAY_SIZE_OVERFLOW:
        return StringStatus::R_RUNTIME_STRING_SIZE_OVERFLOW;
    case ArrayStatus::R_RUNTIME_ARRAY_UNSUPPORTED_ALIGNMENT:
        return StringStatus::R_RUNTIME_STRING_UNSUPPORTED_ALIGNMENT;
    default:
        return StringStatus::R_RUNTIME_STRING_INVALID;
    }
}

protected bool valid_utf8(raw const c_uint8*? bytes, c_size length, raw c_size*? invalid_index) {
    c_bool valid = runtime.utf8::r_runtime_utf8_validate(bytes, length, invalid_index);
    return valid != 0i32 as c_bool;
}

@safety("R-RUNTIME-STRING-INITIALIZE", "text is writable")
extern "C" StringStatus r_runtime_string_initialize(raw String* text, raw Allocator*? allocator) {
    unsafe {
        runtime.arrays::r_runtime_array_initialize(&text->bytes as raw Array*, allocator, byte_type());
    }
    return StringStatus::R_RUNTIME_STRING_OK;
}

@safety("R-RUNTIME-STRING-WITH-CAPACITY", "text is writable")
extern "C" StringStatus r_runtime_string_with_capacity(raw String* text, raw Allocator*? allocator, c_size capacity) {
    unsafe {
        return from_array(runtime.arrays::r_runtime_array_with_capacity(
            &text->bytes as raw Array*, allocator, byte_type(), capacity));
    }
}

protected StringStatus copy_bytes(raw String* text, raw Allocator*? allocator, raw const c_uint8*? bytes, c_size length) {
    StringStatus status = r_runtime_string_with_capacity(text, allocator, length);
    if (status != StringStatus::R_RUNTIME_STRING_OK) {
        return status;
    }
    unsafe {
        if ((length as usize) != 0usize) {
            runtime.types::memcpy(text->bytes.data, bytes as raw const void*?, length) as void;
            text->bytes.length = length;
        }
    }
    return StringStatus::R_RUNTIME_STRING_OK;
}

@safety("R-RUNTIME-STRING-FROM-UTF8", "text is writable; bytes addresses length readable bytes or is null with length zero")
extern "C" StringStatus r_runtime_string_from_utf8(raw String* text, raw Allocator*? allocator, raw const c_uint8*? bytes, c_size length, raw c_size*? invalid_index) {
    if (valid_utf8(bytes, length, invalid_index) == false) {
        return StringStatus::R_RUNTIME_STRING_INVALID_UTF8;
    }
    return copy_bytes(text, allocator, bytes, length);
}

@safety("R-RUNTIME-STRING-FROM-VALID-UTF8", "text is writable; bytes addresses length bytes of well-formed UTF-8")
extern "C" StringStatus r_runtime_string_from_valid_utf8(raw String* text, raw Allocator*? allocator, raw const c_uint8*? bytes, c_size length) {
    return copy_bytes(text, allocator, bytes, length);
}

@safety("R-RUNTIME-STRING-FROM-BYTES", "text is writable; bytes is an initialized byte array whose buffer moves into the string")
extern "C" StringStatus r_runtime_string_from_bytes(raw String* text, raw Array* bytes, raw c_size*? invalid_index) {
    unsafe {
        if (valid_utf8(bytes->data as raw const c_uint8*?, bytes->length, invalid_index) == false) {
            return StringStatus::R_RUNTIME_STRING_INVALID_UTF8;
        }
        text->bytes = *bytes;
        bytes->data = null;
        bytes->length = 0usize as c_size;
        bytes->capacity = 0usize as c_size;
    }
    return StringStatus::R_RUNTIME_STRING_OK;
}

@safety("R-RUNTIME-STRING-LENGTH", "text is initialized")
extern "C" c_size r_runtime_string_length(raw const String* text) {
    unsafe {
        return text->bytes.length;
    }
}

@safety("R-RUNTIME-STRING-CAPACITY", "text is initialized")
extern "C" c_size r_runtime_string_capacity(raw const String* text) {
    unsafe {
        return text->bytes.capacity;
    }
}

@safety("R-RUNTIME-STRING-BYTES", "text is initialized")
extern "C" raw const c_uint8*? r_runtime_string_bytes(raw const String* text) {
    unsafe {
        if ((text->bytes.length as usize) == 0usize) {
            return null;
        }
        return text->bytes.data as raw const c_uint8*?;
    }
}

@safety("R-RUNTIME-STRING-RESERVE", "text is initialized")
extern "C" StringStatus r_runtime_string_reserve(raw String* text, c_size additional) {
    unsafe {
        return from_array(runtime.arrays::r_runtime_array_reserve(&text->bytes as raw Array*, additional));
    }
}

@safety("R-RUNTIME-STRING-APPEND", "text is initialized; bytes addresses length readable bytes outside the string or is null with length zero")
extern "C" StringStatus r_runtime_string_append(raw String* text, raw const c_uint8*? bytes, c_size length) {
    StringStatus status = r_runtime_string_reserve(text, length);
    if (status != StringStatus::R_RUNTIME_STRING_OK) {
        return status;
    }
    unsafe {
        usize count = length as usize;
        if (count != 0usize) {
            usize used = text->bytes.length as usize;
            runtime.types::memcpy(core::offset_bytes(text->bytes.data, used),
                                  bytes as raw const void*?, length) as void;
            text->bytes.length = (used + count) as c_size;
        }
    }
    return StringStatus::R_RUNTIME_STRING_OK;
}

@safety("R-RUNTIME-STRING-APPEND-UTF8", "text is initialized; bytes addresses length readable bytes outside the string or is null with length zero")
extern "C" StringStatus r_runtime_string_append_utf8(raw String* text, raw const c_uint8*? bytes, c_size length, raw c_size*? invalid_index) {
    if (valid_utf8(bytes, length, invalid_index) == false) {
        return StringStatus::R_RUNTIME_STRING_INVALID_UTF8;
    }
    return r_runtime_string_append(text, bytes, length);
}

@safety("R-RUNTIME-STRING-PUSH-SCALAR", "text is initialized")
extern "C" StringStatus r_runtime_string_push_scalar(raw String* text, c_uint32 scalar) {
    c_uint8[4] bytes = {};
    c_size length = 0usize as c_size;
    unsafe {
        c_bool encoded = runtime.utf8::r_runtime_utf8_encode(
            scalar, &bytes[0] as raw c_uint8*, &length as raw c_size*);
        if (encoded == 0i32 as c_bool) {
            return StringStatus::R_RUNTIME_STRING_INVALID;
        }
        return r_runtime_string_append(text, &bytes[0] as raw const c_uint8*, length);
    }
}

@safety("R-RUNTIME-STRING-TRUNCATE", "text is initialized")
extern "C" StringStatus r_runtime_string_truncate(raw String* text, c_size byte_length) {
    unsafe {
        if ((byte_length as usize) > (text->bytes.length as usize)) {
            return StringStatus::R_RUNTIME_STRING_OUT_OF_BOUNDS;
        }
        c_bool boundary = runtime.utf8::r_runtime_utf8_is_scalar_boundary(
            text->bytes.data as raw const c_uint8*?, text->bytes.length, byte_length);
        if (boundary == 0i32 as c_bool) {
            return StringStatus::R_RUNTIME_STRING_NOT_SCALAR_BOUNDARY;
        }
        text->bytes.length = byte_length;
    }
    return StringStatus::R_RUNTIME_STRING_OK;
}

@safety("R-RUNTIME-STRING-CLEAR", "text is initialized")
extern "C" void r_runtime_string_clear(raw String* text) {
    unsafe {
        text->bytes.length = 0usize as c_size;
    }
}

@safety("R-RUNTIME-STRING-INTO-BYTES", "text is initialized; bytes is writable")
extern "C" StringStatus r_runtime_string_into_bytes(raw String* text, raw Array* bytes) {
    unsafe {
        *bytes = text->bytes;
        text->bytes.data = null;
        text->bytes.length = 0usize as c_size;
        text->bytes.capacity = 0usize as c_size;
    }
    return StringStatus::R_RUNTIME_STRING_OK;
}

@safety("R-RUNTIME-STRING-DESTROY", "text is initialized or zeroed")
extern "C" void r_runtime_string_destroy(raw String* text) {
    unsafe {
        if (text->bytes.allocator == null) {
            return;
        }
        runtime.arrays::r_runtime_array_destroy(&text->bytes as raw Array*);
    }
}
