#ifndef PALMONE_WPA2_CREDENTIAL_H
#define PALMONE_WPA2_CREDENTIAL_H

enum {
    Wpa2CredentialFeature = WPA2_CREDENTIAL_FEATURE,
    Wpa2Passphrase = 1, Wpa2CandidatePmk = 2, Wpa2CachedPmk = 3,
    Wpa2PassphraseMin = 8, Wpa2PassphraseMax = 63, Wpa2PmkSize = 32,
    Wpa2SavedPreference = 0, Wpa2SavedVersion = 1,
};

// The 68K wizard and native ARM driver share this volatile Palm OS heap block.
// Byte fields keep the layout independent of the two CPUs' byte order.
typedef struct {
    unsigned char kind;
    unsigned char length;
    unsigned char bytes[Wpa2PassphraseMax];
    char ssid[Wpa2SsidSize];
} Wpa2Credential;

static inline int wpa2ValidPassphrase(const unsigned char *text, unsigned length) {
    if (!text || length < Wpa2PassphraseMin || length > Wpa2PassphraseMax) return 0;
    for (unsigned i = 0; i < length; ++i)
        if (text[i] < 0x20 || text[i] > 0x7e) return 0;
    return 1;
}

#endif
