module example.keystore.backends;
import example.keystore.store::{Store, Full, Record};

// Eight fixed slots: no allocation, and a ninth distinct key is refused.
struct Slots { u32[8] keys; u32[8] values; bool[8] used; };

impl Store for Slots {
    constexpr str name(const Slots* this) { return "slots"; }
    o<u32> get(const Slots* this, u32 key) {
        for (usize index = 0usize; index < 8usize; index++) {
            if (this->used[index] == true && this->keys[index] == key) { return o::some(this->values[index]); }
        }
        return o::none;
    }
    void put(Slots* this, u32 key, u32 value) throws Full, std.array::push_error<Record> {
        usize free = 8usize;
        for (usize index = 0usize; index < 8usize; index++) {
            if (this->used[index] == true && this->keys[index] == key) {
                this->values[index] = value;
                return;
            }
            if (this->used[index] == false && free == 8usize) { free = index; }
        }
        throw (free == 8usize) Full { .capacity = 8u32 };
        this->keys[free] = key;
        this->values[free] = value;
        this->used[free] = true;
    }
    bool remove(Slots* this, u32 key) {
        for (usize index = 0usize; index < 8usize; index++) {
            if (this->used[index] == true && this->keys[index] == key) {
                this->used[index] = false;
                return true;
            }
        }
        return false;
    }
    usize count(const Slots* this) {
        usize total = 0usize;
        for (usize index = 0usize; index < 8usize; index++) {
            if (this->used[index] == true) { total += 1usize; }
        }
        return total;
    }
};

// An append-only log: every put adds a record and retires the key's previous one.
struct Log { array<Record> records; };

protected o<usize> latest(const Log* log, u32 key) {
    usize index = len(log->records);
    while (index > 0usize) {
        index -= 1usize;
        o<const Record*> record = log->records.get(index);
        switch (record) {
        case variant o::some(entry):
            const Record* current = *entry;
            if (current->live == true && current->key == key) { return o::some(index); }
            break;
        case variant o::none: return o::none;
        }
    }
    return o::none;
}

impl Store for Log {
    constexpr str name(const Log* this) { return "log"; }
    o<u32> get(const Log* this, u32 key) {
        o<usize> position = latest(this, key);
        usize index = 0usize;
        switch (position) {
        case variant o::some(found): index = *found; break;
        case variant o::none: return o::none;
        }
        o<const Record*> record = this->records.get(index);
        switch (record) {
        case variant o::some(entry): return o::some((*entry)->value);
        case variant o::none: return o::none;
        }
    }
    void put(Log* this, u32 key, u32 value) throws Full, std.array::push_error<Record> {
        bool retired = this->remove(key);
        retired as void;
        this->records.push(Record { .key = key, .value = value, .live = true });
    }
    bool remove(Log* this, u32 key) {
        o<usize> position = latest(this, key);
        usize index = 0usize;
        switch (position) {
        case variant o::some(found): index = *found; break;
        case variant o::none: return false;
        }
        o<Record*> record = this->records.get_mut(index);
        switch (move record) {
        case variant o::some(entry): (*entry)->live = false; return true;
        case variant o::none: return false;
        }
    }
    usize count(const Log* this) {
        usize total = 0usize;
        for (const Record* record in &this->records) {
            if (record->live == true) { total += 1usize; }
        }
        return total;
    }
};
