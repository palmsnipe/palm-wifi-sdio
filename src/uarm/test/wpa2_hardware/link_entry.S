    .syntax unified
    .arm
    .section .text.entry,"ax"
    .global diagEntry
    .type diagEntry,%function
diagEntry:
    b linkCommand
    b readHeaderEntry
    b readPayloadEntry
    b linkTransmit
    b linkReadInfo

    .global originalCommand
    .type originalCommand,%function
originalCommand:
    ldr ip, [r9]
    b stockCommandResume

    .macro readEntry name, handler, skip
\name:
    push {r4, lr}
    sub sp, sp, #8
    ldr ip, [sp, #16]
    str ip, [sp]
    bl \handler
    add sp, sp, #8
    pop {r4, lr}
    cmn r0, #1
    addeq lr, lr, #\skip
    bx lr
    .endm

    readEntry readHeaderEntry, linkReadHeader, 0xec
    readEntry readPayloadEntry, linkReadPayload, 0x7c

    .global linkStorage
    .type linkStorage,%function
linkStorage:
    ldr r0, [r9, #12]
    ldr r0, [r0, #116]
    add r0, r0, #0x2000
    add r0, r0, #0x93  @ Round the space after the original 0x208c-byte globals up to eight.
    bic r0, r0, #7
    bx lr

    .global stockGetFeature
    .type stockGetFeature,%function
stockGetFeature:
    ldr ip, [r9, #-8]
    ldr pc, [ip, #0x424]

    .global stockSetFeature
    .type stockSetFeature,%function
stockSetFeature:
    ldr ip, [r9, #-8]
    ldr pc, [ip, #0x43c]

    .global stockGetSavedPreference
    .type stockGetSavedPreference,%function
stockGetSavedPreference:
    ldr ip, [r9, #-8]
    ldr pc, [ip, #0x5f0]

    .global stockSetSavedPreference
    .type stockSetSavedPreference,%function
stockSetSavedPreference:
    ldr ip, [r9, #-8]
    ldr pc, [ip, #0x608]
