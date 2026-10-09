#include "r_runtime_dict.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static _Bool r_runtime_dict_type_valid(RRuntimeTypeInfo type) {
    return (type.size != 0U) && (type.alignment != 0U) &&
           ((type.alignment & (type.alignment - 1U)) == 0U);
}

static _Bool r_runtime_dict_align_up(size_t value, size_t alignment, size_t *result) {
    size_t mask = alignment - 1U;

    if (value > (SIZE_MAX - mask)) {
        return 0;
    }
    *result = (value + mask) & ~mask;
    return 1;
}

static size_t r_runtime_dict_max_occupied(size_t capacity) {
    return ((capacity / 3U) * 2U) + (((capacity % 3U) * 2U) / 3U);
}

static RRuntimeDictIndexSlot *r_runtime_dict_slot(const RRuntimeDict *dict, size_t index) {
    return (RRuntimeDictIndexSlot *)((unsigned char *)dict->slots + (index * dict->slot_size));
}

static RRuntimeDictEntryHeader *r_runtime_dict_entry(const RRuntimeDict *dict, size_t index) {
    return (RRuntimeDictEntryHeader *)((unsigned char *)dict->entries + (index * dict->entry_size));
}

static void *r_runtime_dict_entry_key(const RRuntimeDict *dict, size_t index) {
    return (unsigned char *)r_runtime_dict_entry(dict, index) + dict->key_offset;
}

static void *r_runtime_dict_entry_value(const RRuntimeDict *dict, size_t index) {
    return (unsigned char *)r_runtime_dict_entry(dict, index) + dict->value_offset;
}

static void r_runtime_dict_move(RRuntimeTypeInfo type, void *destination, void *source) {
    if (type.move_initialize != NULL) {
        type.move_initialize(destination, source);
    } else {
        (void)memcpy(destination, source, type.size);
    }
}

/* Mirrored verbatim by the emitter (r_llvm_dict_mix in compiler/llvm/dicts.c): both sides must
 * agree so that entries inserted through either path are found by the other. */
static uint64_t r_runtime_dict_mix(uint64_t hash, uint64_t seed) {
    uint64_t mixed = hash ^ seed ^ UINT64_C(0x9e3779b97f4a7c15);

    mixed ^= mixed >> 30U;
    mixed *= UINT64_C(0xbf58476d1ce4e5b9);
    mixed ^= mixed >> 27U;
    mixed *= UINT64_C(0x94d049bb133111eb);
    mixed ^= mixed >> 31U;
    return mixed;
}

static _Bool
r_runtime_dict_geometry(RRuntimeDict *dict, RRuntimeDictKeyInfo key, RRuntimeTypeInfo value) {
    size_t key_end;
    size_t value_end;
    size_t entry_alignment = _Alignof(RRuntimeDictEntryHeader);

    if ((key.hash == NULL) || (key.equal == NULL) || !r_runtime_dict_type_valid(key.type) ||
        !r_runtime_dict_type_valid(value)) {
        return 0;
    }
    if (key.type.alignment > entry_alignment) {
        entry_alignment = key.type.alignment;
    }
    if (value.alignment > entry_alignment) {
        entry_alignment = value.alignment;
    }
    if (!r_runtime_dict_align_up(
            sizeof(RRuntimeDictEntryHeader), key.type.alignment, &dict->key_offset) ||
        (key.type.size > (SIZE_MAX - dict->key_offset))) {
        return 0;
    }
    key_end = dict->key_offset + key.type.size;
    if (!r_runtime_dict_align_up(key_end, value.alignment, &dict->value_offset) ||
        (value.size > (SIZE_MAX - dict->value_offset))) {
        return 0;
    }
    value_end = dict->value_offset + value.size;
    if (!r_runtime_dict_align_up(value_end, entry_alignment, &dict->entry_size)) {
        return 0;
    }
    dict->slot_size = sizeof(RRuntimeDictIndexSlot);
    dict->entry_alignment = entry_alignment;
    dict->slot_alignment = entry_alignment > _Alignof(RRuntimeDictIndexSlot)
                               ? entry_alignment
                               : _Alignof(RRuntimeDictIndexSlot);
    return 1;
}

static RRuntimeDictStatus r_runtime_dict_storage_geometry(const RRuntimeDict *dict,
                                                          size_t capacity,
                                                          size_t *entries_offset,
                                                          size_t *entry_capacity,
                                                          size_t *allocation_size) {
    size_t slot_bytes;
    size_t entry_bytes;

    if (dict->slot_size > (SIZE_MAX / capacity)) {
        return R_RUNTIME_DICT_SIZE_OVERFLOW;
    }
    slot_bytes = dict->slot_size * capacity;
    if (!r_runtime_dict_align_up(slot_bytes, dict->entry_alignment, entries_offset)) {
        return R_RUNTIME_DICT_SIZE_OVERFLOW;
    }
    *entry_capacity = r_runtime_dict_max_occupied(capacity);
    if ((*entry_capacity != 0U) && (dict->entry_size > (SIZE_MAX / *entry_capacity))) {
        return R_RUNTIME_DICT_SIZE_OVERFLOW;
    }
    entry_bytes = dict->entry_size * *entry_capacity;
    if (entry_bytes > (SIZE_MAX - *entries_offset)) {
        return R_RUNTIME_DICT_SIZE_OVERFLOW;
    }
    *allocation_size = *entries_offset + entry_bytes;
    return R_RUNTIME_DICT_OK;
}

static RRuntimeDictStatus r_runtime_dict_allocate_storage(RRuntimeDict *dict,
                                                          size_t capacity,
                                                          void **slots,
                                                          void **entries,
                                                          size_t *entries_offset,
                                                          size_t *entry_capacity) {
    RRuntimeAllocationStatus allocation_status;
    RRuntimeDictStatus geometry_status;
    size_t allocation_size;

    geometry_status = r_runtime_dict_storage_geometry(
        dict, capacity, entries_offset, entry_capacity, &allocation_size);
    if (geometry_status != R_RUNTIME_DICT_OK) {
        return geometry_status;
    }
    allocation_status =
        r_runtime_allocator_allocate(dict->allocator, allocation_size, dict->slot_alignment, slots);
    if (allocation_status == R_RUNTIME_ALLOCATION_EXHAUSTED) {
        return R_RUNTIME_DICT_ALLOCATION_FAILED;
    }
    if (allocation_status == R_RUNTIME_ALLOCATION_SIZE_OVERFLOW) {
        return R_RUNTIME_DICT_SIZE_OVERFLOW;
    }
    if (allocation_status == R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT) {
        return R_RUNTIME_DICT_UNSUPPORTED_ALIGNMENT;
    }
    if (allocation_status != R_RUNTIME_ALLOCATION_OK) {
        return R_RUNTIME_DICT_INVALID;
    }
    (void)memset(*slots, 0, allocation_size);
    *entries = (unsigned char *)*slots + *entries_offset;
    return R_RUNTIME_DICT_OK;
}

static size_t
r_runtime_dict_probe(const RRuntimeDict *dict, const void *key, uint64_t hash, _Bool *found) {
    size_t index = (size_t)(hash & (uint64_t)(dict->capacity - 1U));
    size_t first_tombstone = SIZE_MAX;
    size_t visited;

    for (visited = 0U; visited < dict->capacity; ++visited) {
        RRuntimeDictIndexSlot *slot = r_runtime_dict_slot(dict, index);

        if (slot->state == R_RUNTIME_DICT_SLOT_EMPTY) {
            *found = 0;
            return first_tombstone == SIZE_MAX ? index : first_tombstone;
        }
        if (slot->state == R_RUNTIME_DICT_SLOT_TOMBSTONE) {
            if (first_tombstone == SIZE_MAX) {
                first_tombstone = index;
            }
        } else {
            if ((r_runtime_dict_entry(dict, slot->entry_index)->hash == hash) &&
                dict->key.equal(r_runtime_dict_entry_key(dict, slot->entry_index), key)) {
                *found = 1;
                return index;
            }
        }
        index = (index + 1U) & (dict->capacity - 1U);
    }
    *found = 0;
    return first_tombstone;
}

static size_t r_runtime_dict_find_empty_slot(const RRuntimeDict *dict, uint64_t hash) {
    size_t index = (size_t)(hash & (uint64_t)(dict->capacity - 1U));
    size_t visited;

    for (visited = 0U; visited < dict->capacity; ++visited) {
        if (r_runtime_dict_slot(dict, index)->state == R_RUNTIME_DICT_SLOT_EMPTY) {
            return index;
        }
        index = (index + 1U) & (dict->capacity - 1U);
    }
    return SIZE_MAX;
}

static RRuntimeDictStatus r_runtime_dict_rehash(RRuntimeDict *dict, size_t capacity) {
    RRuntimeDict replacement = *dict;
    void *replacement_slots = NULL;
    void *replacement_entries = NULL;
    size_t replacement_offset;
    size_t replacement_entry_capacity;
    size_t entry_index;
    RRuntimeDictStatus status;

    status = r_runtime_dict_allocate_storage(dict,
                                             capacity,
                                             &replacement_slots,
                                             &replacement_entries,
                                             &replacement_offset,
                                             &replacement_entry_capacity);
    if (status != R_RUNTIME_DICT_OK) {
        return status;
    }
    replacement.slots = replacement_slots;
    replacement.entries = replacement_entries;
    replacement.capacity = capacity;
    replacement.entry_capacity = replacement_entry_capacity;
    replacement.entries_offset = replacement_offset;
    replacement.tombstones = 0U;
    for (entry_index = 0U; entry_index < dict->length; ++entry_index) {
        const uint64_t hash = r_runtime_dict_entry(dict, entry_index)->hash;
        size_t slot_index = r_runtime_dict_find_empty_slot(&replacement, hash);
        RRuntimeDictIndexSlot *slot;

        slot = r_runtime_dict_slot(&replacement, slot_index);
        slot->state = R_RUNTIME_DICT_SLOT_LIVE;
        slot->entry_index = entry_index;
    }
    for (entry_index = 0U; entry_index < dict->length; ++entry_index) {
        r_runtime_dict_entry(&replacement, entry_index)->hash =
            r_runtime_dict_entry(dict, entry_index)->hash;
        r_runtime_dict_move(dict->key.type,
                            r_runtime_dict_entry_key(&replacement, entry_index),
                            r_runtime_dict_entry_key(dict, entry_index));
        r_runtime_dict_move(dict->value,
                            r_runtime_dict_entry_value(&replacement, entry_index),
                            r_runtime_dict_entry_value(dict, entry_index));
    }
    r_runtime_allocator_deallocate(dict->slots, dict->slot_alignment);
    dict->slots = replacement_slots;
    dict->entries = replacement_entries;
    dict->capacity = capacity;
    dict->entry_capacity = replacement_entry_capacity;
    dict->entries_offset = replacement_offset;
    dict->tombstones = 0U;
    return R_RUNTIME_DICT_OK;
}

RRuntimeDictStatus r_runtime_dict_initialize(RRuntimeDict *dict,
                                             RRuntimeAllocator *allocator,
                                             RRuntimeDictKeyInfo key,
                                             RRuntimeTypeInfo value,
                                             uint64_t seed) {
    (void)memset(dict, 0, sizeof(*dict));
    (void)r_runtime_dict_geometry(dict, key, value);
    dict->allocator = allocator;
    dict->key = key;
    dict->value = value;
    dict->first_order = SIZE_MAX;
    dict->last_order = SIZE_MAX;
    dict->seed = seed;
    return R_RUNTIME_DICT_OK;
}

RRuntimeDictStatus r_runtime_dict_with_capacity(RRuntimeDict *dict,
                                                RRuntimeAllocator *allocator,
                                                RRuntimeDictKeyInfo key,
                                                RRuntimeTypeInfo value,
                                                uint64_t seed,
                                                size_t capacity) {
    RRuntimeDictStatus status;

    (void)r_runtime_dict_initialize(dict, allocator, key, value, seed);
    status = r_runtime_dict_reserve(dict, capacity);
    if (status != R_RUNTIME_DICT_OK) {
        r_runtime_dict_destroy(dict);
    }
    return status;
}

static RRuntimeDictStatus r_runtime_dict_reserve_valid(RRuntimeDict *dict, size_t additional) {
    size_t required;
    size_t capacity;
    size_t available_without_rehash;

    if (additional > (SIZE_MAX - dict->length)) {
        return R_RUNTIME_DICT_SIZE_OVERFLOW;
    }
    required = dict->length + additional;
    if ((required == 0U) && (dict->capacity == 0U)) {
        return R_RUNTIME_DICT_OK;
    }
    capacity = dict->capacity == 0U ? 8U : dict->capacity;
    while (required > r_runtime_dict_max_occupied(capacity)) {
        if (capacity > (SIZE_MAX / 2U)) {
            return R_RUNTIME_DICT_SIZE_OVERFLOW;
        }
        capacity *= 2U;
    }
    if (capacity == dict->capacity) {
        available_without_rehash = dict->entry_capacity - (dict->length + dict->tombstones);
        if (additional <= available_without_rehash) {
            return R_RUNTIME_DICT_OK;
        }
    }
    return r_runtime_dict_rehash(dict, capacity);
}

RRuntimeDictStatus r_runtime_dict_reserve(RRuntimeDict *dict, size_t additional) {
    return r_runtime_dict_reserve_valid(dict, additional);
}

RRuntimeDictStatus r_runtime_dict_insert(
    RRuntimeDict *dict, void *key, void *value, void *replaced_value, _Bool *did_replace) {
    uint64_t hash;
    size_t slot_index;
    _Bool found;
    RRuntimeDictStatus status;
    RRuntimeDictIndexSlot *slot;
    size_t entry_index;

    *did_replace = 0;
    hash = r_runtime_dict_mix(dict->key.hash(key), dict->seed);
    if (dict->capacity != 0U) {
        slot_index = r_runtime_dict_probe(dict, key, hash, &found);
        if (found) {
            entry_index = r_runtime_dict_slot(dict, slot_index)->entry_index;
            if (replaced_value != NULL) {
                r_runtime_dict_move(
                    dict->value, replaced_value, r_runtime_dict_entry_value(dict, entry_index));
            } else if (dict->value.drop != NULL) {
                dict->value.drop(r_runtime_dict_entry_value(dict, entry_index));
            }
            if (dict->key.type.drop != NULL) {
                dict->key.type.drop(key);
            }
            r_runtime_dict_move(dict->value, r_runtime_dict_entry_value(dict, entry_index), value);
            *did_replace = 1;
            return R_RUNTIME_DICT_OK;
        }
    }
    status = r_runtime_dict_reserve_valid(dict, 1U);
    if (status != R_RUNTIME_DICT_OK) {
        return status;
    }
    slot_index = r_runtime_dict_probe(dict, key, hash, &found);
    slot = r_runtime_dict_slot(dict, slot_index);
    if (slot->state == R_RUNTIME_DICT_SLOT_TOMBSTONE) {
        dict->tombstones -= 1U;
    }
    entry_index = dict->length;
    r_runtime_dict_entry(dict, entry_index)->hash = hash;
    r_runtime_dict_move(dict->key.type, r_runtime_dict_entry_key(dict, entry_index), key);
    r_runtime_dict_move(dict->value, r_runtime_dict_entry_value(dict, entry_index), value);
    slot->state = R_RUNTIME_DICT_SLOT_LIVE;
    slot->entry_index = entry_index;
    dict->length += 1U;
    dict->first_order = 0U;
    dict->last_order = dict->length - 1U;
    return R_RUNTIME_DICT_OK;
}

const void *r_runtime_dict_get(const RRuntimeDict *dict, const void *key) {
    uint64_t hash;
    _Bool found;
    size_t slot_index;

    if (dict->capacity == 0U) {
        return NULL;
    }
    hash = r_runtime_dict_mix(dict->key.hash(key), dict->seed);
    slot_index = r_runtime_dict_probe(dict, key, hash, &found);
    if (!found) {
        return NULL;
    }
    return r_runtime_dict_entry_value(dict, r_runtime_dict_slot(dict, slot_index)->entry_index);
}

_Bool r_runtime_dict_contains(const RRuntimeDict *dict, const void *key) {
    return r_runtime_dict_get(dict, key) != NULL;
}

void *r_runtime_dict_get_mut(RRuntimeDict *dict, const void *key) {
    return (void *)r_runtime_dict_get(dict, key);
}

_Bool r_runtime_dict_remove(RRuntimeDict *dict, const void *key, void *removed_value) {
    uint64_t hash;
    _Bool found;
    size_t slot_index;
    RRuntimeDictIndexSlot *removed_slot;
    size_t removed_entry_index;
    size_t entry_index;
    size_t index;

    if (dict->capacity == 0U) {
        return 0;
    }
    hash = r_runtime_dict_mix(dict->key.hash(key), dict->seed);
    slot_index = r_runtime_dict_probe(dict, key, hash, &found);
    if (!found) {
        return 0;
    }
    removed_slot = r_runtime_dict_slot(dict, slot_index);
    removed_entry_index = removed_slot->entry_index;
    r_runtime_dict_move(
        dict->value, removed_value, r_runtime_dict_entry_value(dict, removed_entry_index));
    if (dict->key.type.drop != NULL) {
        dict->key.type.drop(r_runtime_dict_entry_key(dict, removed_entry_index));
    }
    for (entry_index = removed_entry_index + 1U; entry_index < dict->length; ++entry_index) {
        RRuntimeDictEntryHeader *source = r_runtime_dict_entry(dict, entry_index);
        RRuntimeDictEntryHeader *destination = r_runtime_dict_entry(dict, entry_index - 1U);

        destination->hash = source->hash;
        r_runtime_dict_move(dict->key.type,
                            r_runtime_dict_entry_key(dict, entry_index - 1U),
                            r_runtime_dict_entry_key(dict, entry_index));
        r_runtime_dict_move(dict->value,
                            r_runtime_dict_entry_value(dict, entry_index - 1U),
                            r_runtime_dict_entry_value(dict, entry_index));
    }
    removed_slot->state = R_RUNTIME_DICT_SLOT_TOMBSTONE;
    removed_slot->entry_index = SIZE_MAX;
    for (index = 0U; index < dict->capacity; ++index) {
        RRuntimeDictIndexSlot *slot = r_runtime_dict_slot(dict, index);

        if ((slot->state == R_RUNTIME_DICT_SLOT_LIVE) &&
            (slot->entry_index > removed_entry_index)) {
            slot->entry_index -= 1U;
        }
    }
    dict->length -= 1U;
    dict->tombstones += 1U;
    dict->first_order = dict->length == 0U ? SIZE_MAX : 0U;
    dict->last_order = dict->length == 0U ? SIZE_MAX : dict->length - 1U;
    return 1;
}

static void r_runtime_dict_clear_valid(RRuntimeDict *dict) {
    size_t index;

    index = dict->length;
    while (index != 0U) {
        index -= 1U;
        if (dict->value.drop != NULL) {
            dict->value.drop(r_runtime_dict_entry_value(dict, index));
        }
        if (dict->key.type.drop != NULL) {
            dict->key.type.drop(r_runtime_dict_entry_key(dict, index));
        }
    }
    if (dict->slots != NULL) {
        (void)memset(dict->slots, 0, dict->slot_size * dict->capacity);
    }
    dict->length = 0U;
    dict->tombstones = 0U;
    dict->first_order = SIZE_MAX;
    dict->last_order = SIZE_MAX;
}

void r_runtime_dict_clear(RRuntimeDict *dict) {
    if (dict->allocator == NULL) {
        return;
    }
    r_runtime_dict_clear_valid(dict);
}

void r_runtime_dict_destroy(RRuntimeDict *dict) {
    if (dict->allocator == NULL) {
        return;
    }
    r_runtime_dict_clear_valid(dict);
    r_runtime_allocator_deallocate(dict->slots, dict->slot_alignment);
    dict->slots = NULL;
    dict->entries = NULL;
    dict->capacity = 0U;
    dict->entry_capacity = 0U;
    dict->entries_offset = 0U;
}

RRuntimeDictIterator r_runtime_dict_iter(const RRuntimeDict *dict) {
    RRuntimeDictIterator iterator;

    iterator.dict = dict;
    iterator.next_order = dict->length == 0U ? SIZE_MAX : 0U;
    return iterator;
}

_Bool r_runtime_dict_next(RRuntimeDictIterator *iterator, RRuntimeDictEntryRef *entry) {
    size_t index;

    if (iterator->next_order == SIZE_MAX) {
        return 0;
    }
    index = iterator->next_order;
    iterator->next_order = (index + 1U) < iterator->dict->length ? index + 1U : SIZE_MAX;
    entry->key = r_runtime_dict_entry_key(iterator->dict, index);
    entry->value = r_runtime_dict_entry_value(iterator->dict, index);
    return 1;
}

size_t r_runtime_dict_destroy_count(const RRuntimeDict *dict) {
    return dict->allocator == NULL ? 0U : dict->length;
}

void *r_runtime_dict_destroy_value(RRuntimeDict *dict, size_t index) {
    return r_runtime_dict_entry_value(dict, index);
}

_Bool r_runtime_dict_destroy_locate(const RRuntimeDict *dict, const void *value, size_t *index) {
    const uintptr_t base = (uintptr_t)dict->entries + (uintptr_t)dict->value_offset;
    const uintptr_t address = (uintptr_t)value;
    uintptr_t offset;

    if ((dict->entries == NULL) || (dict->entry_size == 0U) || (address < base)) {
        return 0;
    }
    offset = address - base;
    if ((offset % dict->entry_size != 0U) || ((offset / dict->entry_size) >= dict->length)) {
        return 0;
    }
    *index = (size_t)(offset / dict->entry_size);
    return 1;
}

void r_runtime_dict_destroy_key(RRuntimeDict *dict, size_t index) {
    if (dict->key.type.drop != NULL) {
        dict->key.type.drop(r_runtime_dict_entry_key(dict, index));
    }
}

RRuntimeTypeInfo *r_runtime_dict_destroy_scratch(RRuntimeDict *dict) {
    return &dict->value;
}

void r_runtime_dict_destroy_finish(RRuntimeDict *dict) {
    if (dict->allocator == NULL) {
        return;
    }
    r_runtime_allocator_deallocate(dict->slots, dict->slot_alignment);
    dict->slots = NULL;
    dict->entries = NULL;
    dict->length = 0U;
    dict->tombstones = 0U;
    dict->capacity = 0U;
    dict->entry_capacity = 0U;
    dict->entries_offset = 0U;
    dict->first_order = SIZE_MAX;
    dict->last_order = SIZE_MAX;
}
