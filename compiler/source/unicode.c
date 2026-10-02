#include "frontend_internal.h"
#include "unicode_data.h"

typedef struct RCodePointVector {
    uint32_t *items;
    size_t count;
    size_t capacity;
} RCodePointVector;

bool r_utf8_decode(
    const uint8_t *bytes, size_t length, size_t offset, uint32_t *code_point, size_t *width) {
    uint8_t first;
    uint32_t result;
    size_t required;
    size_t index;

    if ((bytes == NULL) || (code_point == NULL) || (width == NULL) || (offset >= length)) {
        return false;
    }
    first = bytes[offset];
    if (first <= UINT8_C(0x7F)) {
        *code_point = first;
        *width = 1U;
        return true;
    }
    if ((first >= UINT8_C(0xC2)) && (first <= UINT8_C(0xDF))) {
        result = (uint32_t)(first & UINT8_C(0x1F));
        required = 2U;
    } else if ((first >= UINT8_C(0xE0)) && (first <= UINT8_C(0xEF))) {
        result = (uint32_t)(first & UINT8_C(0x0F));
        required = 3U;
    } else if ((first >= UINT8_C(0xF0)) && (first <= UINT8_C(0xF4))) {
        result = (uint32_t)(first & UINT8_C(0x07));
        required = 4U;
    } else {
        return false;
    }
    if ((length - offset) < required) {
        return false;
    }
    for (index = 1U; index < required; ++index) {
        uint8_t continuation = bytes[offset + index];
        if ((continuation & UINT8_C(0xC0)) != UINT8_C(0x80)) {
            return false;
        }
        result = (result << 6U) | (uint32_t)(continuation & UINT8_C(0x3F));
    }
    if (((required == 3U) && (result < UINT32_C(0x800))) ||
        ((required == 4U) && (result < UINT32_C(0x10000))) || (result > UINT32_C(0x10FFFF)) ||
        ((result >= UINT32_C(0xD800)) && (result <= UINT32_C(0xDFFF)))) {
        return false;
    }
    *code_point = result;
    *width = required;
    return true;
}

static bool r_code_point_in_ranges(uint32_t code_point, const RUnicodeRange *ranges, size_t count) {
    size_t lower = 0U;
    size_t upper = count;

    while (lower < upper) {
        size_t middle = lower + ((upper - lower) / 2U);
        if (code_point < ranges[middle].first) {
            upper = middle;
        } else if (code_point > ranges[middle].last) {
            lower = middle + 1U;
        } else {
            return true;
        }
    }
    return false;
}

bool r_unicode_is_xid_start(uint32_t code_point) {
    return r_code_point_in_ranges(
        code_point, r_unicode_xid_start_ranges, r_unicode_xid_start_range_count);
}

bool r_unicode_is_xid_continue(uint32_t code_point) {
    return r_code_point_in_ranges(
        code_point, r_unicode_xid_continue_ranges, r_unicode_xid_continue_range_count);
}

static uint8_t r_unicode_combining_class(uint32_t code_point) {
    size_t lower = 0U;
    size_t upper = r_unicode_combining_class_count;

    while (lower < upper) {
        size_t middle = lower + ((upper - lower) / 2U);
        if (code_point < r_unicode_combining_classes[middle].code_point) {
            upper = middle;
        } else if (code_point > r_unicode_combining_classes[middle].code_point) {
            lower = middle + 1U;
        } else {
            return r_unicode_combining_classes[middle].combining_class;
        }
    }
    return UINT8_C(0);
}

static const RUnicodeDecomposition *r_unicode_find_decomposition(uint32_t code_point) {
    size_t lower = 0U;
    size_t upper = r_unicode_decomposition_count;

    while (lower < upper) {
        size_t middle = lower + ((upper - lower) / 2U);
        if (code_point < r_unicode_decompositions[middle].code_point) {
            upper = middle;
        } else if (code_point > r_unicode_decompositions[middle].code_point) {
            lower = middle + 1U;
        } else {
            return &r_unicode_decompositions[middle];
        }
    }
    return NULL;
}

static uint32_t r_unicode_find_composition(uint32_t first, uint32_t second) {
    size_t lower = 0U;
    size_t upper = r_unicode_composition_count;

    while (lower < upper) {
        size_t middle = lower + ((upper - lower) / 2U);
        const RUnicodeComposition *entry = &r_unicode_compositions[middle];
        if ((first < entry->first) || ((first == entry->first) && (second < entry->second))) {
            upper = middle;
        } else if ((first > entry->first) ||
                   ((first == entry->first) && (second > entry->second))) {
            lower = middle + 1U;
        } else {
            return entry->composite;
        }
    }
    return UINT32_C(0);
}

static bool
r_vector_push(RFrontendContext *context, RCodePointVector *vector, uint32_t code_point) {
    if (!r_grow_array(context,
                      (void **)&vector->items,
                      &vector->capacity,
                      sizeof(*vector->items),
                      vector->count + 1U)) {
        return false;
    }
    vector->items[vector->count] = code_point;
    vector->count += 1U;
    return true;
}

static bool r_decompose_code_point(RFrontendContext *context,
                                   RCodePointVector *output,
                                   uint32_t code_point,
                                   uint32_t depth) {
    static const uint32_t s_base = UINT32_C(0xAC00);
    static const uint32_t l_base = UINT32_C(0x1100);
    static const uint32_t v_base = UINT32_C(0x1161);
    static const uint32_t t_base = UINT32_C(0x11A7);
    static const uint32_t l_count = UINT32_C(19);
    static const uint32_t v_count = UINT32_C(21);
    static const uint32_t t_count = UINT32_C(28);
    static const uint32_t n_count = v_count * t_count;
    static const uint32_t s_count = l_count * n_count;
    const RUnicodeDecomposition *decomposition;
    uint32_t index;

    if (depth > UINT32_C(32)) {
        context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    if ((code_point >= s_base) && (code_point < (s_base + s_count))) {
        uint32_t s_index = code_point - s_base;
        uint32_t l = l_base + (s_index / n_count);
        uint32_t v = v_base + ((s_index % n_count) / t_count);
        uint32_t t = t_base + (s_index % t_count);
        if (!r_vector_push(context, output, l) || !r_vector_push(context, output, v)) {
            return false;
        }
        if (t != t_base) {
            return r_vector_push(context, output, t);
        }
        return true;
    }
    decomposition = r_unicode_find_decomposition(code_point);
    if (decomposition == NULL) {
        return r_vector_push(context, output, code_point);
    }
    for (index = 0U; index < decomposition->mapping_length; ++index) {
        size_t mapping_index = (size_t)decomposition->first_mapping + (size_t)index;
        if ((mapping_index >= r_unicode_decomposition_mapping_count) ||
            !r_decompose_code_point(
                context, output, r_unicode_decomposition_mappings[mapping_index], depth + 1U)) {
            return false;
        }
    }
    return true;
}

static void r_canonical_reorder(RCodePointVector *vector) {
    size_t index;

    for (index = 1U; index < vector->count; ++index) {
        size_t position = index;
        uint8_t current_class = r_unicode_combining_class(vector->items[position]);
        while ((position > 0U) && (current_class != UINT8_C(0))) {
            uint8_t previous_class = r_unicode_combining_class(vector->items[position - 1U]);
            uint32_t temporary;
            if ((previous_class == UINT8_C(0)) || (previous_class <= current_class)) {
                break;
            }
            temporary = vector->items[position - 1U];
            vector->items[position - 1U] = vector->items[position];
            vector->items[position] = temporary;
            position -= 1U;
        }
    }
}

static uint32_t r_hangul_compose(uint32_t first, uint32_t second) {
    static const uint32_t s_base = UINT32_C(0xAC00);
    static const uint32_t l_base = UINT32_C(0x1100);
    static const uint32_t v_base = UINT32_C(0x1161);
    static const uint32_t t_base = UINT32_C(0x11A7);
    static const uint32_t l_count = UINT32_C(19);
    static const uint32_t v_count = UINT32_C(21);
    static const uint32_t t_count = UINT32_C(28);
    static const uint32_t n_count = v_count * t_count;
    static const uint32_t s_count = l_count * n_count;

    if ((first >= l_base) && (first < (l_base + l_count)) && (second >= v_base) &&
        (second < (v_base + v_count))) {
        uint32_t l_index = first - l_base;
        uint32_t v_index = second - v_base;
        return s_base + ((l_index * v_count + v_index) * t_count);
    }
    if ((first >= s_base) && (first < (s_base + s_count)) &&
        (((first - s_base) % t_count) == UINT32_C(0)) && (second > t_base) &&
        (second < (t_base + t_count))) {
        return first + (second - t_base);
    }
    return UINT32_C(0);
}

static void r_canonical_compose(RCodePointVector *vector) {
    size_t input_index;
    size_t output_count;
    size_t starter_position;
    uint32_t starter;
    uint8_t last_class;

    if (vector->count == 0U) {
        return;
    }
    output_count = 1U;
    starter_position = 0U;
    starter = vector->items[0];
    last_class = r_unicode_combining_class(starter);

    for (input_index = 1U; input_index < vector->count; ++input_index) {
        uint32_t current = vector->items[input_index];
        uint8_t current_class = r_unicode_combining_class(current);
        uint32_t composite = r_hangul_compose(starter, current);
        if (composite == UINT32_C(0)) {
            composite = r_unicode_find_composition(starter, current);
        }
        if ((composite != UINT32_C(0)) &&
            ((last_class < current_class) || (last_class == UINT8_C(0)))) {
            vector->items[starter_position] = composite;
            starter = composite;
        } else {
            if (current_class == UINT8_C(0)) {
                starter_position = output_count;
                starter = current;
            }
            vector->items[output_count] = current;
            output_count += 1U;
            last_class = current_class;
        }
    }
    vector->count = output_count;
}

bool r_unicode_sequence_is_nfc(RFrontendContext *context,
                               const uint8_t *bytes,
                               size_t length,
                               bool *is_nfc) {
    RCodePointVector original = {0};
    RCodePointVector normalized = {0};
    size_t offset = 0U;
    size_t index;
    bool result = false;

    if ((context == NULL) || ((bytes == NULL) && (length != 0U)) || (is_nfc == NULL)) {
        return false;
    }
    while (offset < length) {
        uint32_t code_point;
        size_t width;
        if (!r_utf8_decode(bytes, length, offset, &code_point, &width) ||
            !r_vector_push(context, &original, code_point) ||
            !r_decompose_code_point(context, &normalized, code_point, UINT32_C(0))) {
            goto cleanup;
        }
        offset += width;
    }
    r_canonical_reorder(&normalized);
    r_canonical_compose(&normalized);
    *is_nfc = original.count == normalized.count;
    if (*is_nfc) {
        for (index = 0U; index < original.count; ++index) {
            if (original.items[index] != normalized.items[index]) {
                *is_nfc = false;
                break;
            }
        }
    }
    result = true;

cleanup:
    r_context_free(context, original.items);
    r_context_free(context, normalized.items);
    return result;
}
