#include "otp/otp_token.h"

#include <cstdio>

namespace otp {
namespace {

constexpr const char* PREFIX = "otp:";

bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_lower_hex(char c) { return is_digit(c) || (c >= 'a' && c <= 'f'); }

// A canonical decimal: "0" or a non-zero digit followed by digits, with no
// sign and no leading zero, so one number has one spelling. Reads up to the
// next ':' or the end, advancing `pos`, and refuses anything that does not fit
// in 64 bits rather than wrapping.
bool read_decimal(const std::string& s, size_t& pos, uint64_t* out) {
    const size_t start = pos;
    uint64_t value = 0;
    while (pos < s.size() && is_digit(s[pos])) {
        const uint64_t digit = static_cast<uint64_t>(s[pos] - '0');
        if (value > (UINT64_MAX - digit) / 10) return false;  // would overflow
        value = value * 10 + digit;
        ++pos;
    }
    const size_t length = pos - start;
    if (length == 0) return false;
    if (length > 1 && s[start] == '0') return false;  // leading zero
    *out = value;
    return true;
}

bool fail(std::string* error, const char* why) {
    if (error != nullptr) *error = why;
    return false;
}

}  // namespace

Token token_for(const Entry& e) {
    Token t;
    t.seq = e.seq;
    t.entry_hash = e.entry_hash;
    return t;
}

std::string to_string(const Token& t) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "otp:%u:%llu:%016llx", static_cast<unsigned>(t.version),
                  static_cast<unsigned long long>(t.seq),
                  static_cast<unsigned long long>(t.entry_hash));
    return buf;
}

bool looks_like_token(const std::string& text) {
    return text.compare(0, 4, PREFIX) == 0;
}

bool parse_token(const std::string& text, Token* out, std::string* error) {
    if (!looks_like_token(text)) return fail(error, "does not start with \"otp:\"");

    size_t pos = 4;
    uint64_t version = 0;
    if (!read_decimal(text, pos, &version)) return fail(error, "the version is not a plain number");
    if (version != TOKEN_VERSION) return fail(error, "an unknown coordinate version");
    if (pos >= text.size() || text[pos] != ':') return fail(error, "expected ':' after the version");
    ++pos;

    uint64_t seq = 0;
    if (!read_decimal(text, pos, &seq)) return fail(error, "the sequence number is not a plain number");
    // Sequence numbers start at 1 in this ledger; 0 names nothing.
    if (seq == 0) return fail(error, "the sequence number must be at least 1");
    if (pos >= text.size() || text[pos] != ':') return fail(error, "expected ':' after the sequence number");
    ++pos;

    if (text.size() - pos != 16) return fail(error, "the hash must be exactly 16 hex digits");
    uint64_t hash = 0;
    for (size_t i = 0; i < 16; ++i, ++pos) {
        const char c = text[pos];
        if (!is_lower_hex(c)) return fail(error, "the hash must be lower-case hex digits");
        hash = (hash << 4) | static_cast<uint64_t>(is_digit(c) ? c - '0' : c - 'a' + 10);
    }

    if (out != nullptr) {
        out->version = static_cast<uint32_t>(version);
        out->seq = seq;
        out->entry_hash = hash;
    }
    return true;
}

const char* to_string(TokenStatus s) {
    switch (s) {
        case TOKEN_VALID: return "valid";
        case TOKEN_NO_SUCH_ENTRY: return "no_such_entry";
        case TOKEN_HASH_MISMATCH: return "hash_mismatch";
        case TOKEN_CHAIN_BROKEN: return "chain_broken";
    }
    return "unknown";
}

TokenCheck check_token(const Ledger& ledger, const Token& token) {
    TokenCheck result;

    // Locate by position, not just by number, so that a ledger whose entries
    // were reordered cannot answer for the wrong place.
    const std::vector<Entry>& entries = ledger.entries();
    size_t index = entries.size();
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].seq == token.seq) {
            index = i;
            break;
        }
    }
    if (index == entries.size()) {
        result.status = TOKEN_NO_SUCH_ENTRY;
        result.reason = "this ledger holds no entry at that sequence number (a copy taken "
                        "before it was published, or a coordinate that was never issued)";
        return result;
    }

    const Entry& entry = entries[index];
    if (entry.entry_hash != token.entry_hash) {
        result.status = TOKEN_HASH_MISMATCH;
        result.reason = "the entry at that sequence number has a different hash: it was "
                        "altered, or this coordinate came from a different ledger";
        return result;
    }

    // The ledger as a whole may fail verify() somewhere past this entry and
    // this entry still be sound: it is vouched for by what precedes it.
    const Integrity integrity = ledger.verify();
    if (!integrity.intact && index >= integrity.first_bad_index) {
        result.status = TOKEN_CHAIN_BROKEN;
        result.reason = "the ledger fails verification at or before this entry: " + integrity.reason;
        return result;
    }

    result.status = TOKEN_VALID;
    return result;
}

}  // namespace otp
