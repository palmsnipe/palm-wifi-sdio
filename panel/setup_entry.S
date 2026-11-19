    .section .text.entry,"ax"
    .global setupEntry
setupEntry:
    move.l %a2,-(%sp)
    bsr.w wifiPassphrasePrompt
    addq.l #4,%sp
    tst.w %d0
    bge.s resumeSelection
    addi.l #0x86,(%sp)  | Return to the stock handler's consumed-event path at 0x4940.
    rts
resumeSelection:
    cmpi.b #3,44(%a2)  | Displaced test for the wizard's manual network entry.
    rts
