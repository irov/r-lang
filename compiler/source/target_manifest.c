#include "frontend_internal.h"

#include <string.h>

typedef struct RTargetManifestDigest {
    const char *profile;
    uint8_t digest[32];
} RTargetManifestDigest;

/* Every committed target manifest, keyed by the profile it binds (R-CONF-G005). */
static const RTargetManifestDigest r_target_manifest_digests[] = {
#include "target_manifest_digests.generated.inc"
};

/*
 * The freestanding profile binds its own manifest; every hosted profile of this target compiles
 * against the hosted-native-async manifest, whose C ABI it shares.
 */
bool r_frontend_target_manifest_supported(const uint8_t *manifest,
                                          size_t manifest_length,
                                          bool freestanding) {
    const char *profile = freestanding ? "freestanding" : "hosted-native-async";
    uint8_t actual_digest[32];
    size_t index;

    if ((manifest == NULL) || (manifest_length == 0U)) {
        return false;
    }
    r_sha256_digest(manifest, manifest_length, actual_digest);
    for (index = 0U;
         index < sizeof(r_target_manifest_digests) / sizeof(r_target_manifest_digests[0]);
         ++index) {
        const RTargetManifestDigest *entry = &r_target_manifest_digests[index];

        if ((strcmp(entry->profile, profile) == 0) &&
            (memcmp(actual_digest, entry->digest, sizeof(actual_digest)) == 0)) {
            return true;
        }
    }
    return false;
}
