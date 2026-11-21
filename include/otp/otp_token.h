// otp_token.h — a ledger coordinate, for carrying in a field that wants proof.
//
// OTF-1 and OTB-1 each have a field meant to hold "proof this record exists
// and has not been altered" (ProofOfCleanupToken, ProofOfInvestmentToken) and
// both describe it as a blockchain token. docs/LEDGER.md §0 replaces that with
// a coordinate into this ledger:
//
//     otp:1:8412:9f3c1a4e77b20d51
//         │ │    └─ the entry's hash, 16 lowercase hex digits
//         │ └────── the entry's sequence number
//         └──────── the coordinate format version
//
// Issuing one costs nothing, and checking one needs a copy of the ledger and
// no wallet, node or gas. Both fields are plain strings in their schemas, so
// the schemas do not change.
//
// WHAT A COORDINATE PROVES, AND WHAT IT DOES NOT
// A coordinate proves that an entry exists at that position and has not been
// altered since — given a ledger copy the checker trusts. It does NOT prove
// the entry is about any particular record. A token pasted into an unrelated
// packet would still check out. Closing that gap is the bridge's job
// (bridge/), which makes each entry commit to the packet it records so the
// two can be compared. This header is only the coordinate.
//
// The hash inside it is the ledger's own (FNV-1a/64): fine against accident
// and casual tampering, not against a determined adversary. docs/LEDGER.md §8
// says what that means and what to swap in. A coordinate inherits exactly
// that limit and claims nothing beyond it.

#ifndef OTP_TOKEN_H
#define OTP_TOKEN_H

#include <cstdint>
#include <string>

#include "otp/otp_ledger.h"

namespace otp {

// The only coordinate format version this library reads or writes.
constexpr uint32_t TOKEN_VERSION = 1;

struct Token {
    uint32_t version = TOKEN_VERSION;
    uint64_t seq = 0;
    uint64_t entry_hash = 0;
};

// The coordinate of an entry already in a ledger.
Token token_for(const Entry& e);

// "otp:1:8412:9f3c1a4e77b20d51". Always the same text for the same token.
std::string to_string(const Token& t);

// Parse exactly that form and nothing else. No whitespace, no sign, no leading
// zeros, no upper-case hex, no trailing text, and a version this library does
// not know is refused rather than guessed at — a later format may not mean
// what this one means. `error` (optional) says what was wrong.
bool parse_token(const std::string& text, Token* out, std::string* error = nullptr);

// Whether text is shaped like a coordinate at all, for callers deciding
// whether a field already holds one or holds something else entirely.
bool looks_like_token(const std::string& text);

enum TokenStatus : uint8_t {
    TOKEN_VALID = 0,        // the entry exists, matches, and its history is intact
    TOKEN_NO_SUCH_ENTRY,    // this ledger holds nothing at that sequence number
    TOKEN_HASH_MISMATCH,    // an entry is there, but its hash is not the token's
    TOKEN_CHAIN_BROKEN      // the ledger fails verify() at or before the entry
};

const char* to_string(TokenStatus s);

struct TokenCheck {
    TokenStatus status = TOKEN_VALID;
    std::string reason;
    bool valid() const { return status == TOKEN_VALID; }
};

// Check a coordinate against a ledger copy.
//
// Four things, in the order a reader would care about them:
//   * the ledger holds an entry at token.seq — a ledger that is merely behind
//     (a copy taken before the entry was published) reports NO_SUCH_ENTRY
//     rather than invalid, because that is a different problem;
//   * that entry's hash equals the token's, which also catches a token issued
//     by some other ledger, since the instance is inside every entry's hash;
//   * the ledger's chain verifies up to and including that entry. A break
//     AFTER the entry does not reach it: an entry is vouched for by what
//     precedes it, so a later alteration does not invalidate an earlier token;
//   * the entry itself recomputes to the hash it claims.
TokenCheck check_token(const Ledger& ledger, const Token& token);

}  // namespace otp

#endif  // OTP_TOKEN_H
