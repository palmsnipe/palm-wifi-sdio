#ifndef E2_INSTALLER_PATCH_H
#define E2_INSTALLER_PATCH_H

enum { Internal = 0 };
#define InstallerCreator 'E2WI'
#define StageType 'wTmp'
#define PatchType 'wPch'
#define UnsupportedStock (appErrorClass | 1)

static inline UInt16 word(const UInt8 *p) {
    return ((UInt16)p[0] << 8) | p[1];
}

static inline UInt32 longWord(const UInt8 *p) {
    return ((UInt32)word(p) << 16) | word(p + 2);
}

Err stageInstalled(const Char *name, LocalID original, UInt16 recipe, UInt16 count, Boolean restore);

#endif
