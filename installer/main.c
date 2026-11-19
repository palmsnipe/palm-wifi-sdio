#include <PalmOS.h>
#include <ExpansionMgr.h>

enum { InstallForm = 1000, InstallButton = 1004, RestoreButton = 1005, ResultAlert = 1010 };
enum { Internal = 0, ResetRequiredFeature = 1 };

#define InstallerCreator 'E2WI'
#define StageType 'wTmp'
#define BackupType 'wBak'
#define PayloadType 'wDat'
#define MarkerName "E2WI transaction"
#define InstallPending 'wIns'
#define RestorePending 'wRes'
#define InstallDone 'wIdn'
#define RestoreDone 'wRdn'

typedef struct {
    const Char *name;
    UInt32 type, creator;
    UInt16 installResource, restoreResource;
} Component;

static const Component components[] = {
    {"SP3WLAN", 'libr', 'P3wl', 1000, 1030},
    {"WiFiPanel", 'panl', 'Wpnl', 1010, 1040},
    {"E2 WPA2 Test", 'appl', 'E2WT', 1020, 0},
};

enum { NetworkComponents = 2, AllComponents = sizeof(components) / sizeof(components[0]) };
enum { PayloadChunks = 10 };
#define PayloadChunkSize 30000UL

typedef struct {
    UInt16 resource;
    const UInt8 *firstChunk;
    UInt32 length, firstSize;
} Payload;

static void privateName(Char *name, const Char *prefix, const Component *component) {
    StrPrintF(name, "E2WI %s %s", prefix, component->name);
}

static Err identify(LocalID id, UInt32 type, UInt32 creator) {
    UInt32 foundType, foundCreator;
    Err error = DmDatabaseInfo(Internal, id, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, &foundType, &foundCreator);
    if (error) return error;
    return foundType == type && foundCreator == creator ? errNone : dmErrInvalidDatabaseName;
}

static Err setIdentity(LocalID id, const Char *name, UInt32 type, UInt32 creator) {
    return DmSetDatabaseInfo(Internal, id, name, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, &type, &creator);
}

static Err deletePrivate(const Char *name, UInt32 type) {
    LocalID id = DmFindDatabase(Internal, name);
    if (!id) return errNone;
    Err error = identify(id, type, InstallerCreator);
    return error ? error : DmDeleteDatabase(Internal, id);
}

static UInt16 word(const UInt8 *p) {
    return ((UInt16)p[0] << 8) | p[1];
}

static UInt32 longWord(const UInt8 *p) {
    return ((UInt32)word(p) << 16) | word(p + 2);
}

static Err copyResource(const Payload *payload, void *destination, UInt32 offset, UInt32 length) {
    UInt32 copied = 0;
    while (copied < length) {
        const UInt32 position = sizeof(UInt32) + offset + copied;
        const UInt16 chunk = position / PayloadChunkSize;
        const UInt32 skip = position % PayloadChunkSize;
        if (chunk >= PayloadChunks) return dmErrCorruptDatabase;
        MemHandle handle = chunk ? DmGetResource(PayloadType, payload->resource + chunk) : NULL;
        if (chunk && !handle) return dmErrResourceNotFound;
        const UInt32 size = chunk ? MemHandleSize(handle) : payload->firstSize;
        const UInt8 *bytes = chunk ? MemHandleLock(handle) : payload->firstChunk;
        UInt32 count = length - copied;
        Err error = dmErrCorruptDatabase;
        if (bytes && size > skip && size <= PayloadChunkSize) {
            if (count > size - skip) count = size - skip;
            error = DmWrite(destination, copied, bytes + skip, count);
        }
        if (handle) {
            if (bytes) MemHandleUnlock(handle);
            DmReleaseResource(handle);
        }
        if (error) return error;
        copied += count;
    }
    return errNone;
}

static Err createStaged(const Char *name, const Payload *payload) {
    const UInt8 *image = payload->firstChunk + sizeof(UInt32);
    const UInt32 length = payload->length;
    UInt16 attributes = word(image + 32);
    UInt16 version = word(image + 34);
    const UInt16 count = word(image + 76);
    const UInt32 tableEnd = 78 + 10UL * count;
    if (!(attributes & dmHdrAttrResDB) || !count || tableEnd > length ||
        sizeof(UInt32) + tableEnd > payload->firstSize ||
        longWord(image + 52) || longWord(image + 56) || longWord(image + 72)) {
        return dmErrCorruptDatabase;
    }
    Err error = DmCreateDatabase(Internal, name, InstallerCreator, StageType, true);
    if (error) return error;
    LocalID id = DmFindDatabase(Internal, name);
    if (!id) return dmErrCantFind;
    DmOpenRef database = DmOpenDatabase(Internal, id, dmModeReadWrite);
    if (!database) return DmGetLastErr();
    const UInt8 *entry = image + 78;
    for (UInt16 index = 0; index < count; ++index, entry += 10) {
        const UInt32 offset = longWord(entry + 6);
        const UInt32 end = index + 1 < count ? longWord(entry + 16) : length;
        if (offset < tableEnd || end <= offset || end > length) {
            error = dmErrCorruptDatabase;
            break;
        }
        MemHandle resource = DmNewResource(database, longWord(entry), word(entry + 4), end - offset);
        if (!resource) {
            error = DmGetLastErr();
            break;
        }
        void *destination = MemHandleLock(resource);
        error = destination ? copyResource(payload, destination, offset, end - offset) : memErrNotEnoughSpace;
        if (destination) MemHandleUnlock(resource);
        DmReleaseResource(resource);
        if (error) break;
    }
    Err closeError = DmCloseDatabase(database);
    if (error || closeError) return error ? error : closeError;
    return DmSetDatabaseInfo(Internal, id, NULL, &attributes, &version, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL);
}

static Err stageImage(const Component *component, UInt16 resource) {
    Char name[dmDBNameLength];
    privateName(name, "new", component);
    if (DmFindDatabase(Internal, name)) return dmErrAlreadyExists;

    MemHandle handle = DmGetResource(PayloadType, resource);
    if (!handle) return dmErrResourceNotFound;
    const UInt32 firstSize = MemHandleSize(handle);
    const UInt8 *firstChunk = MemHandleLock(handle);
    Err error = dmErrCorruptDatabase;
    if (firstChunk && firstSize >= sizeof(UInt32) + 78) {
        const UInt8 *image = firstChunk + sizeof(UInt32);
        const UInt32 length = longWord(firstChunk);
        if (length >= 78 && length <= PayloadChunks * PayloadChunkSize - sizeof(UInt32) &&
            !MemCmp(image, component->name, StrLen(component->name) + 1) &&
            longWord(image + 60) == component->type && longWord(image + 64) == component->creator) {
            const Payload payload = {resource, firstChunk, length, firstSize};
            error = createStaged(name, &payload);
        }
    }
    if (firstChunk) MemHandleUnlock(handle);
    DmReleaseResource(handle);
    if (error) return error;
    LocalID id = DmFindDatabase(Internal, name);
    return id ? identify(id, StageType, InstallerCreator) : dmErrCantFind;
}

static Err cleanStaging(void) {
    for (UInt16 i = 0; i < AllComponents; ++i) {
        Char name[dmDBNameLength];
        privateName(name, "new", &components[i]);
        Err error = deletePrivate(name, StageType);
        if (error) return error;
    }
    return errNone;
}

static Err finishTransaction(LocalID marker, UInt16 count, Boolean committed) {
    for (UInt16 i = 0; i < count; ++i) {
        const Component *component = &components[i];
        Char backup[dmDBNameLength], staged[dmDBNameLength];
        privateName(backup, "old", component);
        privateName(staged, "new", component);
        if (committed) {
            Err error = deletePrivate(backup, BackupType);
            if (error) return error;
            continue;
        }
        LocalID old = DmFindDatabase(Internal, backup);
        LocalID current = DmFindDatabase(Internal, component->name);
        LocalID incoming = DmFindDatabase(Internal, staged);
        // Keep the staged copy as evidence during recovery. It distinguishes
        // an untouched component from a newly installed status app.
        if (!incoming && current) {
            Err error = identify(current, component->type, component->creator);
            if (!error) error = setIdentity(current, staged, StageType, InstallerCreator);
            if (error) return error;
            current = 0;
        }
        if (old) {
            if (current) return dmErrAlreadyExists;
            Err error = identify(old, BackupType, InstallerCreator);
            if (!error) error = setIdentity(old, component->name, component->type, component->creator);
            if (error) return error;
        }
    }
    Err error = DmDeleteDatabase(Internal, marker);
    return error ? error : cleanStaging();
}

static Err recover(void) {
    LocalID marker = DmFindDatabase(Internal, MarkerName);
    if (!marker) return cleanStaging();
    UInt32 phase, creator;
    Err error = DmDatabaseInfo(Internal, marker, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, &phase, &creator);
    if (error) return error;
    if (creator != InstallerCreator) return dmErrInvalidDatabaseName;
    switch (phase) {
        case InstallPending: return finishTransaction(marker, AllComponents, false);
        case RestorePending: return finishTransaction(marker, NetworkComponents, false);
        case InstallDone: return finishTransaction(marker, AllComponents, true);
        case RestoreDone: return finishTransaction(marker, NetworkComponents, true);
        default: return dmErrCorruptDatabase;
    }
}

static Err activate(const Component *component) {
    Char name[dmDBNameLength];
    privateName(name, "new", component);
    LocalID incoming = DmFindDatabase(Internal, name);
    if (!incoming) return dmErrCantFind;
    Err error = identify(incoming, StageType, InstallerCreator);
    if (error) return error;
    LocalID old = DmFindDatabase(Internal, component->name);
    if (old) {
        privateName(name, "old", component);
        if (DmFindDatabase(Internal, name)) return dmErrAlreadyExists;
        error = setIdentity(old, name, BackupType, InstallerCreator);
        if (error) return error;
    }
    return setIdentity(incoming, component->name, component->type, component->creator);
}

static Err replaceComponents(Boolean restore) {
    const UInt16 count = restore ? NetworkComponents : AllComponents;
    for (UInt16 i = 0; i < count; ++i) {
        const Component *component = &components[i];
        LocalID old = DmFindDatabase(Internal, component->name);
        if (!old && i < NetworkComponents) return dmErrCantFind;
        if (old) {
            Err error = identify(old, component->type, component->creator);
            if (error) return error;
            DmOpenRef database = DmOpenDatabase(Internal, old, dmModeReadWrite | dmModeExclusive);
            if (!database) return DmGetLastErr();
            error = DmCloseDatabase(database);
            if (error) return error;
        }
        Char backup[dmDBNameLength];
        privateName(backup, "old", component);
        if (DmFindDatabase(Internal, backup)) return dmErrAlreadyExists;
    }
    for (UInt16 i = 0; i < count; ++i) {
        const Component *component = &components[i];
        Err error = stageImage(component, restore ? component->restoreResource : component->installResource);
        if (error) return error;
    }
    Err error = DmCreateDatabase(Internal, MarkerName, InstallerCreator,
                                  restore ? RestorePending : InstallPending, false);
    if (error) return error;
    LocalID marker = DmFindDatabase(Internal, MarkerName);
    if (!marker) return dmErrCantFind;
    error = FtrSet(InstallerCreator, ResetRequiredFeature, 1);
    if (error) return error;
    for (UInt16 i = 0; i < count; ++i) {
        error = activate(&components[i]);
        if (error) return error;
    }
    return setIdentity(marker, NULL, restore ? RestoreDone : InstallDone, InstallerCreator);
}

static Boolean slotEmpty(void) {
    UInt32 iterator = expIteratorStart;
    do {
        UInt16 slot;
        Err error = ExpSlotEnumerate(&slot, &iterator);
        if (error == expErrEnumerationEmpty) return true;
        if (error || ExpCardPresent(slot) != expErrCardNotPresent) return false;
    } while (iterator != expIteratorStop);
    return true;
}

static void runInstaller(Boolean restore) {
    UInt32 device, hal;
    // The Tungsten E2 ROM reports Zir4 as its OEM device ID and hspr as its HAL.
    if (FtrGet(sysFtrCreator, sysFtrNumOEMDeviceID, &device) || device != 'Zir4' ||
        FtrGet(sysFtrCreator, sysFtrNumOEMHALID, &hal) || hal != 'hspr') {
        FrmCustomAlert(ResultAlert, "This installer requires a Tungsten E2.", NULL, NULL);
        return;
    }
    if (!slotEmpty()) {
        FrmCustomAlert(ResultAlert, "Eject the card before continuing.", NULL, NULL);
        return;
    }
    UInt32 resetRequired;
    if (!FtrGet(InstallerCreator, ResetRequiredFeature, &resetRequired)) {
        FrmCustomAlert(ResultAlert, "Leave the card out. Power off, turn on, and soft-reset before continuing.", NULL, NULL);
        return;
    }
    UInt16 card;
    LocalID id;
    if (SysCurAppDatabase(&card, &id) || card != Internal) {
        FrmCustomAlert(ResultAlert, "Install this PRC into internal memory first.", NULL, NULL);
        return;
    }
    Err error = errNone;
    Err recovery = recover();
    if (!recovery) {
        error = replaceComponents(restore);
        // NVFS may still hold an old panel in cache after the rename. Leave
        // its backup and the transaction marker for the next boot.
        if (error && DmFindDatabase(Internal, MarkerName)) recovery = error;
        else if (error) recovery = cleanStaging();
    }
    if (recovery) {
        FrmCustomAlert(ResultAlert, "Leave the card out. Soft-reset and run this installer again.", NULL, NULL);
        return;
    }
    if (error) {
        Char message[100];
        if (error == dmErrCantFind) {
            StrCopy(message, "Install the original palmOne v1.2 Wi-Fi software first.");
        } else {
            StrPrintF(message, "Could not update Wi-Fi (%04X). Leave the card out and soft-reset before retrying.", error);
        }
        FrmCustomAlert(ResultAlert, message, NULL, NULL);
        return;
    }
    FrmCustomAlert(ResultAlert, restore
        ? "Stock Wi-Fi restored. Power off for five seconds, turn on, then soft-reset. Keep the card out until then."
        : "WPA2 installed. Power off for five seconds, turn on, then soft-reset. Keep the card out until then.",
        NULL, NULL);
}

UInt32 PilotMain(UInt16 command, void *parameters, UInt16 flags) {
    (void)parameters;
    (void)flags;
    if (command != sysAppLaunchCmdNormalLaunch) return errNone;
    FormType *form = FrmInitForm(InstallForm);
    if (!form) return memErrNotEnoughSpace;
    FrmSetActiveForm(form);
    FrmDrawForm(form);
    EventType event;
    do {
        EvtGetEvent(&event, evtWaitForever);
        if (SysHandleEvent(&event)) continue;
        if (event.eType == ctlSelectEvent) {
            const UInt16 control = event.data.ctlSelect.controlID;
            if (control == InstallButton || control == RestoreButton) runInstaller(control == RestoreButton);
        } else {
            FrmHandleEvent(form, &event);
        }
    } while (event.eType != appStopEvent);
    FrmEraseForm(form);
    FrmDeleteForm(form);
    return errNone;
}
