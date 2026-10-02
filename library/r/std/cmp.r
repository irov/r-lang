module std.cmp;

/* R-SLIB-CMP-0001: total equality and ordering as traits over the scalar types, with the
   ordering enum and the min/max/clamp helpers over any Ordered type. */
enum ordering {
    less,
    equal,
    greater
};

trait Equal {
    bool eq(const Self* this, const Self* other);
};

trait Ordered {
    ordering cmp(const Self* this, const Self* other);
};

impl Equal for bool {
    bool eq(const bool* this, const bool* other) {
        return *this == *other;
    }
};

impl Ordered for bool {
    ordering cmp(const bool* this, const bool* other) {
        if (*this == *other) { return ordering::equal; }
        if (*this == false) { return ordering::less; }
        return ordering::greater;
    }
};

impl Equal for char {
    bool eq(const char* this, const char* other) {
        return *this == *other;
    }
};

impl Ordered for char {
    ordering cmp(const char* this, const char* other) {
        if (*this < *other) { return ordering::less; }
        if (*this > *other) { return ordering::greater; }
        return ordering::equal;
    }
};

impl Equal for i8 {
    bool eq(const i8* this, const i8* other) {
        return *this == *other;
    }
};

impl Ordered for i8 {
    ordering cmp(const i8* this, const i8* other) {
        if (*this < *other) { return ordering::less; }
        if (*this > *other) { return ordering::greater; }
        return ordering::equal;
    }
};

impl Equal for i16 {
    bool eq(const i16* this, const i16* other) {
        return *this == *other;
    }
};

impl Ordered for i16 {
    ordering cmp(const i16* this, const i16* other) {
        if (*this < *other) { return ordering::less; }
        if (*this > *other) { return ordering::greater; }
        return ordering::equal;
    }
};

impl Equal for i32 {
    bool eq(const i32* this, const i32* other) {
        return *this == *other;
    }
};

impl Ordered for i32 {
    ordering cmp(const i32* this, const i32* other) {
        if (*this < *other) { return ordering::less; }
        if (*this > *other) { return ordering::greater; }
        return ordering::equal;
    }
};

impl Equal for i64 {
    bool eq(const i64* this, const i64* other) {
        return *this == *other;
    }
};

impl Ordered for i64 {
    ordering cmp(const i64* this, const i64* other) {
        if (*this < *other) { return ordering::less; }
        if (*this > *other) { return ordering::greater; }
        return ordering::equal;
    }
};

impl Equal for isize {
    bool eq(const isize* this, const isize* other) {
        return *this == *other;
    }
};

impl Ordered for isize {
    ordering cmp(const isize* this, const isize* other) {
        if (*this < *other) { return ordering::less; }
        if (*this > *other) { return ordering::greater; }
        return ordering::equal;
    }
};

impl Equal for u8 {
    bool eq(const u8* this, const u8* other) {
        return *this == *other;
    }
};

impl Ordered for u8 {
    ordering cmp(const u8* this, const u8* other) {
        if (*this < *other) { return ordering::less; }
        if (*this > *other) { return ordering::greater; }
        return ordering::equal;
    }
};

impl Equal for u16 {
    bool eq(const u16* this, const u16* other) {
        return *this == *other;
    }
};

impl Ordered for u16 {
    ordering cmp(const u16* this, const u16* other) {
        if (*this < *other) { return ordering::less; }
        if (*this > *other) { return ordering::greater; }
        return ordering::equal;
    }
};

impl Equal for u32 {
    bool eq(const u32* this, const u32* other) {
        return *this == *other;
    }
};

impl Ordered for u32 {
    ordering cmp(const u32* this, const u32* other) {
        if (*this < *other) { return ordering::less; }
        if (*this > *other) { return ordering::greater; }
        return ordering::equal;
    }
};

impl Equal for u64 {
    bool eq(const u64* this, const u64* other) {
        return *this == *other;
    }
};

impl Ordered for u64 {
    ordering cmp(const u64* this, const u64* other) {
        if (*this < *other) { return ordering::less; }
        if (*this > *other) { return ordering::greater; }
        return ordering::equal;
    }
};

impl Equal for usize {
    bool eq(const usize* this, const usize* other) {
        return *this == *other;
    }
};

impl Ordered for usize {
    ordering cmp(const usize* this, const usize* other) {
        if (*this < *other) { return ordering::less; }
        if (*this > *other) { return ordering::greater; }
        return ordering::equal;
    }
};

impl Equal for f32 {
    bool eq(const f32* this, const f32* other) {
        return *this == *other;
    }
};

/* NaN orders after every number and equal to itself, so that the order is total. */
impl Ordered for f32 {
    ordering cmp(const f32* this, const f32* other) {
        bool this_nan = *this != *this;
        bool other_nan = *other != *other;
        if (this_nan == true) {
            if (other_nan == true) { return ordering::equal; }
            return ordering::greater;
        }
        if (other_nan == true) { return ordering::less; }
        if (*this < *other) { return ordering::less; }
        if (*this > *other) { return ordering::greater; }
        return ordering::equal;
    }
};

impl Equal for f64 {
    bool eq(const f64* this, const f64* other) {
        return *this == *other;
    }
};

impl Ordered for f64 {
    ordering cmp(const f64* this, const f64* other) {
        bool this_nan = *this != *this;
        bool other_nan = *other != *other;
        if (this_nan == true) {
            if (other_nan == true) { return ordering::equal; }
            return ordering::greater;
        }
        if (other_nan == true) { return ordering::less; }
        if (*this < *other) { return ordering::less; }
        if (*this > *other) { return ordering::greater; }
        return ordering::equal;
    }
};

/* R-SLIB-CMP-0001 (L27): text compares by its UTF-8 bytes, which is the order of its code
   points; o orders none before some; a tuple, a fixed array, a slice and an array compare their
   elements in order, and a sequence that is a prefix of a longer one orders first. */
protected ordering compare_bytes(const u8[] left, const u8[] right) {
    usize left_count = len(left);
    usize right_count = len(right);
    usize shared = left_count;
    if (right_count < shared) { shared = right_count; }
    usize index = 0usize;
    while (index < shared) {
        if (left[index] < right[index]) { return ordering::less; }
        if (left[index] > right[index]) { return ordering::greater; }
        index += 1usize;
    }
    if (left_count < right_count) { return ordering::less; }
    if (left_count > right_count) { return ordering::greater; }
    return ordering::equal;
}

@generic<T: Equal>
protected bool equal_elements(const T[] left, const T[] right) {
    usize count = len(left);
    if (count != len(right)) { return false; }
    usize index = 0usize;
    while (index < count) {
        bool same = left[index].eq(&right[index]);
        if (same == false) { return false; }
        index += 1usize;
    }
    return true;
}

@generic<T: Ordered>
protected ordering compare_elements(const T[] left, const T[] right) {
    usize left_count = len(left);
    usize right_count = len(right);
    usize shared = left_count;
    if (right_count < shared) { shared = right_count; }
    usize index = 0usize;
    while (index < shared) {
        ordering order = left[index].cmp(&right[index]);
        if (order != ordering::equal) { return order; }
        index += 1usize;
    }
    if (left_count < right_count) { return ordering::less; }
    if (left_count > right_count) { return ordering::greater; }
    return ordering::equal;
}

impl Equal for str {
    bool eq(const str* this, const str* other) {
        ordering order = compare_bytes(*this, *other);
        return order == ordering::equal;
    }
};

impl Ordered for str {
    ordering cmp(const str* this, const str* other) {
        return compare_bytes(*this, *other);
    }
};

@generic<T: Equal>
impl Equal for o<T> {
    bool eq(const (o<T>)* this, const (o<T>)* other) {
        switch (*this) {
        case variant o::some(left):
            switch (*other) {
            case variant o::some(right): return left->eq(right);
            case variant o::none: return false;
            }
        case variant o::none:
            switch (*other) {
            case variant o::some(_): return false;
            case variant o::none: return true;
            }
        }
        return false;
    }
};

@generic<T: Ordered>
impl Ordered for o<T> {
    ordering cmp(const (o<T>)* this, const (o<T>)* other) {
        switch (*this) {
        case variant o::some(left):
            switch (*other) {
            case variant o::some(right): return left->cmp(right);
            case variant o::none: return ordering::greater;
            }
        case variant o::none:
            switch (*other) {
            case variant o::some(_): return ordering::less;
            case variant o::none: return ordering::equal;
            }
        }
        return ordering::equal;
    }
};

@generic<T: Equal, const usize N>
impl Equal for T[N] {
    bool eq(const (T[N])* this, const (T[N])* other) {
        return equal_elements(*this, *other);
    }
};

@generic<T: Ordered, const usize N>
impl Ordered for T[N] {
    ordering cmp(const (T[N])* this, const (T[N])* other) {
        return compare_elements(*this, *other);
    }
};

@generic<T: Equal>
impl Equal for const T[] {
    bool eq(const (const T[])* this, const (const T[])* other) {
        return equal_elements(*this, *other);
    }
};

@generic<T: Ordered>
impl Ordered for const T[] {
    ordering cmp(const (const T[])* this, const (const T[])* other) {
        return compare_elements(*this, *other);
    }
};

@generic<T: Equal>
impl Equal for T[] {
    bool eq(const (T[])* this, const (T[])* other) {
        return equal_elements(*this, *other);
    }
};

@generic<T: Ordered>
impl Ordered for T[] {
    ordering cmp(const (T[])* this, const (T[])* other) {
        return compare_elements(*this, *other);
    }
};

@generic<A: Equal, B: Equal>
impl Equal for (A, B) {
    bool eq(const (A, B)* this, const (A, B)* other) {
        if (this->0.eq(&other->0) == false) { return false; }
        return this->1.eq(&other->1);
    }
};

@generic<A: Ordered, B: Ordered>
impl Ordered for (A, B) {
    ordering cmp(const (A, B)* this, const (A, B)* other) {
        ordering order0 = this->0.cmp(&other->0);
        if (order0 != ordering::equal) { return order0; }
        return this->1.cmp(&other->1);
    }
};

@generic<A: Equal, B: Equal, C: Equal>
impl Equal for (A, B, C) {
    bool eq(const (A, B, C)* this, const (A, B, C)* other) {
        if (this->0.eq(&other->0) == false) { return false; }
        if (this->1.eq(&other->1) == false) { return false; }
        return this->2.eq(&other->2);
    }
};

@generic<A: Ordered, B: Ordered, C: Ordered>
impl Ordered for (A, B, C) {
    ordering cmp(const (A, B, C)* this, const (A, B, C)* other) {
        ordering order0 = this->0.cmp(&other->0);
        if (order0 != ordering::equal) { return order0; }
        ordering order1 = this->1.cmp(&other->1);
        if (order1 != ordering::equal) { return order1; }
        return this->2.cmp(&other->2);
    }
};

@generic<A: Equal, B: Equal, C: Equal, D: Equal>
impl Equal for (A, B, C, D) {
    bool eq(const (A, B, C, D)* this, const (A, B, C, D)* other) {
        if (this->0.eq(&other->0) == false) { return false; }
        if (this->1.eq(&other->1) == false) { return false; }
        if (this->2.eq(&other->2) == false) { return false; }
        return this->3.eq(&other->3);
    }
};

@generic<A: Ordered, B: Ordered, C: Ordered, D: Ordered>
impl Ordered for (A, B, C, D) {
    ordering cmp(const (A, B, C, D)* this, const (A, B, C, D)* other) {
        ordering order0 = this->0.cmp(&other->0);
        if (order0 != ordering::equal) { return order0; }
        ordering order1 = this->1.cmp(&other->1);
        if (order1 != ordering::equal) { return order1; }
        ordering order2 = this->2.cmp(&other->2);
        if (order2 != ordering::equal) { return order2; }
        return this->3.cmp(&other->3);
    }
};

@generic<A: Equal, B: Equal, C: Equal, D: Equal, E: Equal>
impl Equal for (A, B, C, D, E) {
    bool eq(const (A, B, C, D, E)* this, const (A, B, C, D, E)* other) {
        if (this->0.eq(&other->0) == false) { return false; }
        if (this->1.eq(&other->1) == false) { return false; }
        if (this->2.eq(&other->2) == false) { return false; }
        if (this->3.eq(&other->3) == false) { return false; }
        return this->4.eq(&other->4);
    }
};

@generic<A: Ordered, B: Ordered, C: Ordered, D: Ordered, E: Ordered>
impl Ordered for (A, B, C, D, E) {
    ordering cmp(const (A, B, C, D, E)* this, const (A, B, C, D, E)* other) {
        ordering order0 = this->0.cmp(&other->0);
        if (order0 != ordering::equal) { return order0; }
        ordering order1 = this->1.cmp(&other->1);
        if (order1 != ordering::equal) { return order1; }
        ordering order2 = this->2.cmp(&other->2);
        if (order2 != ordering::equal) { return order2; }
        ordering order3 = this->3.cmp(&other->3);
        if (order3 != ordering::equal) { return order3; }
        return this->4.cmp(&other->4);
    }
};

@generic<A: Equal, B: Equal, C: Equal, D: Equal, E: Equal, F: Equal>
impl Equal for (A, B, C, D, E, F) {
    bool eq(const (A, B, C, D, E, F)* this, const (A, B, C, D, E, F)* other) {
        if (this->0.eq(&other->0) == false) { return false; }
        if (this->1.eq(&other->1) == false) { return false; }
        if (this->2.eq(&other->2) == false) { return false; }
        if (this->3.eq(&other->3) == false) { return false; }
        if (this->4.eq(&other->4) == false) { return false; }
        return this->5.eq(&other->5);
    }
};

@generic<A: Ordered, B: Ordered, C: Ordered, D: Ordered, E: Ordered, F: Ordered>
impl Ordered for (A, B, C, D, E, F) {
    ordering cmp(const (A, B, C, D, E, F)* this, const (A, B, C, D, E, F)* other) {
        ordering order0 = this->0.cmp(&other->0);
        if (order0 != ordering::equal) { return order0; }
        ordering order1 = this->1.cmp(&other->1);
        if (order1 != ordering::equal) { return order1; }
        ordering order2 = this->2.cmp(&other->2);
        if (order2 != ordering::equal) { return order2; }
        ordering order3 = this->3.cmp(&other->3);
        if (order3 != ordering::equal) { return order3; }
        ordering order4 = this->4.cmp(&other->4);
        if (order4 != ordering::equal) { return order4; }
        return this->5.cmp(&other->5);
    }
};

@generic<A: Equal, B: Equal, C: Equal, D: Equal, E: Equal, F: Equal, G: Equal>
impl Equal for (A, B, C, D, E, F, G) {
    bool eq(const (A, B, C, D, E, F, G)* this, const (A, B, C, D, E, F, G)* other) {
        if (this->0.eq(&other->0) == false) { return false; }
        if (this->1.eq(&other->1) == false) { return false; }
        if (this->2.eq(&other->2) == false) { return false; }
        if (this->3.eq(&other->3) == false) { return false; }
        if (this->4.eq(&other->4) == false) { return false; }
        if (this->5.eq(&other->5) == false) { return false; }
        return this->6.eq(&other->6);
    }
};

@generic<A: Ordered, B: Ordered, C: Ordered, D: Ordered, E: Ordered, F: Ordered, G: Ordered>
impl Ordered for (A, B, C, D, E, F, G) {
    ordering cmp(const (A, B, C, D, E, F, G)* this, const (A, B, C, D, E, F, G)* other) {
        ordering order0 = this->0.cmp(&other->0);
        if (order0 != ordering::equal) { return order0; }
        ordering order1 = this->1.cmp(&other->1);
        if (order1 != ordering::equal) { return order1; }
        ordering order2 = this->2.cmp(&other->2);
        if (order2 != ordering::equal) { return order2; }
        ordering order3 = this->3.cmp(&other->3);
        if (order3 != ordering::equal) { return order3; }
        ordering order4 = this->4.cmp(&other->4);
        if (order4 != ordering::equal) { return order4; }
        ordering order5 = this->5.cmp(&other->5);
        if (order5 != ordering::equal) { return order5; }
        return this->6.cmp(&other->6);
    }
};

@generic<A: Equal, B: Equal, C: Equal, D: Equal, E: Equal, F: Equal, G: Equal, H: Equal>
impl Equal for (A, B, C, D, E, F, G, H) {
    bool eq(const (A, B, C, D, E, F, G, H)* this, const (A, B, C, D, E, F, G, H)* other) {
        if (this->0.eq(&other->0) == false) { return false; }
        if (this->1.eq(&other->1) == false) { return false; }
        if (this->2.eq(&other->2) == false) { return false; }
        if (this->3.eq(&other->3) == false) { return false; }
        if (this->4.eq(&other->4) == false) { return false; }
        if (this->5.eq(&other->5) == false) { return false; }
        if (this->6.eq(&other->6) == false) { return false; }
        return this->7.eq(&other->7);
    }
};

@generic<A: Ordered, B: Ordered, C: Ordered, D: Ordered, E: Ordered, F: Ordered, G: Ordered, H: Ordered>
impl Ordered for (A, B, C, D, E, F, G, H) {
    ordering cmp(const (A, B, C, D, E, F, G, H)* this, const (A, B, C, D, E, F, G, H)* other) {
        ordering order0 = this->0.cmp(&other->0);
        if (order0 != ordering::equal) { return order0; }
        ordering order1 = this->1.cmp(&other->1);
        if (order1 != ordering::equal) { return order1; }
        ordering order2 = this->2.cmp(&other->2);
        if (order2 != ordering::equal) { return order2; }
        ordering order3 = this->3.cmp(&other->3);
        if (order3 != ordering::equal) { return order3; }
        ordering order4 = this->4.cmp(&other->4);
        if (order4 != ordering::equal) { return order4; }
        ordering order5 = this->5.cmp(&other->5);
        if (order5 != ordering::equal) { return order5; }
        ordering order6 = this->6.cmp(&other->6);
        if (order6 != ordering::equal) { return order6; }
        return this->7.cmp(&other->7);
    }
};

/* Owned strings exist from the hosted profile upwards. */
@if (!(core::profile is freestanding) && !(core::profile is allocation)) {
    impl Equal for std.string::string {
        bool eq(const std.string::string* this, const std.string::string* other) {
            ordering order = compare_bytes(this->as_bytes(), other->as_bytes());
            return order == ordering::equal;
        }
    };

    impl Ordered for std.string::string {
        ordering cmp(const std.string::string* this, const std.string::string* other) {
            return compare_bytes(this->as_bytes(), other->as_bytes());
        }
    };
}

/* Arrays exist from the allocation profile upwards. */
@if (!(core::profile is freestanding)) {
    @generic<T: Equal>
    impl Equal for array<T> {
        bool eq(const (array<T>)* this, const (array<T>)* other) {
            return equal_elements(*this, *other);
        }
    };

    @generic<T: Ordered>
    impl Ordered for array<T> {
        ordering cmp(const (array<T>)* this, const (array<T>)* other) {
            return compare_elements(*this, *other);
        }
    };
}

/* R-SLIB-CMP-0002: helpers over any Ordered type; the result borrows from the arguments. */
@generic<T: Ordered>
const T* min(const T* left, const T* right) {
    ordering order = left->cmp(right);
    if (order == ordering::greater) { return right; }
    return left;
}

@generic<T: Ordered>
const T* max(const T* left, const T* right) {
    ordering order = left->cmp(right);
    if (order == ordering::less) { return right; }
    return left;
}

@generic<T: Ordered>
const T* clamp(const T* value, const T* low, const T* high) {
    ordering below = value->cmp(low);
    if (below == ordering::less) { return low; }
    ordering above = value->cmp(high);
    if (above == ordering::greater) { return high; }
    return value;
}

@generic<T: Ordered>
bool is_less(const T* left, const T* right) {
    ordering order = left->cmp(right);
    return order == ordering::less;
}

@generic<T: Equal>
bool is_equal(const T* left, const T* right) {
    bool same = left->eq(right);
    return same;
}
