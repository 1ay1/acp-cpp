// SPDX-License-Identifier: Apache-2.0
//
// Verifies acp/version.hpp constants are wired correctly from CMake.
//
#include <acp/version.hpp>
#include <acp/caps.hpp>

#include "agtest.hpp"
#include <cstdio>
#include <cstring>

TEST_CASE("version_smoke") {
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

    // The negotiation ceiling is never below the stable version, and equals it
    // unless the Draft v2 opt-in was compiled in.
    static_assert(kMaxProtocolVersion >= kProtocolVersion,
                  "ceiling must be >= stable version");
#if defined(ACP_ENABLE_V2_DRAFT)
    static_assert(kMaxProtocolVersion == 2, "v2 opt-in should raise ceiling to 2");
#else
    static_assert(kMaxProtocolVersion == kProtocolVersion,
                  "without opt-in the ceiling stays at the stable version");
#endif

    // Negotiation is min(ours, theirs), clamped to a floor of v1.
    assert(negotiate_version(1, 2) == 1);   // v1 agent, v2 client -> v1
    assert(negotiate_version(2, 1) == 1);   // v2 agent, v1 client -> v1
    assert(negotiate_version(2, 2) == 2);   // both v2 -> v2
    bool threw = false;
    try { (void)negotiate_version(1, 0); } catch (const CapabilityError&) { threw = true; }
    assert(threw && "a peer offering v0 has no compatible version");

    std::printf("acp-cpp %s, protocol v%d (major=%d minor=%d patch=%d)\n",
                kLibraryVersion, kProtocolVersion,
                kLibraryVersionMajor, kLibraryVersionMinor, kLibraryVersionPatch);
}
