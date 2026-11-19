#ifndef PALMONE_CCMP_WIRE_H
#define PALMONE_CCMP_WIRE_H

#include <stdint.h>

// Three-address, non-QoS 802.11 data frames, CCMP-128.
enum {
    CcmpMacSize = 24, CcmpHeaderSize = 8, CcmpTagSize = 8, CcmpKeySize = 16,
    CcmpNonceSize = 13, CcmpAadSize = 22, CcmpOverhead = CcmpHeaderSize + CcmpTagSize,
    CcmpPairwiseId = 0, CcmpGroupId = 1, CcmpFromDs = 0x4208, CcmpToDs = 0x4108,
};
#define CCMP_MAX_PN UINT64_C(0xffffffffffff)

static inline uint64_t ccmpReadRsc(const uint8_t* bytes) {
    uint64_t pn = 0;
    for (unsigned i = 0; i < 6; ++i) pn |= (uint64_t)bytes[i] << (8 * i);
    return pn;
}

static inline uint64_t ccmpReadPn(const uint8_t* header) {
    return (uint64_t)header[0] | (uint64_t)header[1] << 8 | (uint64_t)header[4] << 16 |
        (uint64_t)header[5] << 24 | (uint64_t)header[6] << 32 | (uint64_t)header[7] << 40;
}

static inline void ccmpWriteHeader(uint8_t* header, uint64_t pn, unsigned keyId) {
    header[0] = pn;
    header[1] = pn >> 8;
    header[2] = 0;
    header[3] = 0x20 | (keyId << 6);
    for (unsigned i = 4; i < CcmpHeaderSize; ++i) header[i] = pn >> ((i - 2) * 8);
}

static inline unsigned ccmpReadKeyId(const uint8_t* header) { return header[3] >> 6; }

static inline int ccmpValidHeader(const uint8_t* header) {
    return header[2] == 0 && (header[3] & 0x3f) == 0x20;
}

static inline void ccmpMakeAadNonce(const uint8_t* mac, uint64_t pn, uint8_t* aad, uint8_t* nonce) {
    aad[0] = mac[0] & 0x8f;
    aad[1] = mac[1] & 0xc7;
    for (unsigned i = 0; i < 18; ++i) aad[2 + i] = mac[4 + i];
    aad[20] = mac[22] & 0x0f;
    aad[21] = 0;
    nonce[0] = 0; // Non-QoS data priority.
    for (unsigned i = 0; i < 6; ++i) {
        nonce[1 + i] = mac[10 + i]; // Transmitter address.
        nonce[7 + i] = pn >> ((5 - i) * 8);
    }
}

#endif
