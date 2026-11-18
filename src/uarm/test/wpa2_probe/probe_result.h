#ifndef WPA2_PROBE_RESULT_H
#define WPA2_PROBE_RESULT_H

enum ProbeResult {
    ProbeComplete = 0, ProbeNoFixture, ProbeCardError, ProbeLinkError, ProbeM1Error,
    ProbeM2Error, ProbeM3Error, ProbeReplayError, ProbeMicError, ProbeKeyError, ProbeM4Error,
    ProbeProtected, ProbeInstallError, ProbeUplinkError, ProbeDownlinkError,
    ProbeDownlinkReplay, ProbeDownlinkTag,
    ProbeRekeyed, ProbeRekeyError, ProbeRekeyReplay, ProbeRekeyMic,
};

#endif
