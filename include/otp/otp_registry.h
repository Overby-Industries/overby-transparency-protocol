// otp_registry.h — the aggregator. Ingests every publisher's ledger, verifies
// each one, reports across all of them, and notices when one goes quiet.
//
// ---------------------------------------------------------------------------
// WHY PUBLISHING PUSHES AND REPORTING PULLS
// ---------------------------------------------------------------------------
// The obvious simplification is to make this the only moving part: let the
// registry reach out, collect events from every system, and assign sequence
// numbers itself. Nobody would have to modify a publisher, and there would be
// one place to look.
//
// It cannot work that way, and the reason is not architectural taste:
//
//   1. Gap-free sequence numbers stop meaning anything. Omission is currently
//      detectable before a single hash is checked, because the PUBLISHER
//      assigns the sequence at the moment of the event. If the registry
//      numbers them on arrival, an event that never arrived leaves no hole.
//
//   2. A puller can lie by omission. If the registry decides what to collect,
//      it can decline to collect. When the publisher appends, the entry exists
//      whether the registry likes it or not — and this class cannot author
//      entries at all, which is what can_author() is for.
//
//   3. Timestamps would record when the registry noticed rather than when the
//      thing happened, which is the property anchoring depends on.
//
// So publishers append to their own ledger, with their own chain and their own
// sequence. This registry holds COPIES and checks them. That is not a weaker
// position than authoring — it is the stronger one, because a party that only
// verifies cannot be the party that falsified.
//
// ---------------------------------------------------------------------------
// THIS IS THE COPY-HOLDER THE DESIGN ALREADY REQUIRED
// ---------------------------------------------------------------------------
// docs/LEDGER.md §2 says a hash chain cannot stop an operator who controls
// every copy from rewriting the whole thing, and that the answer is
// distributing copies among parties with divergent interests. A registry is the
// first and most diligent of those holders — but it is not the only one, and it
// must not be. Anyone may run one of these against the same publishers and
// compare, which is why digest() is exposed per source and overall.
//
// The load-bearing behaviour is in ingest(): a copy that DIVERGES from what is
// already held is not an update. It is an alarm. A publisher who rewrites
// history necessarily sends something that disagrees with what was collected
// before, and the registry keeps the earlier copy and reports the conflict
// rather than overwriting itself with the revision. Without that rule this
// class would be an efficient mechanism for laundering a rewrite.

#ifndef OTP_REGISTRY_H
#define OTP_REGISTRY_H

#include <cstdint>
#include <string>
#include <vector>

#include "otp_ledger.h"

namespace otp {

// Why an ingest was refused or flagged.
enum IngestStatus : uint8_t {
    ING_ACCEPTED = 0,      // new entries appended to what we hold
    ING_UNCHANGED,         // identical to the copy already held
    ING_MALFORMED,         // could not be parsed
    ING_BROKEN_CHAIN,      // failed verify(); not stored
    ING_HISTORY_REWRITTEN, // disagrees with entries already held; not stored
    ING_TRUNCATED,         // shorter than what we hold; not stored
    ING_STATUS_COUNT
};

const char* to_string(IngestStatus s);

struct IngestReport {
    IngestStatus status = ING_ACCEPTED;
    std::string publisher_id;
    size_t entries_before = 0;
    size_t entries_after = 0;
    uint64_t conflict_seq = 0;  // where the divergence begins, if any
    std::string reason;

    bool ok() const {
        return status == ING_ACCEPTED || status == ING_UNCHANGED;
    }
};

// A publisher we collect from.
struct Source {
    std::string publisher_id;
    Ledger ledger;  // the copy we hold. Never authored here.

    uint64_t last_ingest_tick = 0;

    // Zero means no expectation. Non-zero means this publisher is expected to
    // be heard from at least this often, and silence beyond it is reportable.
    //
    // A publisher going quiet is the failure mode a pull architecture is worst
    // at noticing, precisely because nothing arrives to trigger anything. It is
    // the same shape as an obligation maturing unmet, and it is checked the
    // same way — on the clock, by silent_publishers(), not on receipt.
    uint64_t heartbeat_ticks = 0;

    // Retained across a later good ingest. A publisher that attempted a rewrite
    // once is a publisher whose record is worth reading differently, and
    // clearing the flag on their next well-formed submission would hand them a
    // reset for free.
    uint32_t rewrite_attempts = 0;
    uint64_t last_rewrite_tick = 0;
};

class Registry {
public:
    // Returns false unconditionally. This class holds copies and checks them.
    // It has no publish(), no correct(), and no way to add an entry that a
    // publisher did not sign off on by putting it in their own chain.
    static constexpr bool can_author() { return false; }

    void track(const std::string& publisher_id, uint64_t heartbeat_ticks = 0);

    // Ingest a publisher's canonical ledger text.
    //
    // Verifies before storing, and refuses rather than storing a broken chain —
    // a registry that keeps whatever it is handed launders it. Refuses a copy
    // that disagrees with entries already held, and records the attempt.
    IngestReport ingest(const std::string& publisher_id,
                        const std::string& canonical_text, uint64_t now_tick);

    // Publishers that have not been heard from within their heartbeat. Runs on
    // the clock rather than on receipt, because nothing arriving is exactly
    // what this detects.
    std::vector<std::string> silent_publishers(uint64_t now_tick) const;

    // Every publisher that has ever attempted a rewrite, with counts.
    std::vector<const Source*> publishers_with_rewrite_attempts() const;

    // Cross-publisher reporting. Entries naming an institution, across every
    // ledger held — the query a citizen actually wants and could not run
    // against six separate ledgers.
    struct Hit {
        std::string publisher_id;
        uint64_t seq = 0;
        Standing standing = ST_ALLEGED;  // resolved through corrections
    };
    std::vector<Hit> find_naming(const std::string& subject_id) const;

    // Counts by resolved standing across everything held, for a dashboard that
    // must not report allegations as findings.
    void standing_counts(uint32_t out[ST_COUNT]) const;

    const Source* source(const std::string& publisher_id) const;
    const std::vector<Source>& sources() const { return sources_; }
    size_t size() const { return sources_.size(); }

    // Digest over every source's digest, publisher-ordered. Two registries
    // collecting from the same publishers produce the same value; a divergence
    // means one of them holds a copy the other does not — which is the check
    // that keeps a registry honest, and the reason there should be more than
    // one of these.
    uint64_t digest() const;

    std::string to_json() const;

private:
    Source* find_mutable(const std::string& publisher_id);

    std::vector<Source> sources_;
};

}  // namespace otp

#endif  // OTP_REGISTRY_H
