#include "ccmp_station.h"

#include <string.h>

#include "bearssl.h"

typedef struct {
    const br_block_ctrcbc_class *vtable;
    const br_aes_ct_ctrcbc_keys *aes;
} CcmpCipher;

static void aesEncrypt(const br_block_ctrcbc_class *const *ctx, void *ctr, void *mac,
                       void *data, size_t length) {
    br_aes_ct_ctrcbc_encrypt(((const CcmpCipher *)ctx)->aes, ctr, mac, data, length);
}

static void aesDecrypt(const br_block_ctrcbc_class *const *ctx, void *ctr, void *mac,
                       void *data, size_t length) {
    br_aes_ct_ctrcbc_decrypt(((const CcmpCipher *)ctx)->aes, ctr, mac, data, length);
}

static void aesCtr(const br_block_ctrcbc_class *const *ctx, void *ctr, void *data, size_t length) {
    br_aes_ct_ctrcbc_ctr(((const CcmpCipher *)ctx)->aes, ctr, data, length);
}

static void aesMac(const br_block_ctrcbc_class *const *ctx, void *mac, const void *data,
                   size_t length) {
    br_aes_ct_ctrcbc_mac(((const CcmpCipher *)ctx)->aes, mac, data, length);
}

static bool ccmCrypt(const StationCcmpKey *key, const uint8_t *mac, uint64_t pn,
                     uint8_t *data, unsigned length, bool encrypt) {
    // Use relocated callbacks with the AES schedule prepared at key installation.
    br_block_ctrcbc_class aesMethods = {sizeof(CcmpCipher), 16, 4, NULL, NULL, NULL,
                                       NULL, NULL};
    volatile br_block_ctrcbc_class *dispatch = &aesMethods;
    dispatch->encrypt = aesEncrypt;
    dispatch->decrypt = aesDecrypt;
    dispatch->ctr = aesCtr;
    dispatch->mac = aesMac;
    CcmpCipher cipher = {&aesMethods, &key->aes};

    uint8_t aad[CcmpAadSize], nonce[CcmpNonceSize];
    ccmpMakeAadNonce(mac, pn, aad, nonce);
    br_ccm_context ccm;
    br_ccm_init(&ccm, &cipher.vtable);
    if (!br_ccm_reset(&ccm, nonce, sizeof(nonce), sizeof(aad), length, CcmpTagSize)) return false;
    br_ccm_aad_inject(&ccm, aad, sizeof(aad));
    br_ccm_flip(&ccm);
    br_ccm_run(&ccm, encrypt, data, length);
    if (encrypt) return br_ccm_get_tag(&ccm, data + length) == CcmpTagSize;
    return br_ccm_check_tag(&ccm, data + length) == 1;
}

void ccmpInstall(StationCcmpKey *key, const uint8_t *material, uint64_t rxPn) {
    memcpy(key->key, material, CcmpKeySize);
    br_aes_ct_ctrcbc_init(&key->aes, material, CcmpKeySize);
    key->txPn = 0;
    key->rxPn = rxPn;
    key->installed = true;
}

void ccmpClear(StationCcmpKey *key) {
    volatile uint8_t *bytes = (volatile uint8_t *)key;
    for (unsigned i = 0; i < sizeof(*key); ++i) bytes[i] = 0;
}

bool ccmpSeal(StationCcmpKey *key, const uint8_t *mac, uint8_t *payload, unsigned plaintextSize) {
    if (!key->installed || key->txPn == CCMP_MAX_PN) return false;
    const uint64_t pn = ++key->txPn;
    ccmpWriteHeader(payload, pn, CcmpPairwiseId);
    return ccmCrypt(key, mac, pn, payload + CcmpHeaderSize, plaintextSize, true);
}

enum CcmpOpenResult ccmpOpen(StationCcmpKey *key, const uint8_t *mac, uint8_t *payload, unsigned size) {
    if (!key->installed || size <= CcmpOverhead || !ccmpValidHeader(payload)) return CcmpInvalid;
    const uint64_t pn = ccmpReadPn(payload);
    if (pn <= key->rxPn) return CcmpReplay;
    const unsigned length = size - CcmpOverhead;
    if (!ccmCrypt(key, mac, pn, payload + CcmpHeaderSize, length, false)) {
        // BearSSL decrypts in place before checking the tag. Discard this plaintext.
        memset(payload + CcmpHeaderSize, 0, length);
        return CcmpBadTag;
    }
    key->rxPn = pn;
    return CcmpAccepted;
}
