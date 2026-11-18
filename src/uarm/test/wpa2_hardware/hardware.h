#ifndef PALMONE_WPA2_HARDWARE_H
#define PALMONE_WPA2_HARDWARE_H

#define HARDWARE_CREATOR 0x45325754u /* E2WT */
#define HARDWARE_LINK_BYTES 4896

#define WPA2_FEATURE_CREATOR HARDWARE_CREATOR
#define WPA2_CREDENTIAL_FEATURE 5

#ifndef __ASSEMBLER__
enum { TestState, TestBssidLow, TestAccessPoint, TestCleanup, TestGroup, TestCredentialSource = 6 };
enum { TestEnteredAndSaved = 1, TestUsedSaved, TestSaveFailed };
enum {
    TestIdle, TestArmed, TestBusy, TestInput, TestIdentity, TestConfig, TestEnable,
    TestJoin, TestAllocate, TestM1, TestM2, TestM3, TestM3Mic, TestVerified, TestInterrupts,
    TestM3Data, TestM4, TestEchoTx, TestEchoTimeout, TestEchoTag, TestEchoFormat, TestCcmpVerified,
    TestJoined, TestNeedPassphrase, TestNoAccessPoint,
};
#endif

#endif
