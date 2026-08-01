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
//      numbered them on arrival, an event that never arrived would leave no
//      hole.
//
//   2. A puller can lie by omission. If the registry decides what to collect,
//      it can decline to collect. When the publisher appends, the entry exists
//      whether the registry likes it or not — and this class cannot author
//      into a source's ledger at all, which is what can_author() is for.
//
//   3. Timestamps would record when the registry noticed rather than when the
//      thing happened, which is the property mirroring depends on for its
//      role as an anchor — design record §5.
//
// So publishers append to their own ledger, with their own chain and their own
// sequence. This registry holds COPIES and checks them. That is not a weaker
// position than authoring — it is the stronger one, because a party that only
// verifies cannot be the party that falsified.
//
// ---------------------------------------------------------------------------
// THIS IS THE COPY-HOLDER THE DESIGN ALREADY REQUIRED
// ---------------------------------------------------------------------------
// docs/DESIGN.md §5 explains that a hash chain cannot stop an operator who
// controls every copy from rewriting the whole thing, and that the answer is
// distributing copies among parties with divergent interests. A registry is
// the first and most diligent of those holders — but it is not the only one,
// and it must not be. Anyone may run one of these against the same publishers
// and compare, which is why digest() is exposed per source and overall.
//
// The load-bearing behaviour is in ingest(): a copy that DIVERGES from what is
// already held is not an update. It is an alarm. A publisher who rewrites
// history necessarily sends something that disagrees with what was collected
// before, and the registry keeps the earlier copy and reports the conflict
// rather than overwriting itself with the revision. Without that rule this
// class would be an efficient mechanism for laundering a rewrite.
//
// ---------------------------------------------------------------------------
// ONE REGISTRY, ONE INSTANCE
// ---------------------------------------------------------------------------
// Design record §2 requires that a simulated Commonwealth and a real company's
// spending never be mistaken for one another, and that the identifier travel
// inside the entry hash so it survives relabelling. A Registry completes that
// guarantee at the aggregation layer: it is bound to exactly one instance, set
// once, and ingest() refuses anything else outright. Mixing instances in one
// registry would make every cross-publisher report — find_naming(),
// standing_counts() — silently blend a rehearsal with the real world, which is
// the specific confusion the whole instance mechanism exists to foreclose.
// Aggregating both instances means running two Registry objects, on purpose.
//
// ---------------------------------------------------------------------------
// THE AGGREGATOR IS A PUBLISHER TOO
// ---------------------------------------------------------------------------
// A watchdog that only watches is unaccountable, which is exactly the shape
// CUR-X.4 §4.3 refuses for anyone else. So this class keeps its own ledger,
// in the same instance it aggregates, and publishes into it: which publisher
// went silent, which one attempted a rewrite, and when. own_ledger() exposes
// that ledger for mirroring on the same terms as any other publisher's — the
// registry appears in its own record rather than standing outside it.
//
// run_builtin_test() is the mechanism, named to match CURStateMachine's method
// of the same name and the same shape in the CUR corpus: a check that runs
// because the clock advanced, not because anything arrived. Silence is
// nothing arriving, which is exactly what a stimulus-triggered check cannot
// see.

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
    ING_WRONG_INSTANCE,    // does not belong to the instance this registry aggregates
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
    // same way — on the clock, by run_builtin_test(), not on receipt.
    uint64_t heartbeat_ticks = 0;

    // Whether the current silence has already been published into own_ledger_.
    // Without this, calling run_builtin_test() at every tick while a publisher
    // stays silent would append a fresh entry every tick — the aggregator's own
    // ledger would grow without bound over exactly the interval it is trying to
    // flag. The obligation is published once per silence, at the edge.
    bool silence_reported = false;

    // Retained across a later good ingest. A publisher that attempted a rewrite
    // once is a publisher whose record is worth reading differently, and
    // clearing the flag on their next well-formed submission would hand them a
    // reset for free.
    uint32_t rewrite_attempts = 0;
    uint64_t last_rewrite_tick = 0;
};

class Registry {
public:
    // Returns false unconditionally. This class holds copies of SOURCE ledgers
    // and checks them; it has no method that adds an entry to one. It is not
    // authorless, though — see own_ledger(). What it cannot do is put words in
    // a publisher's mouth.
    static constexpr bool can_author_into_sources() { return false; }

    // Bind this registry to the instance it aggregates. Refuses, and changes
    // nothing, once any source has been ingested — the same rule
    // Ledger::set_instance() applies, for the same reason: an aggregator that
    // could switch instances mid-collection could relabel everything it
    // already holds by relabelling itself instead of any individual ledger.
    bool set_instance(const std::string& instance,
                      const std::string& schema_version);
    const std::string& instance() const { return instance_; }

    void track(const std::string& publisher_id, uint64_t heartbeat_ticks = 0);

    // Ingest a publisher's canonical ledger text.
    //
    // Refuses a ledger belonging to a different instance than this registry
    // aggregates — checked against the incoming ledger's OWN entries via
    // verify(), not merely its header, so a header edited to claim this
    // registry's instance without altering the entries is still refused.
    //
    // Verifies before storing, and refuses rather than storing a broken chain —
    // a registry that keeps whatever it is handed launders it. Refuses a copy
    // that disagrees with entries already held, and records the attempt.
    IngestReport ingest(const std::string& publisher_id,
                        const std::string& canonical_text, uint64_t now_tick);

    // THE BUILT-IN TEST. Runs because the clock advanced, not because anything
    // arrived — the only way to notice that nothing has. Publishes a
    // publisher_silent entry into own_ledger() the moment a tracked publisher
    // first crosses its heartbeat, and returns the publisher ids newly found
    // silent at this call. Calling it again before the publisher is heard from
    // reports nothing further for that publisher; calling silent_publishers()
    // still reports it as currently silent regardless.
    std::vector<std::string> run_builtin_test(uint64_t now_tick);

    // Pure query, no side effect: publishers currently past their heartbeat.
    // Safe to call as often as wanted; it will not grow own_ledger().
    std::vector<std::string> silent_publishers(uint64_t now_tick) const;

    // Every publisher that has ever attempted a rewrite, with counts.
    std::vector<const Source*> publishers_with_rewrite_attempts() const;

    // The registry's own record of what it observed: silence and rewrite
    // attempts, published under the instance this registry aggregates. Not
    // authored by anyone else, mirrored on the same terms as any source —
    // design record §5, "the aggregator is a publisher too" above.
    const Ledger& own_ledger() const { return own_ledger_; }

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

    // Digest over every source's digest plus the registry's own ledger,
    // publisher-ordered. Two registries collecting from the same publishers
    // produce the same value; a divergence means one of them holds a copy the
    // other does not — which is the check that keeps a registry honest, and
    // the reason there should be more than one of these.
    uint64_t digest() const;

    std::string to_json() const;

private:
    Source* find_mutable(const std::string& publisher_id);
    void publish_own(SubjectKind kind, const std::string& subject_id,
                     const std::string& event_kind, uint64_t tick,
                     const std::string& detail);

    std::string instance_;
    std::string schema_version_;
    std::vector<Source> sources_;
    Ledger own_ledger_;
};

}  // namespace otp

#endif  // OTP_REGISTRY_H
