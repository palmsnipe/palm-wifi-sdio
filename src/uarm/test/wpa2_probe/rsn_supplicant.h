#ifndef WPA2_RSN_SUPPLICANT_H
#define WPA2_RSN_SUPPLICANT_H

#include <stdbool.h>
#include <stdint.h>

#include "palmone_ccmp_wire.h"
#include "palmone_rsn_wire.h"
#include "probe_result.h"

typedef struct {
    const void *context;
    const uint8_t *sta, *ap;
    unsigned (*receive)(const void *context, uint8_t *frame);
    bool (*send)(const void *context, const uint8_t *frame, unsigned length);
} RsnTransport;

typedef struct {
    uint8_t ptk[48], gtk[CcmpKeySize], replay[8];
    uint64_t groupRsc;
} RsnKeys;

enum ProbeResult rsnExchange(const RsnTransport *io, const uint8_t pmk[32], RsnKeys *keys);
void rsnDeriveLabPmk(uint8_t pmk[32]);
void rsnDerivePmk(const uint8_t *passphrase, unsigned passphraseSize,
                  const uint8_t *ssid, unsigned ssidSize, uint8_t pmk[32]);
void rsnDerivePtk(const uint8_t pmk[32], const uint8_t sta[6], const uint8_t ap[6],
                  const uint8_t anonce[32], const uint8_t snonce[32], uint8_t ptk[48]);
void rsnStationNonce(const uint8_t seed[32], const uint8_t sta[6], const uint8_t ap[6],
                     const uint8_t *messageOne, uint8_t snonce[32]);
bool rsnEqual(const uint8_t *a, const uint8_t *b, unsigned length);
bool rsnValidKeyFrame(const uint8_t *frame, unsigned size, unsigned info);
bool rsnVerifyMic(uint8_t *frame, unsigned size, const uint8_t kck[16]);
bool rsnUnwrap(const uint8_t kek[16], const uint8_t *wrapped, unsigned length, uint8_t *plain);
bool rsnReadGtkKde(const uint8_t *data, unsigned length, unsigned keyId, uint8_t gtk[CcmpKeySize]);
enum ProbeResult rsnReadGroupMessage(uint8_t *frame, unsigned size, const uint8_t kckKek[32],
                                     const uint8_t previousReplay[8], unsigned nextKeyId,
                                     uint8_t gtk[CcmpKeySize]);
unsigned rsnMakeReply(uint8_t *frame, unsigned info, const uint8_t replay[8],
                      const uint8_t *nonce, const uint8_t *data, unsigned dataLength,
                      const uint8_t kck[16]);

#endif
