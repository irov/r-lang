module std.pool;

/* R-SLIB-POOL-0001: a pool of a fixed number of objects made up front. acquire lends a free object
   out as a lease, which puts it back when it is dropped, so the objects are reused without
   allocating; a pool and its leases may be used from any task or thread. */

/* Each object lives in a slot of its own, an array of one element made with the pool: lending
   and returning move the slot and allocate nothing. */
@generic<T: send & unborrowed>
protected struct pool_core {
    std.sync::mutex<array<array<T>>> free;
    usize capacity;
};

/* R-SLIB-POOL-0002: a pool of objects of type T. */
@generic<T: send & unborrowed>
struct pool { protected arc pool_core<T> core; };

/* R-SLIB-POOL-0003: an object lent out by a pool. */
@generic<T: send & unborrowed>
struct lease { protected array<T> slot; protected arc pool_core<T> home; };

/* Ends the life of an object that could not be put back. */
@generic<T: send & unborrowed>
protected void discard(array<T> slot) {
    std.array::clear(&slot);
}

/* Puts a slot back; the list never holds more slots than the capacity it was made with, so the
   push does not allocate. */
@generic<T: send & unborrowed>
protected void give_back(array<array<T>>* free, array<T> slot) {
    try {
        free->push(move slot);
    } catch (std.array::push_error<array<T>> rejected) {
        drop rejected;
    }
}

@generic<T: send & unborrowed>
drop(lease<T>* self) {
    array<T> none = [];
    array<T> slot = core::replace(&self->slot, move none);
    const pool_core<T>* home = &*self->home;
    std.sync::lock_result<array<array<T>>> locked = std.sync::lock(&home->free);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard): give_back(std.sync::mutex_guard_mut(&guard), move slot);
    case variant std.sync::lock_result::poisoned(move guard): give_back(std.sync::mutex_guard_mut(&guard), move slot);
    case variant std.sync::lock_result::would_deadlock: discard(move slot);
    }
}

/* R-SLIB-POOL-0002: a pool of the objects; its capacity is their number. */
@generic<T: send & unborrowed>
pool<T> pool<T>::create(array<T> objects) throws std.alloc::alloc_error {
    usize capacity = len(objects);
    array<array<T>> free = std.array::with_capacity::<array<T>>(capacity);
    while (len(objects) > 0usize) {
        o<T> taken = objects.pop();
        switch (move taken) {
        case variant o::some(move value):
            array<T> slot = std.array::with_capacity::<T>(1usize);
            try {
                slot.push(move value);
            } catch (std.array::push_error<T> rejected) {
                drop rejected;
            }
            give_back(&free, move slot);
        case variant o::none: break;
        }
    }
    drop objects;
    return pool<T> {.core = new arc pool_core<T> {.free = std.sync::mutex_new(move free), .capacity = capacity}};
}

@generic<T: send & unborrowed>
protected o<array<T>> take_free(array<array<T>>* free) {
    return free->pop();
}

/* R-SLIB-POOL-0003: a free object as a lease, or none when every object is lent out. */
@generic<T: send & unborrowed>
o<lease<T>> pool<T>::acquire(const pool<T>* this) {
    o<array<T>> taken = o::none;
    std.sync::lock_result<array<array<T>>> locked = std.sync::lock(&this->core->free);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard): taken = take_free(std.sync::mutex_guard_mut(&guard));
    case variant std.sync::lock_result::poisoned(move guard): taken = take_free(std.sync::mutex_guard_mut(&guard));
    case variant std.sync::lock_result::would_deadlock: break;
    }
    switch (move taken) {
    case variant o::some(move slot):
        return o::some(lease<T> {.slot = move slot, .home = std.arc::clone(&this->core)});
    case variant o::none: break;
    }
    return o::none;
}

@generic<T: send & unborrowed>
protected usize count_free(const array<array<T>>* free) {
    return len(*free);
}

/* R-SLIB-POOL-0002: the number of objects not lent out, and of all objects. */
@generic<T: send & unborrowed>
usize pool<T>::available(const pool<T>* this) {
    std.sync::lock_result<array<array<T>>> locked = std.sync::lock(&this->core->free);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard): return count_free(std.sync::mutex_guard_ref(&guard));
    case variant std.sync::lock_result::poisoned(move guard): return count_free(std.sync::mutex_guard_ref(&guard));
    case variant std.sync::lock_result::would_deadlock: break;
    }
    return 0usize;
}

@generic<T: send & unborrowed>
usize pool<T>::capacity(const pool<T>* this) {
    return this->core->capacity;
}

@generic<T: send & unborrowed>
pool<T> pool<T>::share(const pool<T>* this) {
    return pool<T> {.core = std.arc::clone(&this->core)};
}

/* R-SLIB-POOL-0003: the object of a lease. */
@generic<T: send & unborrowed>
const T* lease<T>::get(const lease<T>* this) {
    return &this->slot[0usize];
}

@generic<T: send & unborrowed>
T* lease<T>::get_mut(lease<T>* this) {
    return &this->slot[0usize];
}
