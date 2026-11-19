#ifndef WPA2_CCMP_STATION_H
#define WPA2_CCMP_STATION_H

#include <stdbool.h>

#include "palmone_ccmp_wire.h"

typedef struct {
    uint8_t key[CcmpKeySize];
    uint64_t txPn, rxPn;
    bool installed;
} StationCcmpKey;

enum CcmpOpenResult {
    CcmpAccepted = 1, CcmpReplay, CcmpBadTag, CcmpInvalid,
};

void ccmpInstall(StationCcmpKey *key, const uint8_t *material, uint64_t rxPn);
void ccmpClear(StationCcmpKey *key);
bool ccmpSeal(StationCcmpKey *key, const uint8_t *mac, uint8_t *payload, unsigned plaintextSize);
enum CcmpOpenResult ccmpOpen(StationCcmpKey *key, const uint8_t *mac, uint8_t *payload, unsigned size);

#endif
