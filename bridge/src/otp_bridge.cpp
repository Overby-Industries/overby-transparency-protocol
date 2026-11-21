#include "otp/otp_bridge.h"

#include <algorithm>

#include "sha256.hpp"

namespace otp {
namespace {

constexpr const char* DIGEST_PREFIX = "sha256:";
constexpr size_t DIGEST_PREFIX_LEN = 7;
constexpr size_t DIGEST_HEX_LEN = 64;

bool is_lower_hex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }

// The ledger entry's detail is "sha256:<64 hex> <human summary>". The digest is
// what verification reads; the summary is for a person reading `git diff` on a
// mirror of the ledger, and is never trusted.
std::string make_detail(const std::string& digest, const std::string& summary) {
    return DIGEST_PREFIX + digest + " " + summary;
}

bool parse_detail_digest(const std::string& detail, std::string* digest) {
    if (detail.compare(0, DIGEST_PREFIX_LEN, DIGEST_PREFIX) != 0) return false;
    if (detail.size() < DIGEST_PREFIX_LEN + DIGEST_HEX_LEN) return false;
    for (size_t i = 0; i < DIGEST_HEX_LEN; ++i) {
        if (!is_lower_hex(detail[DIGEST_PREFIX_LEN + i])) return false;
    }
    const size_t end = DIGEST_PREFIX_LEN + DIGEST_HEX_LEN;
    if (detail.size() > end && detail[end] != ' ') return false;
    *digest = detail.substr(DIGEST_PREFIX_LEN, DIGEST_HEX_LEN);
    return true;
}

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.compare(0, prefix.size(), prefix) == 0;
}

// A token value from a packet, shortened for an error message. It is data from
// outside, so it is bounded rather than echoed whole.
std::string shown(const std::string& s) {
    std::string out = s.size() > 60 ? s.substr(0, 60) + "..." : s;
    for (char& c : out) {
        if (static_cast<unsigned char>(c) < 0x20) c = ' ';
    }
    return out;
}

Published failure(const std::string& why) {
    Published p;
    p.ok = false;
    p.error = why;
    return p;
}

Verification verdict(Verdict v, const std::string& why, uint64_t seq = 0) {
    Verification r;
    r.verdict = v;
    r.reason = why;
    r.seq = seq;
    return r;
}

}  // namespace

const char* to_string(Verdict v) {
    switch (v) {
        case Verdict::Verified: return "verified";
        case Verdict::NotAPacket: return "not_a_packet";
        case Verdict::NotPublishable: return "not_publishable";
        case Verdict::NoToken: return "no_token";
        case Verdict::ForeignToken: return "foreign_token";
        case Verdict::MalformedToken: return "malformed_token";
        case Verdict::TokenInvalid: return "token_invalid";
        case Verdict::EntryMismatch: return "entry_mismatch";
    }
    return "unknown";
}

std::string canonical_otf1(const otf1::EventPacket& event) {
    otf1::EventPacket copy = event;
    copy.proofOfCleanupToken.reset();
    return otf1::encode(copy);
}

std::string canonical_otb1(const otb1::Record& record) {
    otb1::Record copy = record;
    copy.certifications.proofOfInvestmentToken.reset();
    return otb1::encode(copy);
}

Bridge::Bridge(Ledger& ledger) : ledger_(ledger) {}

void Bridge::refresh_index() {
    const std::vector<Entry>& entries = ledger_.entries();
    for (; indexed_ < entries.size(); ++indexed_) {
        const Entry& e = entries[indexed_];
        std::string digest;
        const bool otf1_entry = e.citation == "OTF-1" && starts_with(e.kind, "otf1.");
        const bool otb1_entry = e.citation == "OTB-1" && starts_with(e.kind, "otb1.");
        if ((otf1_entry || otb1_entry) && parse_detail_digest(e.detail, &digest)) {
            // The first entry for a digest is the record; a later one with the
            // same digest (published by something that is not this bridge)
            // does not displace it.
            index_.emplace(e.citation + ":" + digest, e.seq);
        }
    }
}

Published Bridge::finish(const std::string& schema, const std::string& digest,
                         const std::optional<std::string>& existing_token, bool replace,
                         const std::function<const Entry&()>& append,
                         const std::function<std::string(const std::string&)>& stamp) {
    refresh_index();

    Published out;
    out.digest = digest;

    const auto found = index_.find(schema + ":" + digest);
    if (found != index_.end()) {
        const Entry* entry = ledger_.get(found->second);
        if (entry != nullptr) {
            const Token token = token_for(*entry);
            const std::string text = to_string(token);
            if (existing_token && *existing_token != text && !replace) {
                return failure("this record is already in the ledger as " + text +
                               ", but the packet carries a different token (" +
                               shown(*existing_token) + "); not overwritten");
            }
            out.ok = true;
            out.already_published = true;
            out.token = token;
            out.seq = token.seq;
            out.stamped = stamp(text);
            return out;
        }
    }

    if (existing_token && !replace) {
        return failure("the packet already carries a token (" + shown(*existing_token) +
                       ") that this ledger did not issue for it; it is not overwritten "
                       "unless replace_existing_token is set");
    }

    const Entry& entry = append();
    index_[schema + ":" + digest] = entry.seq;
    indexed_ = ledger_.entries().size();

    out.ok = true;
    out.token = token_for(entry);
    out.seq = entry.seq;
    out.stamped = stamp(to_string(out.token));
    return out;
}

Published Bridge::publish_otf1(const std::string& text, const PublishOptions& options) {
    if (ledger_.instance().empty()) {
        return failure("the ledger has no instance; call set_instance() before publishing");
    }

    otf1::Packet packet;
    std::string why;
    if (!otf1::decode(text, packet, &why)) return failure("not a valid OTF-1 packet: " + why);
    const otf1::EventPacket* event = std::get_if<otf1::EventPacket>(&packet);
    if (event == nullptr) {
        return failure("only OTF-1 event packets are published; a status, heartbeat, "
                       "assignment or ack is bookkeeping, not the public record");
    }
    if (event->timestamp < 0) return failure("an event dated before 1970 cannot be published");

    const std::string digest = detail::sha256_hex(canonical_otf1(*event));

    const auto append = [&]() -> const Entry& {
        std::string summary = std::string(otf1::toString(event->action)) + " by pod " +
                              event->podId + ", mission " + event->missionId;
        if (event->taskId) summary += ", task " + *event->taskId;
        // A pod is a machine, not a person, so it is named.
        return ledger_.publish(SUBJ_INSTITUTION, event->podId,
                               std::string("otf1.") + otf1::toString(event->action),
                               options.standing, static_cast<uint64_t>(event->timestamp),
                               options.severity, "OTF-1", make_detail(digest, summary));
    };
    const auto stamp = [&](const std::string& token) {
        otf1::EventPacket stamped = *event;
        stamped.proofOfCleanupToken = token;
        return otf1::encode(stamped);
    };
    return finish("OTF-1", digest, event->proofOfCleanupToken, options.replace_existing_token,
                  append, stamp);
}

Published Bridge::publish_otb1(const std::string& text, const Otb1Options& options) {
    if (ledger_.instance().empty()) {
        return failure("the ledger has no instance; call set_instance() before publishing");
    }

    otb1::Record record;
    std::string why;
    if (!otb1::decode(text, record, &why)) return failure("not a valid OTB-1 record: " + why);
    if (record.timestamp < 0) return failure("a record dated before 1970 cannot be published");

    const std::string vendor = record.financials.vendor;
    const bool withheld = options.vendor != VendorKind::LegalEntity;
    // The identity is replaced BEFORE the digest is taken, so the digest, the
    // stamped record and the ledger all agree on what was published.
    if (withheld) record.financials.vendor = WITHHELD_VENDOR;

    const std::string digest = detail::sha256_hex(canonical_otb1(record));

    const auto append = [&]() -> const Entry& {
        const std::string summary = record.investment.id + " " +
                                    otb1::toString(record.investment.type) + " " +
                                    otb1::toString(record.investment.status);
        // A legal entity is named. Anyone else is committed: the ledger
        // replaces the identity with a keyed commitment and keeps no trace of
        // it in the entry.
        return ledger_.publish(withheld ? SUBJ_PERSON : SUBJ_INSTITUTION, vendor,
                               std::string("otb1.") + otb1::toString(record.investment.type),
                               options.standing, static_cast<uint64_t>(record.timestamp),
                               options.severity, "OTB-1", make_detail(digest, summary));
    };
    const auto stamp = [&](const std::string& token) {
        otb1::Record stamped = record;
        stamped.certifications.proofOfInvestmentToken = token;
        return otb1::encode(stamped);
    };
    Published out = finish("OTB-1", digest, record.certifications.proofOfInvestmentToken,
                           options.replace_existing_token, append, stamp);
    out.vendor_withheld = out.ok && withheld;
    return out;
}

Verification Bridge::check_entry(const std::string& schema, const std::string& kind_prefix,
                                 const std::string& digest, const std::string& token_text) const {
    Token token;
    std::string why;
    if (!parse_token(token_text, &token, &why)) {
        return verdict(Verdict::MalformedToken, "the token looks like a coordinate but " + why);
    }

    const TokenCheck check = check_token(ledger_, token);
    if (!check.valid()) {
        Verification r = verdict(Verdict::TokenInvalid, check.reason, token.seq);
        r.token_status = check.status;
        return r;
    }

    const Entry* entry = ledger_.get(token.seq);
    if (entry == nullptr) {  // cannot happen after a valid check; refuse rather than assume
        return verdict(Verdict::TokenInvalid, "the entry vanished", token.seq);
    }
    if (entry->citation != schema || !starts_with(entry->kind, kind_prefix)) {
        return verdict(Verdict::EntryMismatch,
                       "the entry at that coordinate records something other than an " + schema +
                           " packet",
                       token.seq);
    }
    std::string recorded;
    if (!parse_detail_digest(entry->detail, &recorded)) {
        return verdict(Verdict::EntryMismatch,
                       "the entry at that coordinate does not commit to any packet", token.seq);
    }
    if (recorded != digest) {
        return verdict(Verdict::EntryMismatch,
                       "the entry at that coordinate commits to a different packet than this "
                       "one: the packet was altered, or the token belongs to another record",
                       token.seq);
    }
    return verdict(Verdict::Verified, "", token.seq);
}

Verification Bridge::verify_otf1(const std::string& text) const {
    otf1::Packet packet;
    std::string why;
    if (!otf1::decode(text, packet, &why)) {
        return verdict(Verdict::NotAPacket, "not a valid OTF-1 packet: " + why);
    }
    const otf1::EventPacket* event = std::get_if<otf1::EventPacket>(&packet);
    if (event == nullptr) {
        return verdict(Verdict::NotPublishable,
                       "only event packets are recorded in the ledger; this is not one");
    }
    if (!event->proofOfCleanupToken) {
        return verdict(Verdict::NoToken, "the packet has no ProofOfCleanupToken");
    }
    if (!looks_like_token(*event->proofOfCleanupToken)) {
        return verdict(Verdict::ForeignToken,
                       "the token is not a ledger coordinate (" + shown(*event->proofOfCleanupToken) +
                           "); it cannot be checked here");
    }
    return check_entry("OTF-1", "otf1.", detail::sha256_hex(canonical_otf1(*event)),
                       *event->proofOfCleanupToken);
}

Verification Bridge::verify_otb1(const std::string& text) const {
    otb1::Record record;
    std::string why;
    if (!otb1::decode(text, record, &why)) {
        return verdict(Verdict::NotAPacket, "not a valid OTB-1 record: " + why);
    }
    if (!record.certifications.proofOfInvestmentToken) {
        return verdict(Verdict::NoToken, "the record has no ProofOfInvestmentToken");
    }
    const std::string& token = *record.certifications.proofOfInvestmentToken;
    if (!looks_like_token(token)) {
        return verdict(Verdict::ForeignToken, "the token is not a ledger coordinate (" +
                                                  shown(token) + "); it cannot be checked here");
    }
    return check_entry("OTB-1", "otb1.", detail::sha256_hex(canonical_otb1(record)), token);
}

}  // namespace otp
