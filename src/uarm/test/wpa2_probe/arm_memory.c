#include "string.h"

void *memcpy(void *destination, const void *source, size_t length) {
    unsigned char *dst = destination;
    const unsigned char *src = source;
    for (size_t i = 0; i < length; ++i) dst[i] = src[i];
    return destination;
}

void *memset(void *destination, int value, size_t length) {
    unsigned char *dst = destination;
    for (size_t i = 0; i < length; ++i) dst[i] = value;
    return destination;
}

int memcmp(const void *left, const void *right, size_t length) {
    const unsigned char *a = left, *b = right;
    for (size_t i = 0; i < length; ++i)
        if (a[i] != b[i]) return a[i] - b[i];
    return 0;
}
