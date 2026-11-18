#ifndef PALMONE_RSN_WIRE_H
#define PALMONE_RSN_WIRE_H

#include <stdint.h>

enum {
    EapolMax = 192, KeyFrame = 99, KeyInfo = 5, KeyLength = 7, Replay = 9, Nonce = 17,
    KeyRsc = 65, Mic = 81, KeyDataLength = 97, KeyData = 99,
    GroupM1Info = 0x1382, GroupM2Info = 0x0302,
    RsnSnapSize = 8, GtkKdeHeaderSize = 8, GtkKdeSize = GtkKdeHeaderSize + 16,
    // Virtual firmware's opt-in RIDs for the E2 P3WLAN integration.
    RsnOffloadProfile = 0xffe4, RsnOffloadKeys = 0xffe5, RsnOffloadSupported = 0x5732,
    RsnOffloadGroupKey = 0xffe6,
    RsnOffloadKeyBytes = 38, RsnOffloadGroupKeyBytes = 24, RsnOffloadEapolBufferBytes = 512,
};

static const uint8_t rsnSnap[RsnSnapSize] = {0xaa, 0xaa, 3, 0, 0, 0, 0x88, 0x8e};
static const uint8_t gtkKdePrefix[] = {0xdd, GtkKdeSize - 2, 0, 0x0f, 0xac, 1};
static const uint8_t rsnPersonalCcmp[] = {
    0x30, 20, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 2, 0, 0,
};

static inline unsigned rsnReadBE(const uint8_t* p) { return (unsigned)p[0] << 8 | p[1]; }

static inline void rsnWriteBE(uint8_t* p, unsigned value) {
    p[0] = value >> 8;
    p[1] = value;
}

static inline uint64_t rsnReadReplay(const uint8_t* p) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value = (value << 8) | p[i];
    return value;
}

static inline void rsnWriteReplay(uint8_t* p, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) p[i] = value >> ((7 - i) * 8);
}

#endif
