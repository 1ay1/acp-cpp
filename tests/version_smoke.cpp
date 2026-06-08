// SPDX-License-Identifier: Apache-2.0
//
// Verifies acp/version.hpp constants are wired correctly from CMake.
//
#include <acp/version.hpp>

#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    using namespace acp;

    // semver components are non-negative and non-zero in aggregate
    static_assert(kLibraryVersionMajor >= 0, "major must be non-negative");
    static_assert(kLibraryVersionMinor >= 0, "minor must be non-negative");
    static_assert(kLibraryVersionPatch >= 0, "patch must be non-negative");
    static_assert(kLibraryVersionMajor + kLibraryVersionMinor + kLibraryVersionPatch > 0,
                  "library version must not be 0.0.0");

    // version string must exist and be non-empty
    assert(kLibraryVersion != nullptr);
    assert(std::strlen(kLibraryVersion) > 0);

    // protocol version must be a positive integer (the spec is integer-valued
    // and starts at 1)
    static_assert(kProtocolVersion >= 1, "protocol version starts at 1");

    std::printf("acp-cpp %s, protocol v%d (major=%d minor=%d patch=%d)\n",
                kLibraryVersion, kProtocolVersion,
                kLibraryVersionMajor, kLibraryVersionMinor, kLibraryVersionPatch);
    return 0;
}
