#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* The C mirror of bench/dict_lookup.r: open addressing with linear probing, the runtime's
   64-bit mix, a two-thirds load factor and separately stored entries, like RRuntimeDict. */
typedef struct Entry {
    uint64_t hash;
    int32_t key;
    int32_t value;
} Entry;
typedef struct Slot {
    uint32_t state;
    uint32_t entry;
} Slot;
typedef struct Dict {
    Slot *slots;
    Entry *entries;
    size_t capacity;
    size_t count;
} Dict;

static uint64_t mix(uint64_t hash) {
    uint64_t mixed = hash ^ UINT64_C(0x9e3779b97f4a7c15);
    mixed ^= mixed >> 30U;
    mixed *= UINT64_C(0xbf58476d1ce4e5b9);
    mixed ^= mixed >> 27U;
    mixed *= UINT64_C(0x94d049bb133111eb);
    mixed ^= mixed >> 31U;
    return mixed;
}

static int dict_rehash(Dict *dict, size_t capacity) {
    Slot *slots = calloc(capacity, sizeof(Slot));
    Entry *entries = malloc(((capacity / 3U) * 2U + 1U) * sizeof(Entry));
    if (slots == NULL || entries == NULL) {
        free(slots);
        free(entries);
        return 1;
    }
    for (size_t i = 0; i < dict->count; ++i) {
        size_t index = (size_t)(dict->entries[i].hash & (capacity - 1U));
        while (slots[index].state != 0U) {
            index = (index + 1U) & (capacity - 1U);
        }
        slots[index].state = 1U;
        slots[index].entry = (uint32_t)i;
        entries[i] = dict->entries[i];
    }
    free(dict->slots);
    free(dict->entries);
    dict->slots = slots;
    dict->entries = entries;
    dict->capacity = capacity;
    return 0;
}

static int dict_insert(Dict *dict, int32_t key, int32_t value) {
    uint64_t hash = mix((uint64_t)(uint32_t)key);
    if (dict->capacity == 0U || dict->count + 1U > (dict->capacity / 3U) * 2U) {
        if (dict_rehash(dict, dict->capacity == 0U ? 8U : dict->capacity * 2U))
            return 1;
    }
    size_t index = (size_t)(hash & (dict->capacity - 1U));
    for (;;) {
        Slot *slot = &dict->slots[index];
        if (slot->state == 0U) {
            dict->entries[dict->count].hash = hash;
            dict->entries[dict->count].key = key;
            dict->entries[dict->count].value = value;
            slot->state = 1U;
            slot->entry = (uint32_t)dict->count;
            dict->count += 1U;
            return 0;
        }
        if (dict->entries[slot->entry].hash == hash && dict->entries[slot->entry].key == key) {
            dict->entries[slot->entry].value = value;
            return 0;
        }
        index = (index + 1U) & (dict->capacity - 1U);
    }
}

static const int32_t *dict_get(const Dict *dict, int32_t key) {
    uint64_t hash = mix((uint64_t)(uint32_t)key);
    size_t index = (size_t)(hash & (dict->capacity - 1U));
    for (size_t visited = 0; visited < dict->capacity; ++visited) {
        const Slot *slot = &dict->slots[index];
        if (slot->state == 0U)
            return NULL;
        const Entry *entry = &dict->entries[slot->entry];
        if (entry->hash == hash && entry->key == key)
            return &entry->value;
        index = (index + 1U) & (dict->capacity - 1U);
    }
    return NULL;
}

int main(void) {
    const size_t iterations = (size_t)50000000u;
    Dict table = {0};
    uint64_t total = 0u, state = 12345u;

    for (int32_t key = 0; key < 65536; ++key) {
        if (dict_insert(&table, key * 7919, key))
            return 71;
    }
    for (size_t index = 0; index < iterations; ++index) {
        state = (state * 1664525u + 1013904223u) & 0xffffffffu;
        int32_t key = (int32_t)((state >> 16) & 65535u) * 7919;
        const int32_t *found = dict_get(&table, key);
        if (found == NULL) {
            free(table.slots);
            free(table.entries);
            return 70;
        }
        total += (uint64_t)*found;
    }
    free(table.slots);
    free(table.entries);
    return (int)(total % 109u);
}
