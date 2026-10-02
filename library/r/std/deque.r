module std.deque;

/* R-SLIB-DEQUE-0001: a double-ended queue over two arrays; `heads` holds the front elements
   in reverse order and `tails` holds the rest in order, so that both ends push and pop in
   amortized constant time. */
@generic<T: unborrowed>
struct deque {
    array<T> heads;
    array<T> tails;
};

@generic<T: unborrowed>
deque<T> deque<T>::create() {
    array<T> heads = std.array::create::<T>();
    array<T> tails = std.array::create::<T>();
    return deque<T> { .heads = move heads, .tails = move tails };
}

@generic<T: unborrowed>
usize deque<T>::count(const deque<T>* this) {
    usize total = len(this->heads) + len(this->tails);
    return total;
}

@generic<T: unborrowed>
bool deque<T>::is_empty(const deque<T>* this) {
    usize total = len(this->heads) + len(this->tails);
    return total == 0usize;
}

@generic<T: unborrowed>
void deque<T>::push_back(deque<T>* this, T value) throws std.array::push_error<T> {
    std.array::push(&this->tails, move value);
}

@generic<T: unborrowed>
void deque<T>::push_front(deque<T>* this, T value) throws std.array::push_error<T> {
    std.array::push(&this->heads, move value);
}

/* Moves the whole back half into the front half in reverse order when the front is empty, so
   that pop_front stays amortized constant. The room is reserved before the first element moves,
   so a failed reservation moves nothing and returns false. */
@generic<T: unborrowed>
protected bool deque<T>::move_to_front(deque<T>* this) throws std.array::push_error<T> {
    try {
        std.array::reserve(&this->heads, len(this->tails));
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return false;
    }
    while (true) {
        o<T> moved = std.array::pop(&this->tails);
        switch (move moved) {
        case variant o::some(move value):
            std.array::push(&this->heads, move value);
            break;
        case variant o::none:
            return true;
        }
    }
}

@generic<T: unborrowed>
protected bool deque<T>::move_to_back(deque<T>* this) throws std.array::push_error<T> {
    try {
        std.array::reserve(&this->tails, len(this->heads));
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return false;
    }
    while (true) {
        o<T> moved = std.array::pop(&this->heads);
        switch (move moved) {
        case variant o::some(move value):
            std.array::push(&this->tails, move value);
            break;
        case variant o::none:
            return true;
        }
    }
}

/* Prepares the front half for pops; without room for the move the deque stays as it is. */
@generic<T: unborrowed>
void deque<T>::rebalance_front(deque<T>* this) throws std.array::push_error<T> {
    bool moved = this->move_to_front();
    moved as void;
}

/* Prepares the back half for pops; without room for the move the deque stays as it is. */
@generic<T: unborrowed>
void deque<T>::rebalance_back(deque<T>* this) throws std.array::push_error<T> {
    bool moved = this->move_to_back();
    moved as void;
}

/* R-SLIB-DEQUE-0001: without room to move the back half, the first element is taken from it in
   place, so a pop never loses or reorders an element. */
@generic<T: unborrowed>
@discardable
o<T> deque<T>::pop_front(deque<T>* this) throws std.array::push_error<T> {
    usize front_count = len(this->heads);
    if (front_count == 0usize) {
        bool moved = this->move_to_front();
        if (moved == false) {
            o<T> first = std.array::remove(&this->tails, 0usize);
            return move first;
        }
    }
    o<T> value = std.array::pop(&this->heads);
    return move value;
}

@generic<T: unborrowed>
@discardable
o<T> deque<T>::pop_back(deque<T>* this) throws std.array::push_error<T> {
    usize back_count = len(this->tails);
    if (back_count == 0usize) {
        bool moved = this->move_to_back();
        if (moved == false) {
            o<T> last = std.array::remove(&this->heads, 0usize);
            return move last;
        }
    }
    o<T> value = std.array::pop(&this->tails);
    return move value;
}

@generic<T: unborrowed>
o<const T*> deque<T>::front(const deque<T>* this) {
    usize front_count = len(this->heads);
    if (front_count != 0usize) {
        o<const T*> last = std.array::get(&this->heads, front_count - 1usize);
        return move last;
    }
    o<const T*> first = std.array::get(&this->tails, 0usize);
    return move first;
}

@generic<T: unborrowed>
o<const T*> deque<T>::back(const deque<T>* this) {
    usize back_count = len(this->tails);
    if (back_count != 0usize) {
        o<const T*> last = std.array::get(&this->tails, back_count - 1usize);
        return move last;
    }
    o<const T*> first = std.array::get(&this->heads, 0usize);
    return move first;
}

@generic<T: unborrowed>
void deque<T>::clear(deque<T>* this) {
    std.array::clear(&this->heads);
    std.array::clear(&this->tails);
}
