#include "gcpad.h"

namespace gcpad {

#ifndef GCPAD_PACKAGE_VERSION
#define GCPAD_PACKAGE_VERSION "unknown"
#endif

#ifndef GCPAD_GIT_COMMIT
#define GCPAD_GIT_COMMIT "unknown"
#endif

#ifndef GCPAD_BUILD_TIMESTAMP
#define GCPAD_BUILD_TIMESTAMP "unknown"
#endif

#ifndef GCPAD_BUILD_GENERATOR
#define GCPAD_BUILD_GENERATOR "unknown"
#endif

static const BuildMetadata g_build_metadata = {
    GCPAD_PACKAGE_VERSION,
    GCPAD_GIT_COMMIT,
    GCPAD_BUILD_TIMESTAMP,
    GCPAD_BUILD_GENERATOR
};

BuildMetadata getBuildMetadata() {
    return g_build_metadata;
}

} // namespace gcpad
