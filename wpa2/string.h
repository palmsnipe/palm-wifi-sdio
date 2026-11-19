// Freestanding memory routines used by the ARMlet and the selected BearSSL modules.
#ifndef WPA2_PROBE_STRING_H
#define WPA2_PROBE_STRING_H
#include <stddef.h>
void *memcpy(void *destination, const void *source, size_t length);
void *memset(void *destination, int value, size_t length);
int memcmp(const void *left, const void *right, size_t length);
#endif
