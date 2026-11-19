#include "rsn_supplicant.h"

#include <string.h>

#include "bearssl.h"

enum { M1Info = 0x008a, M2Info = 0x010a, M3Info = 0x13ca, M4Info = 0x030a };

static const uint8_t ssid[] = "Cloudpilot WPA2";
static const uint8_t passphrase[] = "cloudpilot-lab-only";

bool rsnEqual(const uint8_t *a, const uint8_t *b, unsigned length) {
    unsigned difference = 0;
    for (unsigned i = 0; i < length; ++i) difference |= a[i] ^ b[i];
    return difference == 0;
}

// The lab uses short HMAC-SHA1 keys. Reuse the keyed prefix during PBKDF2.
typedef struct {
    br_sha1_context inner, outer;
} HmacKey;

static void hmacInit(HmacKey *hmac, const uint8_t *key, unsigned length) {
    uint8_t pad[64];
    memset(pad, 0x36, sizeof(pad));
    for (unsigned i = 0; i < length; ++i) pad[i] ^= key[i];
    br_sha1_init(&hmac->inner);
    br_sha1_update(&hmac->inner, pad, sizeof(pad));
    for (unsigned i = 0; i < sizeof(pad); ++i) pad[i] ^= 0x36 ^ 0x5c;
    br_sha1_init(&hmac->outer);
    br_sha1_update(&hmac->outer, pad, sizeof(pad));
}

static void hmacSha1(const HmacKey *hmac, const uint8_t *input, unsigned size, uint8_t digest[20]) {
    br_sha1_context state = hmac->inner;
    br_sha1_update(&state, input, size);
    br_sha1_out(&state, digest);
    state = hmac->outer;
    br_sha1_update(&state, digest, 20);
    br_sha1_out(&state, digest);
}

void rsnDeriveLabPmk(uint8_t pmk[32]) {
    rsnDerivePmk(passphrase, sizeof(passphrase) - 1, ssid, sizeof(ssid) - 1, pmk);
}

void rsnDerivePmk(const uint8_t *secret, unsigned secretSize,
                  const uint8_t *network, unsigned networkSize, uint8_t pmk[32]) {
    uint8_t salt[32 + 4] = {0};
    const unsigned saltSize = networkSize + 4;
    memcpy(salt, network, networkSize);
    HmacKey key;
    hmacInit(&key, secret, secretSize);
    for (unsigned block = 0; block < 2; ++block) {
        uint8_t u[20], accumulator[20];
        salt[saltSize - 1] = block + 1;
        hmacSha1(&key, salt, saltSize, u);
        memcpy(accumulator, u, sizeof(accumulator));
        for (unsigned i = 1; i < 4096; ++i) {
            hmacSha1(&key, u, sizeof(u), u);
            for (unsigned j = 0; j < sizeof(u); ++j) accumulator[j] ^= u[j];
        }
        memcpy(pmk + block * 20, accumulator, block ? 12 : 20);
    }
}

static void appendOrdered(uint8_t *out, const uint8_t *a, const uint8_t *b, unsigned size) {
    const bool aFirst = memcmp(a, b, size) < 0;
    memcpy(out, aFirst ? a : b, size);
    memcpy(out + size, aFirst ? b : a, size);
}

void rsnDerivePtk(const uint8_t pmk[32], const uint8_t sta[6], const uint8_t ap[6],
                      const uint8_t anonce[32], const uint8_t snonce[32], uint8_t ptk[48]) {
    const uint8_t label[] = "Pairwise key expansion";
    uint8_t input[sizeof(label) + 12 + 64 + 1];
    memcpy(input, label, sizeof(label));
    appendOrdered(input + sizeof(label), sta, ap, 6);
    appendOrdered(input + sizeof(label) + 12, anonce, snonce, 32);
    HmacKey key;
    hmacInit(&key, pmk, 32);
    for (unsigned i = 0; i < 3; ++i) {
        uint8_t digest[20];
        input[sizeof(input) - 1] = i;
        hmacSha1(&key, input, sizeof(input), digest);
        memcpy(ptk + 20 * i, digest, i == 2 ? 8 : sizeof(digest));
    }
}

void rsnStationNonce(const uint8_t seed[32], const uint8_t sta[6], const uint8_t ap[6],
                     const uint8_t *messageOne, uint8_t snonce[32]) {
    uint8_t context[53], tail[20];
    memcpy(context, sta, 6);
    memcpy(context + 6, ap, 6);
    memcpy(context + 12, messageOne + Nonce, 32);
    memcpy(context + 44, messageOne + Replay, 8);
    HmacKey key;
    hmacInit(&key, seed, 32);
    context[52] = 0;
    hmacSha1(&key, context, sizeof(context), snonce);
    context[52] = 1;
    hmacSha1(&key, context, sizeof(context), tail);
    memcpy(snonce + 20, tail, 12);
}

static void calculateMic(const uint8_t kck[16], const uint8_t *frame, unsigned size,
                         uint8_t digest[20]) {
    HmacKey key;
    hmacInit(&key, kck, 16);
    hmacSha1(&key, frame, size, digest);
}

unsigned rsnMakeReply(uint8_t *frame, unsigned info, const uint8_t replay[8],
                          const uint8_t *nonce, const uint8_t *data, unsigned dataLength,
                          const uint8_t kck[16]) {
    const unsigned size = KeyFrame + dataLength;
    memset(frame, 0, size);
    frame[0] = 2;
    frame[1] = 3;
    frame[4] = 2;
    rsnWriteBE(frame + 2, size - 4);
    rsnWriteBE(frame + KeyInfo, info);
    memcpy(frame + Replay, replay, 8);
    if (nonce) memcpy(frame + Nonce, nonce, 32);
    rsnWriteBE(frame + KeyDataLength, dataLength);
    if (dataLength) memcpy(frame + KeyData, data, dataLength);
    uint8_t digest[20];
    calculateMic(kck, frame, size, digest);
    memcpy(frame + Mic, digest, 16);
    return size;
}

// RFC 3394 unwrap for the EAPOL encrypted key data, using BearSSL's AES block primitive.
bool rsnUnwrap(const uint8_t kek[16], const uint8_t *wrapped, unsigned length, uint8_t *plain) {
    if (length < 24 || length % 8) return false;
    unsigned blocks = length / 8 - 1;
    uint8_t a[8];
    memcpy(a, wrapped, 8);
    memcpy(plain, wrapped + 8, length - 8);
    br_aes_ct_cbcdec_keys aes;
    br_aes_ct_cbcdec_init(&aes, kek, 16);
    for (unsigned j = 6; j > 0; --j) {
        for (unsigned i = blocks; i > 0; --i) {
            uint8_t input[16], iv[16] = {0};
            memcpy(input, a, 8);
            unsigned t = (j - 1) * blocks + i;
            input[6] ^= t >> 8;
            input[7] ^= t;
            memcpy(input + 8, plain + 8 * (i - 1), 8);
            br_aes_ct_cbcdec_run(&aes, iv, input, sizeof(input));
            memcpy(a, input, 8);
            memcpy(plain + 8 * (i - 1), input + 8, 8);
        }
    }
    const uint8_t expected[8] = {0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6};
    return rsnEqual(a, expected, sizeof(expected));
}

bool rsnReadGtkKde(const uint8_t *data, unsigned length, unsigned keyId, uint8_t gtk[CcmpKeySize]) {
    if (length < GtkKdeSize || memcmp(data, gtkKdePrefix, sizeof(gtkKdePrefix)) ||
        data[6] != keyId || data[7] != 0) return false;
    memcpy(gtk, data + GtkKdeHeaderSize, CcmpKeySize);
    return true;
}

enum ProbeResult rsnReadGroupMessage(uint8_t *frame, unsigned size, const uint8_t kckKek[32],
                                     const uint8_t previousReplay[8], unsigned nextKeyId,
                                     uint8_t gtk[CcmpKeySize]) {
    if (!rsnValidKeyFrame(frame, size, GroupM1Info) || rsnReadBE(frame + KeyLength) != 0 ||
        frame[KeyRsc + 6] || frame[KeyRsc + 7]) return ProbeRekeyError;
    if (memcmp(frame + Replay, previousReplay, 8) <= 0) return ProbeRekeyReplay;
    if (!rsnVerifyMic(frame, size, kckKek)) return ProbeRekeyMic;

    uint8_t kde[GtkKdeSize];
    if (rsnReadBE(frame + KeyDataLength) != GtkKdeSize + 8 ||
        !rsnUnwrap(kckKek + 16, frame + KeyData, GtkKdeSize + 8, kde) ||
        !rsnReadGtkKde(kde, sizeof(kde), nextKeyId, gtk)) return ProbeRekeyError;
    return ProbeComplete;
}

bool rsnVerifyMic(uint8_t *frame, unsigned size, const uint8_t kck[16]) {
    uint8_t received[16], digest[20];
    memcpy(received, frame + Mic, sizeof(received));
    memset(frame + Mic, 0, sizeof(received));
    calculateMic(kck, frame, size, digest);
    return rsnEqual(received, digest, sizeof(received));
}

bool rsnValidKeyFrame(const uint8_t *frame, unsigned size, unsigned info) {
    return size >= KeyFrame && frame[0] == 2 && frame[1] == 3 && frame[4] == 2 &&
           rsnReadBE(frame + 2) + 4 == size && rsnReadBE(frame + KeyInfo) == info &&
           rsnReadBE(frame + KeyDataLength) + KeyFrame == size;
}

enum ProbeResult rsnExchange(const RsnTransport *io, const uint8_t pmk[32], RsnKeys *keys) {
    uint8_t anonce[32], snonce[32], frame[EapolMax];
    unsigned size = io->receive(io->context, frame);
    if (size != KeyFrame || !rsnValidKeyFrame(frame, size, M1Info) || rsnReadBE(frame + KeyLength) != 16)
        return ProbeM1Error;
    memcpy(anonce, frame + Nonce, sizeof(anonce));
    memcpy(keys->replay, frame + Replay, sizeof(keys->replay));
    // Fixed only for comparison with the independent scripted AP transcript.
    for (unsigned i = 0; i < sizeof(snonce); ++i) snonce[i] = 0x40 + i;
    rsnDerivePtk(pmk, io->sta, io->ap, anonce, snonce, keys->ptk);
    size = rsnMakeReply(frame, M2Info, keys->replay, snonce, rsnPersonalCcmp, sizeof(rsnPersonalCcmp), keys->ptk);
    if (!io->send(io->context, frame, size)) return ProbeM2Error;
    size = io->receive(io->context, frame);
    if (!rsnValidKeyFrame(frame, size, M3Info) || rsnReadBE(frame + KeyLength) != 16 ||
        !rsnEqual(frame + Nonce, anonce, sizeof(anonce))) return ProbeM3Error;
    if (memcmp(frame + Replay, keys->replay, sizeof(keys->replay)) <= 0) return ProbeReplayError;
    if (!rsnVerifyMic(frame, size, keys->ptk)) return ProbeMicError;
    uint8_t plain[EapolMax - KeyFrame];
    const unsigned wrappedLen = rsnReadBE(frame + KeyDataLength);
    if (wrappedLen < sizeof(rsnPersonalCcmp) + GtkKdeSize + 8 ||
        !rsnUnwrap(keys->ptk + 16, frame + KeyData, wrappedLen, plain) ||
        memcmp(plain, rsnPersonalCcmp, sizeof(rsnPersonalCcmp)) ||
        !rsnReadGtkKde(plain + sizeof(rsnPersonalCcmp), wrappedLen - 8 - sizeof(rsnPersonalCcmp), CcmpGroupId, keys->gtk))
        return ProbeKeyError;
    if (frame[KeyRsc + 6] || frame[KeyRsc + 7]) return ProbeKeyError;
    keys->groupRsc = ccmpReadRsc(frame + KeyRsc);
    memcpy(keys->replay, frame + Replay, sizeof(keys->replay));
    size = rsnMakeReply(frame, M4Info, keys->replay, NULL, NULL, 0, keys->ptk);
    return io->send(io->context, frame, size) ? ProbeComplete : ProbeM4Error;
}
