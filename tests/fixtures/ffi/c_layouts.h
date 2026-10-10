#ifndef C_LAYOUTS_H
#define C_LAYOUTS_H

/* R-AGG-0014, R-FFI-0061, R-FFI-0062 (B9.3): a union, packed and aligned structs and bit-fields,
   passed by value and through pointers. */
typedef union layout_value {
    int integer;
    double real;
    unsigned char bytes[3];
} layout_value;

struct __attribute__((packed)) layout_packed {
    unsigned char tag;
    int count;
    unsigned short flags;
};

struct __attribute__((aligned(16))) layout_aligned {
    int x;
};

struct layout_field_aligned {
    unsigned char tag;
    _Alignas(8) int value;
};

struct layout_bits {
    unsigned a : 3;
    unsigned b : 5;
    int c : 7;
    unsigned char d : 2;
    unsigned long long e : 40;
    _Bool f : 1;
    short g : 9;
};

struct __attribute__((packed)) layout_packed_bits {
    unsigned char a : 4;
    int b : 30;
    unsigned char c;
};

double layout_value_real(layout_value value);
layout_value layout_value_of_integer(int integer);
int layout_packed_sum(struct layout_packed packed);
struct layout_packed layout_packed_make(unsigned char tag, int count, unsigned short flags);
int layout_aligned_get(const struct layout_aligned *aligned);
int layout_field_aligned_sum(struct layout_field_aligned value);
long long layout_bits_sum(struct layout_bits bits);
struct layout_bits layout_bits_make(void);
int layout_packed_bits_b(const struct layout_packed_bits *bits);

#endif
