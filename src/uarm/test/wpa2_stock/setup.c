#include <PalmOS.h>

#ifdef WPA2_CLEAR_WEP_PROFILE
#include "../wpa2_hardware/profile.h"
#endif
#include "credential.h"

enum { PassphraseForm = 6500, PassphraseLabel, PassphraseField, PassphraseConnect, PassphraseCancel };
enum { SavedKeyForm = 6510, SavedKeyLabel, SavedKeyConnect, SavedKeyChange, SavedKeyCancel, SavedKeyForget };

#ifdef WPA2_CLEAR_WEP_PROFILE
static Boolean readProfile(const Char *ssid, Wpa2Profile *profile) {
    UInt16 size = sizeof(*profile);
    return PrefGetAppPreferences(WPA2_FEATURE_CREATOR, wpa2ProfileId(ssid), profile, &size, true) ==
               Wpa2ProfileVersion &&
           size == sizeof(*profile) && !StrNCompare(profile->ssid, ssid, Wpa2SsidSize);
}

static Boolean writeProfile(Wpa2Profile *profile) {
    PrefSetAppPreferences(WPA2_FEATURE_CREATOR, wpa2ProfileId(profile->ssid), Wpa2ProfileVersion,
                          profile, sizeof(*profile), true);
    Wpa2Profile verified;
    const Boolean saved = readProfile(profile->ssid, &verified) && !MemCmp(&verified, profile, sizeof(verified));
    MemSet(&verified, sizeof(verified), 0);
    return saved;
}

static Boolean createProfile(const Char *ssid, Wpa2Profile *profile) {
    UInt16 size = 0;
    if (PrefGetAppPreferences(WPA2_FEATURE_CREATOR, wpa2ProfileId(ssid), NULL, &size, true) !=
        noPreferenceFound) return false;
    MemSet(profile, sizeof(*profile), 0);
    StrCopy(profile->ssid, ssid);

    // Preserve a v0.6 credential when its network is selected in the new panel.
    struct { Char ssid[12]; UInt8 pmk[Wpa2PmkSize]; } legacy;
    size = sizeof(legacy);
    if (StrLen(ssid) == sizeof(legacy.ssid) - 1 &&
        PrefGetAppPreferences(WPA2_FEATURE_CREATOR, Wpa2SavedPreference, &legacy, &size, true) ==
            Wpa2SavedVersion && size == sizeof(legacy) && !MemCmp(legacy.ssid, ssid, sizeof(legacy.ssid))) {
        MemMove(profile->pmk, legacy.pmk, sizeof(legacy.pmk));
        profile->hasKey = 1;
    }
    MemSet(&legacy, sizeof(legacy), 0);
    return writeProfile(profile);
}

static Int16 chooseSecurity(void) {
    enum { SecurityForm = 6520, SecurityLabel, SecurityWpa2, SecurityStock, SecurityCancel };
    FormType *form = FrmNewForm(SecurityForm, "Wi-Fi Security", 5, 39, 150, 76,
                                true, SecurityWpa2, 0, 0);
    if (!form) return -1;
    UInt16 choice = SecurityCancel;
    if (FrmNewLabel(&form, SecurityLabel, "How should this network connect?", 8, 20, stdFont) &&
        CtlNewControl((void **)&form, SecurityWpa2, buttonCtl, "WPA2", 8, 44, 40, 14,
                      stdFont, 0, true) &&
        CtlNewControl((void **)&form, SecurityStock, buttonCtl, "Stock Wi-Fi", 52, 44, 46, 14,
                      stdFont, 0, true) &&
        CtlNewControl((void **)&form, SecurityCancel, buttonCtl, "Cancel", 102, 44, 40, 14,
                      stdFont, 0, true)) choice = FrmDoDialog(form);
    FrmDeleteForm(form);
    return choice == SecurityWpa2 ? 1 : choice == SecurityStock ? 0 : -1;
}
#else
static Boolean hasSavedKey(void) {
    Wpa2SavedCredential stored;
    UInt16 size = sizeof(stored);
    const Int16 version = PrefGetAppPreferences(WPA2_FEATURE_CREATOR, Wpa2SavedPreference,
                                                &stored, &size, true);
    const Boolean found = version == Wpa2SavedVersion && size == sizeof(stored) &&
                          !MemCmp(stored.ssid, WPA2_LAB_SSID, sizeof(stored.ssid));
    MemSet(&stored, sizeof(stored), 0);
    return found;
}
#endif

static void clearSessionKey(const Char *ssid) {
    UInt32 value;
    if (FtrGet(WPA2_FEATURE_CREATOR, Wpa2CredentialFeature, &value) || !value) return;
    Wpa2Credential *const credential = (Wpa2Credential *)value;
#ifdef WPA2_CLEAR_WEP_PROFILE
    if (StrCompare(credential->ssid, ssid)) return;
#else
    (void)ssid;
#endif
    MemSet(credential, sizeof(*credential), 0);
}

static Boolean forgetSavedKey(const Char *ssid) {
    clearSessionKey(ssid);
#ifdef WPA2_CLEAR_WEP_PROFILE
    Wpa2Profile profile;
    if (!readProfile(ssid, &profile)) return false;
    profile.hasKey = 0;
    MemSet(profile.pmk, sizeof(profile.pmk), 0);
    return writeProfile(&profile);
#else
    PrefSetAppPreferences(WPA2_FEATURE_CREATOR, Wpa2SavedPreference, Wpa2SavedVersion, NULL, 0, true);
    UInt16 size = 0;
    return PrefGetAppPreferences(WPA2_FEATURE_CREATOR, Wpa2SavedPreference,
                                 NULL, &size, true) == noPreferenceFound;
#endif
}

static UInt16 chooseSavedKey(const Char *ssid) {
    FormType *form = FrmNewForm(SavedKeyForm, "Saved WPA2 Key", 5, 36, 150, 85,
                                true, SavedKeyConnect, 0, 0);
    if (!form) return SavedKeyCancel;
    UInt16 choice = SavedKeyCancel;
    if (FrmNewLabel(&form, SavedKeyLabel, "Use the saved key?", 8, 20, stdFont) &&
        CtlNewControl((void **)&form, SavedKeyConnect, buttonCtl, "Connect", 8, 40, 62, 14,
                      stdFont, 0, true) &&
        CtlNewControl((void **)&form, SavedKeyChange, buttonCtl, "Change", 80, 40, 62, 14,
                      stdFont, 0, true) &&
        CtlNewControl((void **)&form, SavedKeyForget, buttonCtl, "Forget", 8, 60, 62, 14,
                      stdFont, 0, true) &&
        CtlNewControl((void **)&form, SavedKeyCancel, buttonCtl, "Cancel", 80, 60, 62, 14,
                      stdFont, 0, true)) {
        while ((choice = FrmDoDialog(form)) == SavedKeyForget) {
            if (forgetSavedKey(ssid)) break;
            FrmCopyLabel(form, SavedKeyLabel, "Could not forget.");
            SndPlaySystemSound(sndError);
        }
    }
    FrmDeleteForm(form);
    return choice;
}

static Boolean storePassphrase(const Char *ssid, const Char *text, UInt16 length) {
    UInt32 value;
    Wpa2Credential *credential;
    const Err result = FtrGet(WPA2_FEATURE_CREATOR, Wpa2CredentialFeature, &value);
    if (result == ftrErrNoSuchFeature) {
        credential = MemPtrNew(sizeof(*credential));
        if (!credential) return false;
        MemSet(credential, sizeof(*credential), 0);
        if (MemPtrSetOwner(credential, 0) ||
            FtrSet(WPA2_FEATURE_CREATOR, Wpa2CredentialFeature, (UInt32)credential)) {
            MemPtrFree(credential);
            return false;
        }
    } else {
        if (result || !value) return false;
        credential = (Wpa2Credential *)value;
    }

    MemSet(credential, sizeof(*credential), 0);
    MemMove(credential->bytes, text, length);
#ifdef WPA2_CLEAR_WEP_PROFILE
    StrCopy(credential->ssid, ssid);
#else
    (void)ssid;
#endif
    credential->length = length;
    credential->kind = Wpa2Passphrase;
    return true;
}

static FormType *makeDialog(void) {
    FormType *form = FrmNewForm(PassphraseForm, "WPA2 Passphrase", 5, 39, 150, 80,
                                true, PassphraseConnect, 0, 0);
    if (!form) return NULL;
    if (FrmNewLabel(&form, PassphraseLabel, "Enter 8 to 63 printable characters.", 8, 20, stdFont) &&
        FldNewField((void **)&form, PassphraseField, 8, 35, 134, 12, stdFont, Wpa2PassphraseMax,
                    true, true, true, false, leftAlign, false, false, false) &&
        CtlNewControl((void **)&form, PassphraseConnect, buttonCtl, "Connect", 8, 59, 46, 14,
                      stdFont, 0, true) &&
        CtlNewControl((void **)&form, PassphraseCancel, buttonCtl, "Cancel", 61, 59, 44, 14,
                      stdFont, 0, true)) return form;
    FrmDeleteForm(form);
    return NULL;
}

static Int16 selectCredential(const Char *ssid, Boolean saved) {
    if (saved) {
        const UInt16 choice = chooseSavedKey(ssid);
        if (choice == SavedKeyConnect) {
            clearSessionKey(ssid);
            return 1;
        }
        if (choice != SavedKeyChange) return -1;
    }

    FormType *form = makeDialog();
    if (!form) return -1;
    const UInt16 fieldIndex = FrmGetObjectIndex(form, PassphraseField);
    FieldType *field = FrmGetObjectPtr(form, fieldIndex);
    Int16 result = -1;
    for (;;) {
        FrmSetFocus(form, fieldIndex);
        if (FrmDoDialog(form) != PassphraseConnect) break;
        const Char *text = FldGetTextPtr(field);
        const UInt16 length = FldGetTextLength(field);
        if (wpa2ValidPassphrase((const unsigned char *)text, length)) {
            if (storePassphrase(ssid, text, length)) result = 1;
            break;
        }
        FrmCopyLabel(form, PassphraseLabel, "Use 8 to 63 printable characters.");
        SndPlaySystemSound(sndError);
    }
    Char *text = FldGetTextPtr(field);
    if (text) MemSet(text, FldGetTextLength(field), 0);
    FrmDeleteForm(form);
    return result;
}

// The selected 274-byte Wpnl list record contains its NUL-terminated SSID at +11.
// A negative return keeps Setup on the network list. Other results follow the stock path.
Int16 wifiPassphrasePrompt(UInt8 *network) {
    const Char *const ssid = (const Char *)network + 11;
#ifdef WPA2_CLEAR_WEP_PROFILE
    UInt16 ssidLength = 0;
    while (ssidLength < Wpa2SsidSize && ssid[ssidLength]) ++ssidLength;
    if (!ssidLength || ssidLength == Wpa2SsidSize) return 0;
    Wpa2Profile profile;
    if (!readProfile(ssid, &profile)) {
        MemSet(&profile, sizeof(profile), 0);
        if (!network[10] || network[10] > 14 ||
            (network[44] != 3 && !network[4] && !network[5] && !network[6] && !network[7])) return 0;
        const Int16 choice = chooseSecurity();
        if (choice <= 0) return choice;
        if (!createProfile(ssid, &profile)) {
            SndPlaySystemSound(sndError);
            return -1;
        }
    }
    const Boolean saved = profile.hasKey != 0;
    MemSet(&profile, sizeof(profile), 0);
    const Int16 result = selectCredential(ssid, saved);
    if (result > 0) {
        // This physical driver performs RSN itself. Leave the stock WEP dialog out of the path.
        MemSet(network + 4, 4, 0);
        network[44] = 0;
    }
    return result;
#else
    if (StrCompare(ssid, WPA2_LAB_SSID)) return 0;
    return selectCredential(ssid, hasSavedKey());
#endif
}
