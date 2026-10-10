#include "c_layouts.h"

double layout_value_real(layout_value value) {
    return value.real;
}

layout_value layout_value_of_integer(int integer) {
    layout_value value = {0};
    value.integer = integer;
    return value;
}

int layout_packed_sum(struct layout_packed packed) {
    return (int)packed.tag + packed.count + (int)packed.flags;
}

struct layout_packed layout_packed_make(unsigned char tag, int count, unsigned short flags) {
    struct layout_packed packed = {0};
    packed.tag = tag;
    packed.count = count;
    packed.flags = flags;
    return packed;
}

int layout_aligned_get(const struct layout_aligned *aligned) {
    return ((unsigned long)aligned % 16U) == 0U ? aligned->x : -1;
}

int layout_field_aligned_sum(struct layout_field_aligned value) {
    return (int)value.tag + value.value;
}

long long layout_bits_sum(struct layout_bits bits) {
    return (long long)bits.a + (long long)bits.b + (long long)bits.c + (long long)bits.d +
           (long long)bits.e + (long long)bits.f + (long long)bits.g;
}

struct layout_bits layout_bits_make(void) {
    struct layout_bits bits = {0};
    bits.a = 5U;
    bits.b = 17U;
    bits.c = -33;
    bits.d = 3U;
    bits.e = 0xABCDE12345ULL;
    bits.f = 1;
    bits.g = -200;
    return bits;
}

int layout_packed_bits_b(const struct layout_packed_bits *bits) {
    return bits->b;
}
