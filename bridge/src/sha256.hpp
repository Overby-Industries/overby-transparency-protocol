#pragma once

// SHA-256 (FIPS 180-4), for the bridge's content digest.
//
// The ledger's own chain hash is FNV-1a/64: fine against accident, not against
// an adversary. The bridge's binding between an entry and the packet it records
// does not have to inherit that, and a collision-resistant digest costs about a
// hundred lines. Implemented here rather than taken from a library so that the
// bridge, like the ledger, builds with nothing but the C++ standard library.

#include <string>

namespace otp {
namespace detail {

// Lower-case hex of SHA-256(data): 64 characters.
std::string sha256_hex(const std::string& data);

}  // namespace detail
}  // namespace otp
