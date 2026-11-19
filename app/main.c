#include <PalmOS.h>

#include "hardware.h"

enum { TestForm = 1000 };

static void line(const Char *text, Coord y) {
    WinDrawChars(text, StrLen(text), 8, y);
}

static UInt32 feature(UInt16 number) {
    UInt32 value;
    return FtrGet(HARDWARE_CREATOR, number, &value) ? 0 : value;
}

static void drawTarget(void) {
    const UInt32 target = feature(TestAccessPoint);
    if (!target) return;
    const UInt32 low = feature(TestBssidLow);
    const UInt8 bssid[] = {low, low >> 8, low >> 16, low >> 24, target, target >> 8};
    static const Char digits[] = "0123456789ABCDEF";
    Char address[18];
    for (UInt16 i = 0; i < sizeof(bssid); ++i) {
        address[3 * i] = digits[bssid[i] >> 4];
        address[3 * i + 1] = digits[bssid[i] & 0x0f];
        address[3 * i + 2] = ':';
    }
    address[sizeof(address) - 1] = '\0';
    line(address, 35);
}

static void drawStatus(UInt32 detail, UInt32 group, UInt32 source) {
    const RectangleType status = {{0, 51}, {160, 90}};
    WinEraseRectangle(&status, 0);
    const UInt16 stage = (UInt16)(detail >> 16);
    const Char *result = NULL;
    switch (stage) {
        case TestArmed:
        case TestIdle:
            line("Open Wi-Fi Setup.", 80);
            line("Select a WPA2 network.", 97);
            return;
        case TestBusy: result = "Connecting to WPA2 network"; break;
        case TestInput: result = "AP address missing or invalid"; break;
        case TestIdentity: result = "Card identity did not match"; break;
        case TestConfig: result = "Prism setup write failed"; break;
        case TestEnable: result = "Port enable failed"; break;
        case TestJoin: result = "Join request failed"; break;
        case TestAllocate: result = "TX buffer allocation failed"; break;
        case TestM1: result = "No valid Message 1"; break;
        case TestM2: result = "Message 2 transmit failed"; break;
        case TestM3: result = "No matching Message 3"; break;
        case TestM3Mic: result = "Message 3 MIC failed"; break;
        case TestVerified: result = "Message 3 verified"; break;
        case TestInterrupts: result = "Interrupt setup failed"; break;
        case TestM3Data: result = "Message 3 key data failed"; break;
        case TestM4: result = "Message 4 transmit failed"; break;
        case TestEchoTx: result = "Protected transmit failed"; break;
        case TestEchoTimeout: result = "No protected echo received"; break;
        case TestEchoTag: result = "Protected reply tag failed"; break;
        case TestEchoFormat: result = "Protected echo did not match"; break;
        case TestCcmpVerified: result = "CCMP verified both ways"; break;
        case TestJoined: result = "WPA2 link active"; break;
        case TestNeedPassphrase: result = "Enter key in Wi-Fi Setup"; break;
        case TestNoAccessPoint: result = "Rescan in Wi-Fi Setup"; break;
        default: result = "Unknown result"; break;
    }
    line(result, 57);
    Char statusText[40];
    const UInt32 cleanup = feature(TestCleanup);
    StrPrintF(statusText, "Stage %u  Detail %04X", stage, (UInt16)detail);
    line(statusText, 74);
    if (cleanup) {
        StrPrintF(statusText, "Cleanup error %u. Eject card.", (UInt16)cleanup);
        line(statusText, 91);
    } else if (stage == TestVerified) {
        line("Install the CCMP test driver.", 91);
    } else if (stage == TestCcmpVerified) {
        line("Install the DHCP test driver.", 91);
    } else if (stage == TestJoined) {
        StrPrintF(statusText, "GTK %u  Verified rekeys %u", (UInt16)group, (UInt16)(group >> 16));
        line(statusText, 91);
        if (source == TestEnteredAndSaved) line("Entered key saved", 108);
        if (source == TestUsedSaved) line("Used saved key", 108);
        if (source == TestSaveFailed) line("Could not save entered key", 108);
        StrPrintF(statusText, "Channel %u", (UInt16)(feature(TestAccessPoint) >> 16));
        line(statusText, 124);
        return;
    }
    if (stage != TestNeedPassphrase && stage != TestBusy) line("Soft-reset to try again.", 116);
}

UInt32 PilotMain(UInt16 command, void *parameters, UInt16 flags) {
    (void)parameters;
    (void)flags;
    if (command != sysAppLaunchCmdNormalLaunch) return errNone;
    FormType *form = FrmInitForm(TestForm);
    if (!form) return memErrNotEnoughSpace;
    FrmSetActiveForm(form);
    FrmDrawForm(form);
    drawTarget();
    UInt32 lastState = feature(TestState);
    UInt32 lastGroup = feature(TestGroup);
    UInt32 lastSource = feature(TestCredentialSource);
    drawStatus(lastState, lastGroup, lastSource);
    EventType event;
    do {
        EvtGetEvent(&event, SysTicksPerSecond());
        if (SysHandleEvent(&event)) continue;
        if (event.eType == frmUpdateEvent || event.eType == nilEvent) {
            const UInt32 state = feature(TestState);
            const UInt32 group = feature(TestGroup);
            const UInt32 source = feature(TestCredentialSource);
            if (event.eType == frmUpdateEvent) {
                FrmDrawForm(form);
                drawTarget();
            }
            if (event.eType == frmUpdateEvent || state != lastState || group != lastGroup ||
                source != lastSource) {
                drawTarget();
                lastState = state;
                lastGroup = group;
                lastSource = source;
                drawStatus(state, group, source);
            }
        } else {
            FrmHandleEvent(form, &event);
        }
    } while (event.eType != appStopEvent);
    FrmEraseForm(form);
    FrmDeleteForm(form);
    return errNone;
}
