#include <string.h>

#include "ccmp_station.h"
#include "hardware.h"
#include "profile.h"
#include "../panel/credential.h"
#include "nonce_seed.h"
#include "rsn_supplicant.h"

int originalCommand(void *driver, uint16_t *command, unsigned timeout);
int stockReadRegister(void *driver, unsigned reg, uint16_t *value);
int stockWriteRegister(void *driver, unsigned reg, unsigned value);
int stockReadBuffer(void *driver, unsigned fid, unsigned offset, void *buffer, unsigned size);
int stockTransmit(void *driver, unsigned code, unsigned fid, unsigned timeout, uint16_t *status);
uint16_t stockGetFeature(uint32_t creator, uint16_t number, uint32_t *value);
uint16_t stockSetFeature(uint32_t creator, uint16_t number, uint32_t value);
int16_t stockGetSavedPreference(uint32_t creator, uint16_t id, void *prefs, uint16_t *size, bool saved);
void stockSetSavedPreference(uint32_t creator, uint16_t id, int16_t version, const void *prefs,
                             uint16_t size, bool saved);

enum {
    Initialize = 0, EnablePort = 1, DisablePort = 2, ReadRid = 0x21, WriteRid = 0x121,
    Allocate = 0x0a, Transmit = 0x10b,
    OwnMac = 0xfc01, PortType = 0xfc00, DesiredSsid = 0xfc02, WepFlags = 0xfc28,
    Authentication = 0xfc2a, Roaming = 0xfc2d, GenericElement = 0xfc48,
    SsnMode = 0xfcbb, JoinRequest = 0xfce2,
    NicId = 0xfd0b, PrimaryId = 0xfd02, StationId = 0xfd20,
    RxFid = 0x20, AllocFid = 0x22, EventStatus = 0x30, IrqMask = 0x32, EventAck = 0x34,
    RxEvent = 1, AllocEvent = 8, Select0 = 0x18, Offset0 = 0x1c, Data0 = 0x36,
    OffsetBusy = 0x8000, OffsetError = 0x4000, HeaderSize = 60,
    M1Info = 0x008a, M2Info = 0x010a, M3Info = 0x13ca, M4Info = 0x030a, TicksPerMs = 3250,
    RoamingDisabled = 3, PrivacyInvoked = 1, ExcludeUnencrypted = 2,
    HostEncrypt = 0x10, HostDecrypt = 0x80,
    Dot11Header = 14, MaxPayload = RsnSnapSize + EapolMax, MaxMsdu = 1510,
    MaxProtected = MaxMsdu + CcmpOverhead,
};

typedef struct {
    void *driver;
    uint8_t ap[6], sta[6];
    uint16_t txFid;
    uint8_t channel;
    char ssid[Wpa2SsidSize];
} TestLink;

enum { MaxScanEntries = 32 };
typedef struct {
    uint8_t ssid[32], bssid[6], channel, length;
    uint16_t signal;
} ScanEntry;

typedef struct {
    TestLink link;
    StationCcmpKey pairwise, groups[4];
    uint8_t kckKek[32], eapolReplay[8];
    uint16_t rxFid, rxSize, groupCount;
    uint8_t groupId;
    bool groupPending;
    uint8_t scanCount;
    ScanEntry scan[MaxScanEntries];
    uint8_t rx[MaxProtected];
    uint8_t tx[HeaderSize + MaxProtected];
} LinkStorage;

_Static_assert(sizeof(LinkStorage) + 7 <= HARDWARE_LINK_BYTES, "reserve room for eight-byte alignment");
LinkStorage *linkStorage(void);

static uint16_t word(const uint8_t *bytes) { return bytes[0] | (uint16_t)bytes[1] << 8; }

static void putWord(uint8_t *bytes, uint16_t value) {
    bytes[0] = value;
    bytes[1] = value >> 8;
}

static uint32_t now(void) {
    // The stock Tungsten E2 ROM maps PXA270's 3.25 MHz OS timer here.
    return *(volatile const uint32_t *)0x90a00010u;
}

static bool command(void *driver, uint16_t code, uint16_t argument) {
    uint16_t request[8] = {code, argument};
    return originalCommand(driver, request, 1000) == 0;
}

static bool readRid(void *driver, uint16_t rid, uint8_t *data, unsigned length) {
    uint8_t record[38] = {0};
    if (length > sizeof(record) - 4 || !command(driver, ReadRid, rid) ||
        stockReadBuffer(driver, rid, 0, record, length + 4) ||
        word(record) != length / 2 + 1 || word(record + 2) != rid) return false;
    memcpy(data, record + 4, length);
    return true;
}

static bool writeBuffer(void *driver, uint16_t fid, const uint8_t *data, unsigned bytes) {
    if (stockWriteRegister(driver, Select0, fid) || stockWriteRegister(driver, Offset0, 0)) return false;
    const uint32_t start = now();
    uint16_t offset;
    do {
        if (stockReadRegister(driver, Offset0, &offset)) return false;
        if (now() - start > 20u * TicksPerMs) return false;
    } while (offset & OffsetBusy);
    if (offset & OffsetError) return false;
    for (unsigned i = 0; i < bytes; i += 2)
        if (stockWriteRegister(driver, Data0, word(data + i))) return false;
    return true;
}

static bool writeRid(void *driver, uint16_t rid, const uint8_t *data, unsigned length) {
    uint8_t record[4 + 34];
    if (length > sizeof(record) - 4 || (length & 1)) return false;
    putWord(record, length / 2 + 1);
    putWord(record + 2, rid);
    memcpy(record + 4, data, length);
    return writeBuffer(driver, rid, record, length + 4) && command(driver, WriteRid, rid);
}

static bool writeWord(void *driver, uint16_t rid, uint16_t value) {
    uint8_t data[2];
    putWord(data, value);
    return writeRid(driver, rid, data, sizeof(data));
}

static uint16_t identity(void *driver) {
    static const struct { uint16_t rid, id, variant, major, minor; } expected[] = {
        {NicId, 0x801d, 0, 1, 0}, {PrimaryId, 0x0015, 2, 1, 1}, {StationId, 0x001f, 0, 1, 8},
    };
    for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        uint8_t data[8];
        if (!readRid(driver, expected[i].rid, data, sizeof(data)) ||
            word(data) != expected[i].id || word(data + 2) != expected[i].variant ||
            word(data + 4) != expected[i].major || word(data + 6) != expected[i].minor)
            return expected[i].rid;
    }
    return 0;
}

static uint16_t configure(void *driver) {
    static const struct { uint16_t rid, value; } settings[] = {
        {PortType, 1}, {Authentication, 1}, {Roaming, RoamingDisabled}, {SsnMode, 1},
    };
    for (unsigned i = 0; i < sizeof(settings) / sizeof(settings[0]); ++i)
        if (!writeWord(driver, settings[i].rid, settings[i].value)) return settings[i].rid;
    uint8_t privacy[2];
    if (!readRid(driver, WepFlags, privacy, sizeof(privacy)) ||
        !writeWord(driver, WepFlags,
                   (word(privacy) | PrivacyInvoked | HostEncrypt | HostDecrypt) & ~ExcludeUnencrypted))
        return WepFlags;
    uint8_t element[2 + sizeof(rsnPersonalCcmp)] = {sizeof(rsnPersonalCcmp), 0};
    memcpy(element + 2, rsnPersonalCcmp, sizeof(rsnPersonalCcmp));
    return writeRid(driver, GenericElement, element, sizeof(element)) ? 0 : GenericElement;
}

static bool join(const TestLink *link) {
    uint8_t data[8];
    memcpy(data, link->ap, 6);
    putWord(data + 6, link->channel);
    return writeRid(link->driver, JoinRequest, data, sizeof(data));
}

static bool waitEvent(void *driver, uint16_t event, uint32_t start, unsigned milliseconds) {
    while (now() - start < milliseconds * (uint32_t)TicksPerMs) {
        uint16_t status;
        if (stockReadRegister(driver, EventStatus, &status)) return false;
        if (status & event) return true;
        const uint32_t delay = now();
        while (now() - delay < TicksPerMs) {}
    }
    return false;
}

static bool allocate(TestLink *link) {
    void *driver = link->driver;
    return command(driver, Allocate, 512) && waitEvent(driver, AllocEvent, now(), 750) &&
           !stockReadRegister(driver, AllocFid, &link->txFid) &&
           !stockWriteRegister(driver, EventAck, AllocEvent);
}

static bool addressed(const TestLink *link, const uint8_t *descriptor, uint16_t control) {
    const uint8_t *mac = descriptor + Dot11Header;
    // The firmware can mark a protected packet as undecrypted when host decryption is enabled.
    return !(word(descriptor) & (control == CcmpFromDs ? 1 : 3)) &&
           (word(mac) & 0xc7ff) == control && !(mac[22] & 0x0f) &&
           !memcmp(mac + 4, link->sta, 6) && !memcmp(mac + 10, link->ap, 6);
}

static unsigned receiveFrame(const TestLink *link, uint16_t control, unsigned minLength,
                             uint8_t *descriptor, uint8_t *payload, unsigned capacity,
                             uint32_t start, unsigned milliseconds) {
    void *driver = link->driver;
    while (waitEvent(driver, RxEvent, start, milliseconds)) {
        uint16_t fid;
        if (stockReadRegister(driver, RxFid, &fid) ||
            stockReadBuffer(driver, fid, 0, descriptor, HeaderSize)) return 0;
        const unsigned size = word(descriptor + 44);
        const unsigned rounded = (size + 1) & ~1u;
        const bool candidate = addressed(link, descriptor, control) && size >= minLength && rounded <= capacity;
        if (candidate && stockReadBuffer(driver, fid, HeaderSize, payload, rounded)) return 0;
        if (stockWriteRegister(driver, EventAck, RxEvent)) return 0;
        if (candidate) return size;
    }
    return 0;
}

static unsigned receive(const TestLink *link, uint8_t *frame, unsigned info, unsigned milliseconds) {
    const uint32_t start = now();
    uint8_t descriptor[HeaderSize], payload[MaxPayload];
    unsigned bytes;
    while ((bytes = receiveFrame(link, 0x0208, RsnSnapSize + KeyFrame, descriptor, payload,
                                 sizeof(payload), start, milliseconds)) != 0) {
        if (memcmp(payload, rsnSnap, RsnSnapSize)) continue;
        const uint8_t *key = payload + RsnSnapSize;
        const unsigned length = rsnReadBE(key + 2) + 4;
        if ((key[0] != 1 && key[0] != 2 && key[0] != 3) || key[1] != 3 || key[4] != 2 ||
            length < KeyFrame || length > bytes - RsnSnapSize ||
            rsnReadBE(key + KeyInfo) != info || rsnReadBE(key + KeyDataLength) + KeyFrame != length ||
            rsnReadBE(key + KeyLength) != 16) continue;
        memcpy(frame, key, length);
        return length;
    }
    return 0;
}

static void makeTxDescriptor(const TestLink *link, uint8_t *buffer, uint16_t control, unsigned length) {
    putWord(buffer + 12, 8); // 802.11 transmit descriptor.
    putWord(buffer + Dot11Header, control);
    memcpy(buffer + 18, link->ap, 6);
    memcpy(buffer + 24, link->sta, 6);
    memcpy(buffer + 30, link->ap, 6);
    putWord(buffer + 44, length);
    rsnWriteBE(buffer + 58, length);
}

static bool sendFrame(const TestLink *link, const uint8_t *buffer, unsigned length) {
    void *driver = link->driver;
    uint16_t returned;
    // Wait for this FID to be reclaimed before modifying its contents for the next frame.
    return writeBuffer(driver, link->txFid, buffer, (length + 1) & ~1u) &&
           command(driver, Transmit, link->txFid) && waitEvent(driver, AllocEvent, now(), 750) &&
           !stockReadRegister(driver, AllocFid, &returned) &&
           !stockWriteRegister(driver, EventAck, AllocEvent) && returned == link->txFid;
}

static bool sendEapol(const TestLink *link, const uint8_t *frame, unsigned length) {
    uint8_t buffer[HeaderSize + RsnSnapSize + KeyFrame + sizeof(rsnPersonalCcmp) + 1] = {0};
    makeTxDescriptor(link, buffer, 0x0108, RsnSnapSize + length);
    memcpy(buffer + HeaderSize, rsnSnap, RsnSnapSize);
    memcpy(buffer + HeaderSize + RsnSnapSize, frame, length);
    return sendFrame(link, buffer, HeaderSize + RsnSnapSize + length);
}

static void wipe(void *data, unsigned size) {
    volatile uint8_t *bytes = data;
    for (unsigned i = 0; i < size; ++i) bytes[i] = 0;
}

static unsigned compatibleRsn(const uint8_t *data, unsigned available) {
    if (available < sizeof(rsnPersonalCcmp) - 2 || data[0] != 0x30 ||
        (unsigned)data[1] + 2 > available || word(data + 2) != 1 ||
        memcmp(data + 4, rsnPersonalCcmp + 4, 4)) return 0;
    const unsigned length = data[1] + 2;
    unsigned offset = 8;
    for (unsigned suite = 0; suite < 2; ++suite) {
        if (offset + 2 > length) return 0;
        unsigned count = word(data + offset);
        offset += 2;
        if (count > (length - offset) / 4) return 0;
        bool found = false;
        while (count--) {
            if (!memcmp(data + offset, rsnPersonalCcmp + 10 + 6 * suite, 4)) found = true;
            offset += 4;
        }
        if (!found) return 0;
    }
    if (offset < length && (offset + 2 > length || (data[offset] & 0x40))) return 0;
    return length;
}

static unsigned verifyKeyData(const uint8_t *frame, const uint8_t kek[16], uint8_t gtk[CcmpKeySize]) {
    uint8_t plain[EapolMax - KeyFrame];
    const unsigned wrapped = rsnReadBE(frame + KeyDataLength);
    unsigned groupId = 0;
    if (wrapped >= sizeof(rsnPersonalCcmp) + GtkKdeSize + 6 &&
        rsnUnwrap(kek, frame + KeyData, wrapped, plain)) {
        const unsigned rsne = compatibleRsn(plain, wrapped - 8);
        if (!rsne) {
            wipe(plain, sizeof(plain));
            return 0;
        }
        for (unsigned offset = rsne; offset + 2 <= wrapped - 8;) {
            const uint8_t *item = plain + offset;
            const unsigned length = 2 + item[1];
            if (length > wrapped - 8 - offset) break;
            if (length == GtkKdeSize && !memcmp(item, gtkKdePrefix, sizeof(gtkKdePrefix))) {
                const unsigned id = item[6] & 3;
                if (id && rsnReadGtkKde(item, length, id, gtk)) groupId = id;
                break;
            }
            offset += length;
        }
    }
    wipe(plain, sizeof(plain));
    return groupId;
}

static unsigned finish(LinkStorage *storage, const uint8_t *m1, uint8_t *frame, const uint8_t ptk[48]) {
    const TestLink *link = &storage->link;
    const unsigned length = receive(link, frame, M3Info, 3500);
    if (!length || !rsnEqual(frame + Nonce, m1 + Nonce, 32) ||
        memcmp(frame + Replay, m1 + Replay, 8) <= 0) return TestM3;
    if (!rsnVerifyMic(frame, length, ptk)) return TestM3Mic;
    uint8_t gtk[CcmpKeySize];
    const unsigned groupId = verifyKeyData(frame, ptk + 16, gtk);
    const uint64_t initialGroupPn = ccmpReadRsc(frame + KeyRsc);
    if (!groupId || frame[KeyRsc + 6] || frame[KeyRsc + 7]) {
        wipe(gtk, sizeof(gtk));
        return TestM3Data;
    }
    uint8_t replay[8];
    memcpy(replay, frame + Replay, sizeof(replay));
    const unsigned reply = rsnMakeReply(frame, M4Info, replay, NULL, NULL, 0, ptk);
    const bool sent = sendEapol(link, frame, reply);
    if (sent) {
        ccmpInstall(&storage->pairwise, ptk + 32, 0);
        ccmpInstall(&storage->groups[groupId], gtk, initialGroupPn);
        memcpy(storage->kckKek, ptk, sizeof(storage->kckKek));
        memcpy(storage->eapolReplay, replay, sizeof(storage->eapolReplay));
        storage->groupId = groupId;
        const uint32_t start = now();
        while (now() - start < 180u * TicksPerMs) {}
    }
    wipe(gtk, sizeof(gtk));
    return sent ? TestJoined : TestM4;
}

static unsigned exchange(LinkStorage *storage, const uint8_t pmk[32]) {
    const TestLink *link = &storage->link;
    uint8_t m1[EapolMax], frame[EapolMax], snonce[32], ptk[48];
    const unsigned m1Length = receive(link, m1, M1Info, 5000);
    if (!m1Length) return TestM1;
    rsnStationNonce(nonceSeed, link->sta, link->ap, m1, snonce);
    rsnDerivePtk(pmk, link->sta, link->ap, m1 + Nonce, snonce, ptk);
    const unsigned m2Length = rsnMakeReply(frame, M2Info, m1 + Replay, snonce,
        rsnPersonalCcmp, sizeof(rsnPersonalCcmp), ptk);
    const unsigned result = sendEapol(link, frame, m2Length) ? finish(storage, m1, frame, ptk) : TestM2;
    wipe(ptk, sizeof(ptk));
    return result;
}

static void report(unsigned state, uint16_t detail) {
    stockSetFeature(HARDWARE_CREATOR, TestState, (uint32_t)state << 16 | detail);
}

static void reportGroup(const LinkStorage *storage) {
    stockSetFeature(HARDWARE_CREATOR, TestGroup, (uint32_t)storage->groupCount << 16 | storage->groupId);
}

static void clearLink(LinkStorage *storage) {
    ccmpClear(&storage->pairwise);
    for (unsigned id = 1; id < 4; ++id) ccmpClear(&storage->groups[id]);
    wipe(storage->kckKek, sizeof(storage->kckKek));
    wipe(storage->eapolReplay, sizeof(storage->eapolReplay));
    storage->groupCount = storage->groupId = 0;
    storage->groupPending = false;
    storage->rxSize = 0;
    reportGroup(storage);
    stockSetFeature(HARDWARE_CREATOR, TestCredentialSource, 0);
}

static Wpa2Credential *sessionCredential(void) {
    uint32_t pointer;
    if (stockGetFeature(HARDWARE_CREATOR, Wpa2CredentialFeature, &pointer)) return NULL;
    return (Wpa2Credential *)(uintptr_t)pointer;
}

static bool readProfile(const char *ssid, Wpa2Profile *stored) {
    uint16_t size = sizeof(*stored);
    return stockGetSavedPreference(HARDWARE_CREATOR, wpa2ProfileId(ssid), stored, &size, true) ==
               Wpa2ProfileVersion &&
           size == sizeof(*stored) && !memcmp(stored->ssid, ssid, Wpa2SsidSize);
}

static bool loadPmk(Wpa2Credential *credential, const Wpa2Profile *stored,
                    unsigned size, uint8_t pmk[Wpa2PmkSize]) {
    if (!credential || !credential->kind) {
        if (!stored->hasKey) return false;
        memcpy(pmk, stored->pmk, Wpa2PmkSize);
        return true;
    }
    if (credential->kind == Wpa2CandidatePmk || credential->kind == Wpa2CachedPmk) {
        memcpy(pmk, credential->bytes, Wpa2PmkSize);
        return true;
    }
    if (credential->kind != Wpa2Passphrase ||
        !wpa2ValidPassphrase(credential->bytes, credential->length)) return false;
    rsnDerivePmk(credential->bytes, credential->length, (const uint8_t *)stored->ssid, size, pmk);
    wipe(credential->bytes, sizeof(credential->bytes));
    memcpy(credential->bytes, pmk, Wpa2PmkSize);
    credential->kind = Wpa2CandidatePmk;
    return true;
}

static unsigned saveAcceptedPmk(Wpa2Credential *credential, Wpa2Profile *stored, const TestLink *link,
                                const uint8_t pmk[Wpa2PmkSize]) {
    const unsigned source = credential && credential->kind ? TestEnteredAndSaved : TestUsedSaved;
    if (stored->hasKey && stored->channel == link->channel && !memcmp(stored->bssid, link->ap, 6) &&
        rsnEqual(stored->pmk, pmk, Wpa2PmkSize))
        return source;

    memcpy(stored->bssid, link->ap, 6);
    stored->channel = link->channel;
    stored->hasKey = 1;
    memcpy(stored->pmk, pmk, Wpa2PmkSize);
    stockSetSavedPreference(HARDWARE_CREATOR, wpa2ProfileId(stored->ssid), Wpa2ProfileVersion,
                            stored, sizeof(*stored), true);
    Wpa2Profile verified;
    const bool saved = readProfile(stored->ssid, &verified) && rsnEqual(verified.pmk, pmk, Wpa2PmkSize) &&
                       verified.hasKey && verified.channel == link->channel &&
                       !memcmp(verified.bssid, link->ap, 6);
    wipe(&verified, sizeof(verified));
    if (saved && source == TestEnteredAndSaved) credential->kind = Wpa2CachedPmk;
    return saved ? source : TestSaveFailed;
}

static bool selectAccessPoint(LinkStorage *storage, const Wpa2Profile *stored, unsigned length) {
    const ScanEntry *best = NULL;
    for (unsigned i = 0; i < storage->scanCount; ++i) {
        const ScanEntry *entry = &storage->scan[i];
        if (entry->length == length && !memcmp(entry->ssid, stored->ssid, length) &&
            (!best || entry->signal > best->signal)) best = entry;
    }
    storage->link.channel = best ? best->channel : stored->channel;
    memcpy(storage->link.ap, best ? best->bssid : stored->bssid, 6);
    return storage->link.channel && storage->link.channel <= 14;
}

static int enable(LinkStorage *storage, void *driver, uint16_t *request, unsigned timeout,
                  Wpa2Profile *stored, unsigned length) {
    clearLink(storage);
    storage->link.driver = driver;
    TestLink *link = &storage->link;
    memcpy(link->ssid, stored->ssid, Wpa2SsidSize);
    if (!selectAccessPoint(storage, stored, length)) {
        report(TestNoAccessPoint, 0);
        return 1;
    }
    stockSetFeature(HARDWARE_CREATOR, TestBssidLow, word(link->ap) | (uint32_t)word(link->ap + 2) << 16);
    stockSetFeature(HARDWARE_CREATOR, TestAccessPoint, word(link->ap + 4) | (uint32_t)link->channel << 16);
    const uint16_t mismatch = identity(driver);
    if (mismatch || !readRid(driver, OwnMac, link->sta, sizeof(link->sta))) {
        report(TestIdentity, mismatch ? mismatch : OwnMac);
        return 1;
    }

    Wpa2Credential *credential = sessionCredential();
    if (credential && memcmp(credential->ssid, stored->ssid, Wpa2SsidSize)) credential = NULL;
    uint8_t pmk[Wpa2PmkSize];
    if (!loadPmk(credential, stored, length, pmk)) {
        report(TestNeedPassphrase, 0);
        return 1;
    }

    uint16_t irq;
    if (stockReadRegister(driver, IrqMask, &irq)) {
        wipe(pmk, sizeof(pmk));
        report(TestInterrupts, 0);
        return 1;
    }
    if (stockWriteRegister(driver, IrqMask, 0)) {
        wipe(pmk, sizeof(pmk));
        if (stockWriteRegister(driver, IrqMask, irq))
            stockSetFeature(HARDWARE_CREATOR, TestCleanup, 2);
        report(TestInterrupts, 0);
        return 1;
    }
    unsigned result = TestConfig;
    const uint16_t failedRid = configure(driver);
    if (!failedRid) {
        result = TestEnable;
        if (!originalCommand(driver, request, timeout)) {
            result = TestAllocate;
            if (allocate(link)) {
                result = TestJoin;
                if (join(link)) result = exchange(storage, pmk);
            }
        }
    }

    unsigned cleanup = 0;
    if (result != TestJoined && !command(driver, DisablePort, 0)) cleanup = 1;
    if (stockWriteRegister(driver, IrqMask, irq)) cleanup |= 2;
    if (cleanup) {
        clearLink(storage);
        if (result == TestJoined) {
            result = TestInterrupts;
            command(driver, DisablePort, 0);
        }
    }
    stockSetFeature(HARDWARE_CREATOR, TestCleanup, cleanup);
    if (result == TestJoined)
        stockSetFeature(HARDWARE_CREATOR, TestCredentialSource, saveAcceptedPmk(credential, stored, link, pmk));
    wipe(pmk, sizeof(pmk));
    reportGroup(storage);
    report(result, failedRid);
    return result == TestJoined ? 0 : 1;
}

static bool protectTx(LinkStorage *storage, void *driver, unsigned fid) {
    uint8_t *const tx = storage->tx;
    uint8_t *const mac = tx + Dot11Header;
    uint8_t *const body = tx + HeaderSize;
    if (stockReadBuffer(driver, fid, 0, tx, HeaderSize)) return false;
    const unsigned length = word(tx + 44);
    if ((word(mac) & 0xc7ff) != 0x0108 || length < RsnSnapSize || length > MaxMsdu ||
        memcmp(mac + 10, storage->link.sta, 6) ||
        stockReadBuffer(driver, fid, HeaderSize, body + CcmpHeaderSize, (length + 1) & ~1u))
        return false;
    // Stock TX leaves Address 1 for the firmware. CCMP authenticates it before submission.
    memcpy(mac + 4, storage->link.ap, 6);
    putWord(mac, CcmpToDs);
    putWord(tx + 44, length + CcmpOverhead);
    rsnWriteBE(tx + 58, length + CcmpOverhead);
    return ccmpSeal(&storage->pairwise, mac, body, length) &&
           writeBuffer(driver, fid, tx, (HeaderSize + length + CcmpOverhead + 1) & ~1u);
}

static bool sendGroupReply(LinkStorage *storage, const uint8_t *replay) {
    const TestLink *const link = &storage->link;
    void *const driver = link->driver;
    enum { Length = RsnSnapSize + KeyFrame, ProtectedLength = Length + CcmpOverhead };
    uint8_t buffer[HeaderSize + ProtectedLength + 1] = {0};
    uint8_t *const body = buffer + HeaderSize;
    uint8_t *const plain = body + CcmpHeaderSize;
    makeTxDescriptor(link, buffer, CcmpToDs, ProtectedLength);
    memcpy(plain, rsnSnap, RsnSnapSize);
    rsnMakeReply(plain + RsnSnapSize, GroupM2Info, replay, NULL, NULL, 0, storage->kckKek);

    uint16_t irq, events;
    if (stockReadRegister(driver, IrqMask, &irq)) return false;
    const bool sent = !stockWriteRegister(driver, IrqMask, 0) &&
        !stockReadRegister(driver, EventStatus, &events) && !(events & AllocEvent) &&
        ccmpSeal(&storage->pairwise, buffer + Dot11Header, body, Length) &&
        sendFrame(link, buffer, HeaderSize + ProtectedLength);
    const int restore = stockWriteRegister(driver, IrqMask, irq);
    return sent && !restore;
}

static void groupRekey(LinkStorage *storage, uint8_t *frame, unsigned size) {
    uint8_t kde[GtkKdeSize];
    if (!rsnValidKeyFrame(frame, size, GroupM1Info) || rsnReadBE(frame + KeyLength) != 0 ||
        rsnReadBE(frame + KeyDataLength) != sizeof(kde) + 8 || frame[KeyRsc + 6] || frame[KeyRsc + 7])
        return;
    const int replayOrder = memcmp(frame + Replay, storage->eapolReplay, sizeof(storage->eapolReplay));
    if (replayOrder < 0 || !rsnVerifyMic(frame, size, storage->kckKek) ||
        !rsnUnwrap(storage->kckKek + 16, frame + KeyData, sizeof(kde) + 8, kde)) {
        wipe(kde, sizeof(kde));
        return;
    }

    const unsigned id = kde[6] & 3;
    uint8_t gtk[CcmpKeySize];
    const bool valid = id && rsnReadGtkKde(kde, sizeof(kde), id, gtk);
    wipe(kde, sizeof(kde));
    if (!valid) return;

    StationCcmpKey *const current = &storage->groups[storage->groupId];
    StationCcmpKey *const next = &storage->groups[id];
    if (id == storage->groupId) {
        // Retransmissions need a fresh pairwise PN, but keep the installed GTK's RX PN.
        if (rsnEqual(gtk, current->key, sizeof(gtk))) {
            memcpy(storage->eapolReplay, frame + Replay, sizeof(storage->eapolReplay));
            sendGroupReply(storage, frame + Replay);
        }
    } else if (replayOrder > 0 && !rsnEqual(gtk, current->key, sizeof(gtk)) &&
               (!next->installed || !rsnEqual(gtk, next->key, sizeof(gtk))) &&
               sendGroupReply(storage, frame + Replay)) {
        ccmpInstall(next, gtk, ccmpReadRsc(frame + KeyRsc));
        storage->groupId = id;
        storage->groupPending = true;
        memcpy(storage->eapolReplay, frame + Replay, sizeof(storage->eapolReplay));
        reportGroup(storage);
    }
    wipe(gtk, sizeof(gtk));
}

int linkCommand(void *driver, uint16_t *request, unsigned timeout) {
    if (!request) return originalCommand(driver, request, timeout);
    const uint16_t commandCode = request[0];
    if (commandCode == Initialize || commandCode == DisablePort) {
        LinkStorage *storage = linkStorage();
        if (storage->link.driver == driver && storage->pairwise.installed) {
            clearLink(storage);
            report(TestIdle, 0);
        }
    } else if (commandCode == EnablePort) {
        uint8_t desired[34];
        if (readRid(driver, DesiredSsid, desired, sizeof(desired))) {
            const unsigned length = word(desired);
            if (length && length <= 32) {
                Wpa2Profile stored;
                char ssid[Wpa2SsidSize] = {0};
                memcpy(ssid, desired + 2, length);
                if (readProfile(ssid, &stored)) {
                    uint32_t state;
                    if (stockGetFeature(HARDWARE_CREATOR, TestState, &state)) state = 0;
                    LinkStorage *const storage = linkStorage();
                    int result = 1;
                    if (state == (uint32_t)TestJoined << 16 && storage->link.driver == driver &&
                        storage->pairwise.installed && !memcmp(storage->link.ssid, ssid, Wpa2SsidSize)) {
                        result = 0;
                    } else if (state != (uint32_t)TestBusy << 16) {
                        report(TestBusy, 0);
                        result = enable(storage, driver, request, timeout, &stored, length);
                    }
                    wipe(&stored, sizeof(stored));
                    return result;
                }
                wipe(&stored, sizeof(stored));
            }
        }
    }
    return originalCommand(driver, request, timeout);
}

int linkReadInfo(void *driver, unsigned fid, unsigned offset, uint8_t *data, unsigned bytes) {
    const int status = stockReadBuffer(driver, fid, offset, data, bytes);
    if (status || bytes < 6) return status;
    const unsigned info = word(data);
    if (info != 0xf101 && info != 0xf103) return status;
    const unsigned stride = info == 0xf103 ? word(data + 2) : 62;
    if (stride < 62) return status;

    LinkStorage *const storage = linkStorage();
    storage->scanCount = 0;
    for (unsigned pos = 6; pos + stride <= bytes && storage->scanCount < MaxScanEntries; pos += stride) {
        const uint8_t *entry = data + pos;
        const unsigned channel = word(entry), length = word(entry + 16);
        if (!channel || channel > 14 || !length || length > 32 ||
            (word(entry + 14) & 0x11) != 0x11 || (entry[6] & 1)) continue;
        ScanEntry *out = &storage->scan[storage->scanCount++];
        memcpy(out->ssid, entry + 18, length);
        memcpy(out->bssid, entry + 6, 6);
        out->channel = channel;
        out->length = length;
        out->signal = word(entry + 4);
    }
    return status;
}

int linkTransmit(void *driver, unsigned code, unsigned fid, unsigned timeout, uint16_t *status) {
    LinkStorage *storage = linkStorage();
    if (storage->link.driver != driver || !storage->pairwise.installed)
        return stockTransmit(driver, code, fid, timeout, status);
    uint16_t irq;
    if (stockReadRegister(driver, IrqMask, &irq)) return 1;
    int result = 1;
    if (!stockWriteRegister(driver, IrqMask, 0) && protectTx(storage, driver, fid))
        result = stockTransmit(driver, code, fid, timeout, status);
    const int restore = stockWriteRegister(driver, IrqMask, irq);
    return result ? result : restore;
}

int linkReadHeader(void *driver, unsigned fid, unsigned offset, uint8_t *header, unsigned bytes) {
    const int result = stockReadBuffer(driver, fid, offset, header, bytes);
    LinkStorage *storage = linkStorage();
    if (result || storage->link.driver != driver || !storage->pairwise.installed) return result;
    storage->rxSize = 0;
    uint8_t *const mac = header + Dot11Header;
    const unsigned size = word(header + 44);
    const bool group = mac[4] & 1;
    if ((word(header) & 1) || (word(mac) & 0xc7ff) != CcmpFromDs || (mac[22] & 15) ||
        memcmp(mac + 10, storage->link.ap, 6) || (!group && memcmp(mac + 4, storage->link.sta, 6)) ||
        size < RsnSnapSize + CcmpOverhead || size > MaxProtected ||
        stockReadBuffer(driver, fid, HeaderSize, storage->rx, (size + 1) & ~1u)) return -1;

    const unsigned keyId = ccmpReadKeyId(storage->rx);
    if ((group ? !keyId : keyId != CcmpPairwiseId) ||
        ccmpOpen(group ? &storage->groups[keyId] : &storage->pairwise,
                 mac, storage->rx, size) != CcmpAccepted)
        return -1;
    uint8_t *const plain = storage->rx + CcmpHeaderSize;
    if (memcmp(plain, rsnSnap, 6)) return -1;
    if (!memcmp(plain, rsnSnap, RsnSnapSize)) {
        if (!group && !memcmp(mac + 16, storage->link.ap, 6))
            groupRekey(storage, plain + RsnSnapSize, size - CcmpOverhead - RsnSnapSize);
        return -1;
    }
    if (group && keyId == storage->groupId && storage->groupPending) {
        storage->groupPending = false;
        for (unsigned id = 1; id < 4; ++id)
            if (id != keyId) ccmpClear(&storage->groups[id]);
        if (storage->groupCount < UINT16_MAX) ++storage->groupCount;
        reportGroup(storage);
    }

    storage->rxSize = size - CcmpOverhead;
    storage->rxFid = fid;
    putWord(header, 0x2000);  // The stock receiver expects a plaintext RFC 1042 packet.
    putWord(mac, 0x0208);
    putWord(header + 44, storage->rxSize);
    memcpy(header + 46, mac + 4, 6);
    memcpy(header + 52, mac + 16, 6);
    rsnWriteBE(header + 58, storage->rxSize);
    return 0;
}

int linkReadPayload(void *driver, unsigned fid, unsigned offset, uint8_t *payload, unsigned bytes) {
    LinkStorage *storage = linkStorage();
    if (storage->link.driver != driver || !storage->pairwise.installed)
        return stockReadBuffer(driver, fid, offset, payload, bytes);
    if (!storage->rxSize || storage->rxFid != fid || storage->rxSize != bytes) return -1;
    memcpy(payload, storage->rx + CcmpHeaderSize, bytes);
    storage->rxSize = 0;
    return 0;
}
