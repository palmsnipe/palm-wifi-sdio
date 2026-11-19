#ifndef PALMONE_WPA2_PROFILE_H
#define PALMONE_WPA2_PROFILE_H

enum { Wpa2SsidSize = 33, Wpa2ProfileVersion = 2 };

// Shared by the 68K panel and the ARM driver. All fields are byte arrays or bytes.
typedef struct {
    char ssid[Wpa2SsidSize];
    unsigned char bssid[6], channel, hasKey;
    unsigned char pmk[32];
} Wpa2Profile;

static inline unsigned short wpa2ProfileId(const char *ssid) {
    unsigned short hash = 0;
    while (*ssid) hash = hash * 31u + (unsigned char)*ssid++;
    return hash ? hash : 1;
}

#endif
