module runtime.dicts;
import runtime.types;
import runtime.heap;
import runtime.types::{TypeInfo, Allocator, AllocationStatus, Dict, DictKeyInfo, DictStatus, DictSlotState, DictIndexSlot, DictEntryHeader, DictIterator, DictEntryRef};

// Insertion-ordered hash dictionaries (r_runtime_dict.h): one allocation holds a power-of-two
// table of index slots probed linearly and, after it, the dense entries (hash, key, value) in
// insertion order.
@header("r_runtime_dict.h")
extern "C" {
}

protected bool type_valid(TypeInfo type) {
    usize alignment = type.alignment as usize;
    return ((type.size as usize) != 0usize) && (alignment != 0usize) &&
           ((alignment & (alignment - 1usize)) == 0usize);
}

// `value` rounded up to `alignment`, a power of two, or false when that overflows.
protected bool align_up(usize value, usize alignment, usize* result) {
    usize mask = alignment - 1usize;
    if (value > runtime.types::size_maximum - mask) {
        return false;
    }
    *result = (value + mask) & (runtime.types::size_maximum ^ mask);
    return true;
}

protected usize max_occupied(usize capacity) {
    return ((capacity / 3usize) * 2usize) + (((capacity % 3usize) * 2usize) / 3usize);
}

protected raw DictIndexSlot*? slot_at(raw const Dict*? table, usize index) {
    unsafe {
        raw const void*? slot = core::offset_bytes(table->slots as raw const void*?,
                                                   index * (table->slot_size as usize));
        return core::cast_mutable(slot) as raw DictIndexSlot*?;
    }
}

protected raw DictEntryHeader*? entry_at(raw const Dict*? table, usize index) {
    unsafe {
        raw const void*? entry = core::offset_bytes(table->entries as raw const void*?,
                                                    index * (table->entry_size as usize));
        return core::cast_mutable(entry) as raw DictEntryHeader*?;
    }
}

protected raw void*? entry_key(raw const Dict*? table, usize index) {
    unsafe {
        return core::offset_bytes(entry_at(table, index), table->key_offset as usize);
    }
}

protected raw void*? entry_value(raw const Dict*? table, usize index) {
    unsafe {
        return core::offset_bytes(entry_at(table, index), table->value_offset as usize);
    }
}

protected u64 entry_hash(raw const Dict*? table, usize index) {
    unsafe {
        raw DictEntryHeader*? entry = entry_at(table, index);
        return entry->hash as u64;
    }
}

protected void set_entry_hash(raw const Dict*? table, usize index, u64 hash) {
    unsafe {
        raw DictEntryHeader*? entry = entry_at(table, index);
        entry->hash = hash as c_uint64;
    }
}

protected usize slot_entry(raw const Dict*? table, usize index) {
    unsafe {
        raw DictIndexSlot*? slot = slot_at(table, index);
        return slot->entry_index as usize;
    }
}

protected DictSlotState slot_state(raw const Dict*? table, usize index) {
    unsafe {
        raw DictIndexSlot*? slot = slot_at(table, index);
        return slot->state;
    }
}

protected void drop_with(TypeInfo type, raw void*? value) {
    unsafe {
        raw fn?(raw void*?) -> void drop_glue = type.drop_value;
        if (drop_glue != null) {
            drop_glue(value);
        }
    }
}

// Mirrored verbatim by the emitter (r_llvm_dict_mix in compiler/llvm/dicts.c): both sides must
// agree so that entries inserted through either path are found by the other.
protected u64 mix(u64 hash, u64 seed) {
    u64 start = hash ^ seed ^ 0x9e3779b97f4a7c15u64;
    u64 first = core::wrapping_mul(start ^ (start >> 30u64), 0xbf58476d1ce4e5b9u64);
    u64 second = core::wrapping_mul(first ^ (first >> 27u64), 0x94d049bb133111ebu64);
    return second ^ (second >> 31u64);
}

protected u64 hash_key(raw const Dict*? table, raw const void*? key) {
    unsafe {
        raw fn?(raw const void*?) -> c_uint64 hash = table->key.hash;
        return mix(hash(key) as u64, table->seed as u64);
    }
}

protected bool geometry(raw Dict* table, DictKeyInfo key, TypeInfo value) {
    unsafe {
        if ((key.hash == null) || (key.equal == null)) {
            return false;
        }
    }
    if ((type_valid(key.type) == false) || (type_valid(value) == false)) {
        return false;
    }
    usize entry_alignment = alignof(DictEntryHeader);
    if ((key.type.alignment as usize) > entry_alignment) {
        entry_alignment = key.type.alignment as usize;
    }
    if ((value.alignment as usize) > entry_alignment) {
        entry_alignment = value.alignment as usize;
    }
    usize key_offset = 0usize;
    if ((align_up(sizeof(DictEntryHeader), key.type.alignment as usize, &key_offset) == false) ||
        ((key.type.size as usize) > runtime.types::size_maximum - key_offset)) {
        return false;
    }
    usize value_offset = 0usize;
    if ((align_up(key_offset + (key.type.size as usize), value.alignment as usize, &value_offset) == false) ||
        ((value.size as usize) > runtime.types::size_maximum - value_offset)) {
        return false;
    }
    usize entry_size = 0usize;
    if (align_up(value_offset + (value.size as usize), entry_alignment, &entry_size) == false) {
        return false;
    }
    usize slot_alignment = alignof(DictIndexSlot);
    if (entry_alignment > slot_alignment) {
        slot_alignment = entry_alignment;
    }
    unsafe {
        table->key_offset = key_offset as c_size;
        table->value_offset = value_offset as c_size;
        table->entry_size = entry_size as c_size;
        table->slot_size = sizeof(DictIndexSlot) as c_size;
        table->entry_alignment = entry_alignment as c_size;
        table->slot_alignment = slot_alignment as c_size;
    }
    return true;
}

protected DictStatus storage_geometry(raw Dict* table, usize capacity, usize* entries_offset, usize* entry_capacity, usize* allocation_size) {
    unsafe {
        usize slot_size = table->slot_size as usize;
        if (slot_size > runtime.types::size_maximum / capacity) {
            return DictStatus::R_RUNTIME_DICT_SIZE_OVERFLOW;
        }
        if (align_up(slot_size * capacity, table->entry_alignment as usize, entries_offset) == false) {
            return DictStatus::R_RUNTIME_DICT_SIZE_OVERFLOW;
        }
        *entry_capacity = max_occupied(capacity);
        usize entry_size = table->entry_size as usize;
        if ((*entry_capacity != 0usize) && (entry_size > runtime.types::size_maximum / *entry_capacity)) {
            return DictStatus::R_RUNTIME_DICT_SIZE_OVERFLOW;
        }
        usize entry_bytes = entry_size * *entry_capacity;
        if (entry_bytes > runtime.types::size_maximum - *entries_offset) {
            return DictStatus::R_RUNTIME_DICT_SIZE_OVERFLOW;
        }
        *allocation_size = *entries_offset + entry_bytes;
    }
    return DictStatus::R_RUNTIME_DICT_OK;
}

protected DictStatus allocate_storage(raw Dict* table, usize capacity, raw (raw void*?)* slots, raw (raw void*?)* entries, usize* entries_offset, usize* entry_capacity) {
    usize allocation_size = 0usize;
    DictStatus geometry_status = storage_geometry(table, capacity, entries_offset, entry_capacity, &allocation_size);
    if (geometry_status != DictStatus::R_RUNTIME_DICT_OK) {
        return geometry_status;
    }
    unsafe {
        AllocationStatus status = runtime.heap::r_runtime_allocator_allocate(
            table->allocator, allocation_size as c_size, table->slot_alignment, slots);
        switch (status) {
        case AllocationStatus::R_RUNTIME_ALLOCATION_OK:
            break;
        case AllocationStatus::R_RUNTIME_ALLOCATION_EXHAUSTED:
            return DictStatus::R_RUNTIME_DICT_ALLOCATION_FAILED;
        case AllocationStatus::R_RUNTIME_ALLOCATION_SIZE_OVERFLOW:
            return DictStatus::R_RUNTIME_DICT_SIZE_OVERFLOW;
        case AllocationStatus::R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT:
            return DictStatus::R_RUNTIME_DICT_UNSUPPORTED_ALIGNMENT;
        default:
            return DictStatus::R_RUNTIME_DICT_INVALID;
        }
        runtime.types::memset(*slots, 0i32 as c_int, allocation_size as c_size) as void;
        *entries = core::offset_bytes(*slots, *entries_offset);
    }
    return DictStatus::R_RUNTIME_DICT_OK;
}

// The slot of `key`, or of the first tombstone or the empty slot where it would go; `found`
// tells which. A full table without the key gives its first tombstone from the probe start.
protected usize capacity_of(raw const Dict*? table) {
    unsafe {
        return table->capacity as usize;
    }
}

protected usize probe(raw const Dict*? table, raw const void*? key, u64 hash, bool* found) {
    usize capacity = capacity_of(table);
    usize first_tombstone = runtime.types::size_maximum;
    usize mask = capacity - 1usize;
    usize index = (hash as usize) & mask;
    for (usize visited = 0usize; visited < capacity; visited += 1usize) {
        unsafe {
            raw DictIndexSlot*? slot = slot_at(table, index);
            switch (slot->state) {
            case DictSlotState::R_RUNTIME_DICT_SLOT_EMPTY:
                *found = false;
                if (first_tombstone == runtime.types::size_maximum) {
                    return index;
                }
                return first_tombstone;
            case DictSlotState::R_RUNTIME_DICT_SLOT_TOMBSTONE:
                if (first_tombstone == runtime.types::size_maximum) {
                    first_tombstone = index;
                }
                break;
            default:
                usize entry = slot->entry_index as usize;
                raw fn?(raw const void*?, raw const void*?) -> c_bool equal = table->key.equal;
                if ((entry_hash(table, entry) == hash) &&
                    (equal(entry_key(table, entry) as raw const void*?, key) != 0i32 as c_bool)) {
                    *found = true;
                    return index;
                }
                break;
            }
        }
        index = (index + 1usize) & mask;
    }
    *found = false;
    return first_tombstone;
}

protected usize find_empty_slot(raw const Dict*? table, u64 hash) {
    unsafe {
        usize capacity = table->capacity as usize;
        usize mask = capacity - 1usize;
        usize index = (hash as usize) & mask;
        for (usize visited = 0usize; visited < capacity; visited += 1usize) {
            if (slot_state(table, index) == DictSlotState::R_RUNTIME_DICT_SLOT_EMPTY) {
                return index;
            }
            index = (index + 1usize) & mask;
        }
    }
    return runtime.types::size_maximum;
}

protected DictStatus rehash(raw Dict* table, usize capacity) {
    unsafe {
        Dict replacement = *table;
        raw void*? replacement_slots = null;
        raw void*? replacement_entries = null;
        usize replacement_offset = 0usize;
        usize replacement_entry_capacity = 0usize;
        DictStatus status = allocate_storage(table, capacity, &replacement_slots as raw (raw void*?)*,
                                             &replacement_entries as raw (raw void*?)*,
                                             &replacement_offset, &replacement_entry_capacity);
        if (status != DictStatus::R_RUNTIME_DICT_OK) {
            return status;
        }
        replacement.slots = replacement_slots;
        replacement.entries = replacement_entries;
        replacement.capacity = capacity as c_size;
        replacement.entry_capacity = replacement_entry_capacity as c_size;
        replacement.entries_offset = replacement_offset as c_size;
        replacement.tombstones = 0usize as c_size;
        raw const Dict* target = &replacement as raw const Dict*;
        raw const Dict* source = table as raw const Dict*;
        usize length = table->length as usize;
        for (usize entry = 0usize; entry < length; entry += 1usize) {
            u64 hash = entry_hash(source, entry);
            raw DictIndexSlot*? slot = slot_at(target, find_empty_slot(target, hash));
            slot->state = DictSlotState::R_RUNTIME_DICT_SLOT_LIVE;
            slot->entry_index = entry as c_size;
        }
        for (usize entry = 0usize; entry < length; entry += 1usize) {
            set_entry_hash(target, entry, entry_hash(source, entry));
            runtime.types::move_value(table->key.type, entry_key(target, entry), entry_key(source, entry));
            runtime.types::move_value(table->value, entry_value(target, entry), entry_value(source, entry));
        }
        runtime.heap::r_runtime_allocator_deallocate(table->slots, table->slot_alignment);
        table->slots = replacement_slots;
        table->entries = replacement_entries;
        table->capacity = capacity as c_size;
        table->entry_capacity = replacement_entry_capacity as c_size;
        table->entries_offset = replacement_offset as c_size;
        table->tombstones = 0usize as c_size;
    }
    return DictStatus::R_RUNTIME_DICT_OK;
}

protected DictStatus reserve_valid(raw Dict* table, usize additional) {
    unsafe {
        usize length = table->length as usize;
        usize current = table->capacity as usize;
        if (additional > runtime.types::size_maximum - length) {
            return DictStatus::R_RUNTIME_DICT_SIZE_OVERFLOW;
        }
        usize required = length + additional;
        if ((required == 0usize) && (current == 0usize)) {
            return DictStatus::R_RUNTIME_DICT_OK;
        }
        usize capacity = current;
        if (capacity == 0usize) {
            capacity = 8usize;
        }
        while (required > max_occupied(capacity)) {
            if (capacity > runtime.types::size_maximum / 2usize) {
                return DictStatus::R_RUNTIME_DICT_SIZE_OVERFLOW;
            }
            capacity *= 2usize;
        }
        if ((capacity == current) &&
            (additional <= (table->entry_capacity as usize) - (length + (table->tombstones as usize)))) {
            return DictStatus::R_RUNTIME_DICT_OK;
        }
        return rehash(table, capacity);
    }
}

@safety("R-RUNTIME-DICT-INITIALIZE", "table is writable")
extern "C" DictStatus r_runtime_dict_initialize(raw Dict* table, raw Allocator*? allocator, DictKeyInfo key, TypeInfo value, c_uint64 seed) {
    unsafe {
        runtime.types::memset(table as raw void*?, 0i32 as c_int, sizeof(Dict) as c_size) as void;
    }
    geometry(table, key, value) as void;
    unsafe {
        table->allocator = allocator;
        table->key = key;
        table->value = value;
        table->first_order = runtime.types::size_maximum as c_size;
        table->last_order = runtime.types::size_maximum as c_size;
        table->seed = seed;
    }
    return DictStatus::R_RUNTIME_DICT_OK;
}

@safety("R-RUNTIME-DICT-RESERVE", "table is initialized")
extern "C" DictStatus r_runtime_dict_reserve(raw Dict* table, c_size additional) {
    return reserve_valid(table, additional as usize);
}

protected void clear_valid(raw Dict* table) {
    unsafe {
        raw const Dict* view = table as raw const Dict*;
        usize index = table->length as usize;
        while (index != 0usize) {
            index -= 1usize;
            drop_with(table->value, entry_value(view, index));
            drop_with(table->key.type, entry_key(view, index));
        }
        if (table->slots != null) {
            runtime.types::memset(table->slots, 0i32 as c_int,
                                  ((table->slot_size as usize) * (table->capacity as usize)) as c_size) as void;
        }
        table->length = 0usize as c_size;
        table->tombstones = 0usize as c_size;
        table->first_order = runtime.types::size_maximum as c_size;
        table->last_order = runtime.types::size_maximum as c_size;
    }
}

@safety("R-RUNTIME-DICT-DESTROY", "table is initialized or zeroed")
extern "C" void r_runtime_dict_destroy(raw Dict* table) {
    unsafe {
        if (table->allocator == null) {
            return;
        }
    }
    clear_valid(table);
    unsafe {
        runtime.heap::r_runtime_allocator_deallocate(table->slots, table->slot_alignment);
        table->slots = null;
        table->entries = null;
        table->capacity = 0usize as c_size;
        table->entry_capacity = 0usize as c_size;
        table->entries_offset = 0usize as c_size;
    }
}

@safety("R-RUNTIME-DICT-WITH-CAPACITY", "table is writable")
extern "C" DictStatus r_runtime_dict_with_capacity(raw Dict* table, raw Allocator*? allocator, DictKeyInfo key, TypeInfo value, c_uint64 seed, c_size capacity) {
    r_runtime_dict_initialize(table, allocator, key, value, seed) as void;
    DictStatus status = r_runtime_dict_reserve(table, capacity);
    if (status != DictStatus::R_RUNTIME_DICT_OK) {
        r_runtime_dict_destroy(table);
    }
    return status;
}

@safety("R-RUNTIME-DICT-INSERT", "table is initialized; key and value move into the table; replaced_value is null or storage for a value; did_replace is writable")
extern "C" DictStatus r_runtime_dict_insert(raw Dict* table, raw void*? key, raw void*? value, raw void*? replaced_value, raw c_bool* did_replace) {
    unsafe {
        *did_replace = 0i32 as c_bool;
        raw const Dict* view = table as raw const Dict*;
        u64 hash = hash_key(view, key as raw const void*?);
        bool found = false;
        if ((table->capacity as usize) != 0usize) {
            usize slot_index = probe(view, key as raw const void*?, hash, &found);
            if (found == false) {
                slot_index as void;
            } else {
                usize entry = slot_entry(view, slot_index);
                if (replaced_value != null) {
                    runtime.types::move_value(table->value, replaced_value, entry_value(view, entry));
                } else {
                    drop_with(table->value, entry_value(view, entry));
                }
                drop_with(table->key.type, key);
                runtime.types::move_value(table->value, entry_value(view, entry), value);
                *did_replace = 1i32 as c_bool;
                return DictStatus::R_RUNTIME_DICT_OK;
            }
        }
        DictStatus status = reserve_valid(table, 1usize);
        if (status != DictStatus::R_RUNTIME_DICT_OK) {
            hash as void;
            return status;
        }
        raw DictIndexSlot*? slot = slot_at(view, probe(view, key as raw const void*?, hash, &found));
        if (slot->state == DictSlotState::R_RUNTIME_DICT_SLOT_TOMBSTONE) {
            table->tombstones = ((table->tombstones as usize) - 1usize) as c_size;
        }
        usize entry = table->length as usize;
        set_entry_hash(view, entry, hash);
        runtime.types::move_value(table->key.type, entry_key(view, entry), key);
        runtime.types::move_value(table->value, entry_value(view, entry), value);
        slot->state = DictSlotState::R_RUNTIME_DICT_SLOT_LIVE;
        slot->entry_index = entry as c_size;
        table->length = (entry + 1usize) as c_size;
        table->first_order = 0usize as c_size;
        table->last_order = entry as c_size;
    }
    return DictStatus::R_RUNTIME_DICT_OK;
}

@safety("R-RUNTIME-DICT-GET", "table is initialized; key addresses a key of its type")
extern "C" raw const void*? r_runtime_dict_get(raw const Dict* table, raw const void*? key) {
    unsafe {
        if ((table->capacity as usize) == 0usize) {
            return null;
        }
        bool found = false;
        usize slot_index = probe(table, key, hash_key(table, key), &found);
        if (found == false) {
            slot_index as void;
            return null;
        }
        return entry_value(table, slot_entry(table, slot_index)) as raw const void*?;
    }
}

@safety("R-RUNTIME-DICT-CONTAINS", "table is initialized; key addresses a key of its type")
extern "C" c_bool r_runtime_dict_contains(raw const Dict* table, raw const void*? key) {
    unsafe {
        if (r_runtime_dict_get(table, key) == null) {
            return 0i32 as c_bool;
        }
    }
    return 1i32 as c_bool;
}

@safety("R-RUNTIME-DICT-GET-MUT", "table is initialized; key addresses a key of its type")
extern "C" raw void*? r_runtime_dict_get_mut(raw Dict* table, raw const void*? key) {
    unsafe {
        return core::cast_mutable(r_runtime_dict_get(table as raw const Dict*, key));
    }
}

@safety("R-RUNTIME-DICT-REMOVE", "table is initialized; key addresses a key of its type; removed_value addresses storage for a value")
extern "C" c_bool r_runtime_dict_remove(raw Dict* table, raw const void*? key, raw void*? removed_value) {
    unsafe {
        if ((table->capacity as usize) == 0usize) {
            return 0i32 as c_bool;
        }
        raw const Dict* view = table as raw const Dict*;
        bool found = false;
        usize slot_index = probe(view, key, hash_key(view, key), &found);
        if (found == false) {
            slot_index as void;
            return 0i32 as c_bool;
        }
        raw DictIndexSlot*? removed_slot = slot_at(view, slot_index);
        usize removed = removed_slot->entry_index as usize;
        usize length = table->length as usize;
        runtime.types::move_value(table->value, removed_value, entry_value(view, removed));
        drop_with(table->key.type, entry_key(view, removed));
        for (usize entry = removed + 1usize; entry < length; entry += 1usize) {
            set_entry_hash(view, entry - 1usize, entry_hash(view, entry));
            runtime.types::move_value(table->key.type, entry_key(view, entry - 1usize), entry_key(view, entry));
            runtime.types::move_value(table->value, entry_value(view, entry - 1usize), entry_value(view, entry));
        }
        removed_slot->state = DictSlotState::R_RUNTIME_DICT_SLOT_TOMBSTONE;
        removed_slot->entry_index = runtime.types::size_maximum as c_size;
        for (usize index = 0usize; index < (table->capacity as usize); index += 1usize) {
            raw DictIndexSlot*? slot = slot_at(view, index);
            if ((slot->state == DictSlotState::R_RUNTIME_DICT_SLOT_LIVE) &&
                ((slot->entry_index as usize) > removed)) {
                slot->entry_index = ((slot->entry_index as usize) - 1usize) as c_size;
            }
        }
        usize remaining = length - 1usize;
        table->length = remaining as c_size;
        table->tombstones = ((table->tombstones as usize) + 1usize) as c_size;
        if (remaining == 0usize) {
            table->first_order = runtime.types::size_maximum as c_size;
            table->last_order = runtime.types::size_maximum as c_size;
        } else {
            table->first_order = 0usize as c_size;
            table->last_order = (remaining - 1usize) as c_size;
        }
    }
    return 1i32 as c_bool;
}

@safety("R-RUNTIME-DICT-CLEAR", "table is initialized or zeroed")
extern "C" void r_runtime_dict_clear(raw Dict* table) {
    unsafe {
        if (table->allocator == null) {
            return;
        }
    }
    clear_valid(table);
}

@safety("R-RUNTIME-DICT-ITER", "table is initialized and outlives the iterator")
extern "C" DictIterator r_runtime_dict_iter(raw const Dict* table) {
    unsafe {
        usize next = 0usize;
        if ((table->length as usize) == 0usize) {
            next = runtime.types::size_maximum;
        }
        return DictIterator {.dict_value = table, .next_order = next as c_size};
    }
}

@safety("R-RUNTIME-DICT-NEXT", "iterator came from r_runtime_dict_iter of a live table; entry is writable")
extern "C" c_bool r_runtime_dict_next(raw DictIterator* iterator, raw DictEntryRef* entry) {
    unsafe {
        usize index = iterator->next_order as usize;
        if (index == runtime.types::size_maximum) {
            return 0i32 as c_bool;
        }
        raw const Dict*? table = iterator->dict_value;
        if ((index + 1usize) < (table->length as usize)) {
            iterator->next_order = (index + 1usize) as c_size;
        } else {
            iterator->next_order = runtime.types::size_maximum as c_size;
        }
        *entry = DictEntryRef {.key = entry_key(table, index) as raw const void*?,
                               .value = entry_value(table, index) as raw const void*?};
    }
    return 1i32 as c_bool;
}

@safety("R-RUNTIME-DICT-DESTROY-COUNT", "table is initialized or zeroed")
extern "C" c_size r_runtime_dict_destroy_count(raw const Dict*? table) {
    unsafe {
        if (table->allocator == null) {
            return 0usize as c_size;
        }
        return table->length;
    }
}

@safety("R-RUNTIME-DICT-DESTROY-VALUE", "table is being destroyed; index is below its count")
extern "C" raw void*? r_runtime_dict_destroy_value(raw Dict*? table, c_size index) {
    unsafe {
        return entry_value(table as raw const Dict*?, index as usize);
    }
}

@safety("R-RUNTIME-DICT-DESTROY-LOCATE", "table is being destroyed; index is writable")
extern "C" c_bool r_runtime_dict_destroy_locate(raw const Dict*? table, raw const void*? value, raw c_size* index) {
    unsafe {
        usize size = table->entry_size as usize;
        if ((table->entries == null) || (size == 0usize)) {
            return 0i32 as c_bool;
        }
        usize base = core::pointer_address(table->entries) + (table->value_offset as usize);
        usize address = core::pointer_address(value);
        if (address < base) {
            return 0i32 as c_bool;
        }
        usize offset = address - base;
        if (((offset % size) != 0usize) || ((offset / size) >= (table->length as usize))) {
            return 0i32 as c_bool;
        }
        *index = (offset / size) as c_size;
    }
    return 1i32 as c_bool;
}

@safety("R-RUNTIME-DICT-DESTROY-KEY", "table is being destroyed; index is below its count")
extern "C" void r_runtime_dict_destroy_key(raw Dict*? table, c_size index) {
    unsafe {
        drop_with(table->key.type, entry_key(table as raw const Dict*?, index as usize));
    }
}

@safety("R-RUNTIME-DICT-DESTROY-SCRATCH", "table is being destroyed")
extern "C" raw TypeInfo* r_runtime_dict_destroy_scratch(raw Dict*? table) {
    unsafe {
        return &table->value as raw TypeInfo*;
    }
}

@safety("R-RUNTIME-DICT-DESTROY-FINISH", "every entry of the table is destroyed")
extern "C" void r_runtime_dict_destroy_finish(raw Dict*? table) {
    unsafe {
        if (table->allocator == null) {
            return;
        }
        runtime.heap::r_runtime_allocator_deallocate(table->slots, table->slot_alignment);
        table->slots = null;
        table->entries = null;
        table->length = 0usize as c_size;
        table->tombstones = 0usize as c_size;
        table->capacity = 0usize as c_size;
        table->entry_capacity = 0usize as c_size;
        table->entries_offset = 0usize as c_size;
        table->first_order = runtime.types::size_maximum as c_size;
        table->last_order = runtime.types::size_maximum as c_size;
    }
}
