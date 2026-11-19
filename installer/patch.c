#include <PalmOS.h>
#include <Encrypt.h>

#include "patch.h"

enum { MaxRecipes = 2, RecipeHeader = 44, EditSize = 3, DigestSize = 16 };

typedef struct {
    MemHandle handle;
    const UInt8 *bytes;
    Boolean applied;
} Recipe;

static Err openRecipe(Recipe *recipe, UInt16 id) {
    recipe->handle = DmGetResource(PatchType, id);
    if (!recipe->handle) return dmErrResourceNotFound;
    recipe->bytes = MemHandleLock(recipe->handle);
    const UInt32 length = MemHandleSize(recipe->handle);
    const UInt8 *bytes = recipe->bytes;
    if (!bytes || length < RecipeHeader) return dmErrCorruptDatabase;
    const UInt16 stock = word(bytes + 6), patched = word(bytes + 8), count = word(bytes + 10);
    if (!stock || patched < stock || patched > 65530 ||
        length != RecipeHeader + (UInt32)EditSize * count + patched - stock)
        return dmErrCorruptDatabase;
    for (UInt16 i = 0; i < count; ++i)
        if (word(bytes + RecipeHeader + (UInt32)EditSize * i) >= stock) return dmErrCorruptDatabase;
    return errNone;
}

static Err cloneResource(DmOpenRef source, DmOpenRef destination, UInt16 index,
                         Recipe *recipes, UInt16 count, Boolean restore) {
    DmResType type;
    DmResID id;
    Err error = DmResourceInfo(source, index, &type, &id, NULL);
    if (error) return error;
    MemHandle input = DmGet1Resource(type, id);
    if (!input) return DmGetLastErr();
    const UInt8 *original = MemHandleLock(input);
    const UInt32 size = MemHandleSize(input);
    if (!original) error = memErrNotEnoughSpace;

    Recipe *recipe = NULL;
    for (UInt16 i = 0; i < count; ++i)
        if (type == longWord(recipes[i].bytes) && id == word(recipes[i].bytes + 4)) recipe = &recipes[i];
    const UInt8 *plan = recipe ? recipe->bytes : NULL;
    const UInt8 *expected = NULL;
    UInt32 targetSize = size, prefixSize = size;
    Boolean edit = false;
    UInt16 edits = 0;
    if (!error && plan) {
        const UInt16 stock = word(plan + 6), patched = word(plan + 8);
        const UInt8 *stockDigest = plan + 12, *patchedDigest = stockDigest + DigestSize;
        UInt8 digest[DigestSize];
        if (size != stock && size != patched) {
            error = UnsupportedStock;
        } else {
            error = EncDigestMD5((UInt8 *)original, size, digest);
            const Boolean stockMatch = !error && size == stock && !MemCmp(digest, stockDigest, DigestSize);
            const Boolean patchMatch = !error && size == patched && !MemCmp(digest, patchedDigest, DigestSize);
            if (!error && !(stockMatch || patchMatch)) error = UnsupportedStock;
            edit = restore ? patchMatch : stockMatch;
            expected = restore ? stockDigest : patchedDigest;
            targetSize = restore ? stock : patched;
            prefixSize = edit ? stock : targetSize;
            edits = word(plan + 10);
        }
    }

    if (!error) {
        MemHandle output = DmNewResource(destination, type, id, targetSize);
        if (!output) {
            error = DmGetLastErr();
        } else {
            UInt8 *bytes = MemHandleLock(output);
            error = bytes ? DmWrite(bytes, 0, original, prefixSize) : memErrNotEnoughSpace;
            if (!error && edit && targetSize > prefixSize) {
                const UInt8 *suffix = plan + RecipeHeader + (UInt32)EditSize * edits;
                error = DmWrite(bytes, prefixSize, suffix, targetSize - prefixSize);
            }
            if (edit) {
                const UInt8 *entry = plan + RecipeHeader;
                for (UInt16 i = 0; !error && i < edits; ++i, entry += EditSize) {
                    const UInt16 offset = word(entry);
                    const UInt8 patched = original[offset] ^ entry[2];
                    error = DmWrite(bytes, offset, &patched, sizeof(patched));
                }
            }
            if (!error && expected) {
                UInt8 digest[DigestSize];
                error = EncDigestMD5(bytes, targetSize, digest);
                if (!error && MemCmp(digest, expected, DigestSize)) error = dmErrCorruptDatabase;
                if (!error) recipe->applied = true;
            }
            if (bytes) MemHandleUnlock(output);
            DmReleaseResource(output);
        }
    }
    if (original) MemHandleUnlock(input);
    DmReleaseResource(input);
    return error;
}

static Err cloneDatabase(const Char *name, LocalID original, Recipe *recipes, UInt16 count, Boolean restore) {
    UInt16 attributes, version;
    Err error = DmDatabaseInfo(Internal, original, NULL, &attributes, &version, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL);
    if (error) return error;
    if (!(attributes & dmHdrAttrResDB)) return UnsupportedStock;
    error = DmCreateDatabase(Internal, name, InstallerCreator, StageType, true);
    if (error) return error;
    const LocalID id = DmFindDatabase(Internal, name);
    if (!id) return dmErrCantFind;
    DmOpenRef destination = DmOpenDatabase(Internal, id, dmModeReadWrite);
    if (!destination) return DmGetLastErr();
    // Open the original last and without a locale overlay so DmGet1Resource
    // reads the base resources in the same database as DmResourceInfo.
    DmOpenRef source = DmOpenDBNoOverlay(Internal, original, dmModeReadOnly);
    if (source) {
        const UInt16 resources = DmNumResources(source);
        for (UInt16 i = 0; !error && i < resources; ++i)
            error = cloneResource(source, destination, i, recipes, count, restore);
        Err closeError = DmCloseDatabase(source);
        if (!error) error = closeError;
    } else {
        error = DmGetLastErr();
    }
    Err closeError = DmCloseDatabase(destination);
    if (error || closeError) return error ? error : closeError;
    for (UInt16 i = 0; i < count; ++i)
        if (!recipes[i].applied) return UnsupportedStock;
    attributes &= ~dmHdrAttrOpen;
    return DmSetDatabaseInfo(Internal, id, NULL, &attributes, &version, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL);
}

Err stageInstalled(const Char *name, LocalID original, UInt16 recipe, UInt16 count, Boolean restore) {
    if (!original || !count || count > MaxRecipes) return dmErrInvalidParam;
    Recipe recipes[MaxRecipes] = {{0}};
    Err error = errNone;
    for (UInt16 i = 0; !error && i < count; ++i)
        error = openRecipe(&recipes[i], recipe + i);
    if (!error) error = cloneDatabase(name, original, recipes, count, restore);
    for (UInt16 i = 0; i < count; ++i) {
        if (recipes[i].bytes) MemHandleUnlock(recipes[i].handle);
        if (recipes[i].handle) DmReleaseResource(recipes[i].handle);
    }
    return error;
}
