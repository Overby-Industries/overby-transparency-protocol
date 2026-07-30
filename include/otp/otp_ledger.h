// otp_ledger.h — Overby Ledger, reference implementation.
//
// A free, append-only, tamper-evident public record. No chain, no token, no
// consensus, no fee. C++17, no external dependencies, no dependency on any
// other Overby repository — including the corpus it is most often used to
// publish. That independence is deliberate and §4 of the README explains it.
//
// ---------------------------------------------------------------------------
// WHY NOT A BLOCKCHAIN
// ---------------------------------------------------------------------------
// A blockchain buys one specific thing: agreement on ordering among parties who
// do not trust each other and share no authority. It is expensive because that
// is expensive — proof-of-work burns energy, proof-of-stake burns capital, and
// either way somebody pays per entry. A body publishing its own conduct is not
// in that situation. Nobody disputes who is publishing, and no rival chain is
// competing to be canonical.
//
// What is actually needed is tamper-EVIDENCE, and a hash chain gives that for
// free. Each entry commits to the one before it, so altering or removing an
// entry breaks every hash after it, and the break is localised to the edit
// rather than merely telling you something somewhere changed. Appending is
// O(1), verification is a linear walk, and the marginal cost of an entry is a
// few dozen bytes. Nobody mines, nobody stakes, and there is no token.
//
// The honest limit: a hash chain does not stop an operator who controls every
// copy from rewriting the whole thing and recomputing every hash. Nothing
// cryptographic fixes that — blockchains included. They solve it by
// distributing copies among parties with divergent interests, and so does this.
// Distribution is the free part: any party may hold a copy and compare digests,
// and a ledger that disagrees with copies held by others is detected by anyone
// who bothers to look. That is why to_canonical() and from_canonical() are core
// API rather than a convenience, and why compare() exists.
//
// ---------------------------------------------------------------------------
// THE ASYMMETRY, WHICH IS THE WHOLE DESIGN
// ---------------------------------------------------------------------------
// A record equally transparent about an institution and about a person who
// reported harm is not neutral. It is a weapon pointed at the person, because
// the institution can absorb exposure and the person cannot. A register of
// everyone ever accused of anything, searchable by name, is how a transparency
// system becomes an instrument for the exact harm it was built to surface.
//
// So disclosure is asymmetric, structurally rather than by policy:
//
//   NAMED      institutions, enterprises, authorisations, offices.
//              Fully linkable across entries. Pattern over an institution is
//              precisely what the record exists to reveal.
//
//   COMMITTED  people, and any subject not established to be an institution.
//              Identity replaced by a per-entry keyed commitment, unlinkable to
//              their other entries without a key held separately. Pattern over
//              a person is precisely what enables targeting, so it is not
//              available here.
//
// There is deliberately no by-name lookup for a committed subject and no
// parameter by which a caller elects to name one. The absence is the
// protection; see publish().
//
// ---------------------------------------------------------------------------
// FOUR ABUSES THIS IS BUILT AGAINST
// ---------------------------------------------------------------------------
// 1. Exposure of the vulnerable      — the asymmetry above.
// 2. The permanent accusation        — Standing distinguishes an allegation
//                                      from a finding, and corrections are
//                                      bound to the original so a dismissal
//                                      cannot be lost by reading only the first
//                                      entry. See latest_standing().
// 3. Selective publication           — sequence numbers are gap-free, so an
//                                      omission is a hole visible before any
//                                      hash is checked.
// 4. Quiet revision                  — the chain, plus distributed copies.

#ifndef OTP_LEDGER_H
#define OTP_LEDGER_H

#include <cstdint>
#include <string>
#include <vector>

namespace otp {

constexpr const char* OTP_VERSION = "1.0.0";

// How a subject appears in a published entry.
enum Disclosure : uint8_t {
    DISC_NAMED = 0,   // institutions, authorisations, offices
    DISC_COMMITTED,   // people, and anything not established to be otherwise
    DISC_COUNT
};

const char* to_string(Disclosure d);

// What a subject is, for the purpose of deciding disclosure. Kept minimal on
// purpose: this library takes no view on governance, only on who can absorb
// exposure.
enum SubjectKind : uint8_t {
    SUBJ_PERSON = 0,     // any being; the protective default
    SUBJ_INSTITUTION,    // body, enterprise, office, authorisation
    SUBJ_KIND_COUNT
};

// The rule, applied structurally rather than left to a caller's judgement.
// Note which way the default falls: an unclassified subject is COMMITTED,
// because wrongly naming a person is unrecoverable while wrongly protecting an
// institution only means somebody has to ask.
Disclosure disclosure_for(SubjectKind k);

// Whether an entry is an allegation or a finding. Publishing the first as
// though it were the second is how a record becomes a punishment no proceeding
// imposed.
enum Standing : uint8_t {
    ST_ALLEGED = 0,   // reported or detected; nothing determined
    ST_UNDER_REVIEW,  // being examined; still an allegation
    ST_DETERMINED,    // established with process
    ST_DISMISSED,     // examined and not established
    ST_REMEDIED,      // determined, and corrective action verified
    ST_OVERTURNED,    // determined, then overturned on appeal
    ST_COUNT
};

const char* to_string(Standing s);

// Only a determination carries a consequence anywhere downstream. Provided so
// consumers do not each re-derive it and get it wrong in different ways.
constexpr bool is_finding(Standing s) {
    return s == ST_DETERMINED || s == ST_REMEDIED;
}

struct Entry {
    // Which ledger this entry belongs to — CUR-X.5 §5.6(e) and the design
    // record §2. It is hashed, so it cannot be changed by renaming a file or
    // moving a directory.
    //
    // One protocol serves instances with entirely different subject matter: a
    // simulated Commonwealth's governance, and a real company's spending. A
    // reader must never be able to mistake one for the other, and the identifier
    // travels inside the entry precisely so that stripping the surrounding
    // context cannot make an Aevoria determination read as a real-world one.
    std::string instance;

    // The version of CUR-X.5 §5.2 this entry was published under. §5.6(e): no
    // entry is reinterpreted under a later version, so a subsequent narrowing of
    // the publishable set cannot reach entries already published.
    std::string schema_version;

    // Monotonic and gap-free by construction, so a removed entry shows as a
    // hole before any hash is checked.
    uint64_t seq = 0;
    uint64_t tick = 0;

    // What happened. Free-form so the protocol does not have to know the
    // vocabulary of every publisher.
    std::string kind;
    std::string citation;
    std::string detail;
    uint8_t severity = 0;

    Disclosure disclosure = DISC_COMMITTED;
    std::string subject_id;         // only when DISC_NAMED
    uint64_t subject_commitment = 0;  // only when DISC_COMMITTED

    Standing standing = ST_ALLEGED;

    // Zero for an original. Otherwise the seq of the entry this corrects.
    // Corrections are appended and bound, never applied in place: a record that
    // edits itself cannot show that it was ever wrong, and a pattern of being
    // wrong is exactly what a reader needs.
    uint64_t corrects_seq = 0;

    uint64_t prev_hash = 0;
    uint64_t entry_hash = 0;
};

struct Integrity {
    bool intact = true;
    uint64_t first_bad_seq = 0;
    size_t first_bad_index = 0;
    std::string reason;
};

struct Divergence {
    bool identical = true;
    uint64_t first_differing_seq = 0;
    bool length_differs = false;
    size_t mine = 0;
    size_t theirs = 0;
};

class Ledger {
public:
    // Returns false unconditionally. Nothing here produces a measure, a
    // restriction, or a consequence for any subject. A record that could act on
    // a subject would be a court, and this is a record. Stated so the
    // prohibition is greppable rather than resting on a comment.
    static constexpr bool imposes_consequences() { return false; }

    // Set once by the publisher and held separately from the ledger. Without
    // it, commitments cannot be computed — which is what makes them unlinkable
    // to a reader holding the record but not the key, and what stops a reader
    // testing a guess at someone's identity.
    void set_commitment_key(uint64_t k) { commitment_key_ = k; }

    // Identify this ledger. Every entry published afterwards carries both, and
    // both are hashed. CUR-X.5 §5.6(e).
    void set_instance(const std::string& instance, const std::string& schema_version);
    const std::string& instance() const { return instance_; }

    // Append an entry. Disclosure is decided from `kind` and cannot be
    // overridden: there is no parameter by which a caller elects to name a
    // person, and its absence is the protection.
    const Entry& publish(SubjectKind kind, const std::string& subject_id,
                         const std::string& event_kind, Standing standing,
                         uint64_t tick, uint8_t severity,
                         const std::string& citation,
                         const std::string& detail);

    // Append a correction bound to an earlier entry. The original is never
    // altered. Returns nullptr if `corrects_seq` names no entry.
    const Entry* correct(uint64_t corrects_seq, Standing new_standing,
                         uint64_t tick, const std::string& detail);

    // The standing an entry carries after every correction bound to it, applied
    // in sequence. THIS is what a reader must use. An allegation later
    // dismissed reads as dismissed, and a dismissal cannot be lost by someone
    // reading only the original entry.
    Standing latest_standing(uint64_t seq) const;

    // Walk the chain. Detects alteration, removal, and reordering, and reports
    // where rather than merely that.
    Integrity verify() const;

    // Digest over the whole ledger, for comparing one held copy against
    // another.
    uint64_t digest() const;

    // Compare against another copy and report the first sequence at which they
    // differ. This is the check distribution exists to make available.
    Divergence compare(const Ledger& other) const;

    // Open to anyone: pattern over an institution is what the record is for.
    std::vector<uint64_t> entries_naming(const std::string& subject_id) const;

    // Requires the commitment, which requires the key. There is deliberately no
    // by-name equivalent for a committed subject.
    std::vector<uint64_t> entries_committed_to(uint64_t commitment) const;

    uint64_t commitment_for(const std::string& subject_id, uint64_t seq) const;

    const Entry* get(uint64_t seq) const;
    const std::vector<Entry>& entries() const { return entries_; }
    size_t size() const { return entries_.size(); }

    // Canonical serialisation. Deterministic, used both for hashing and for
    // re-import, so a holder can load someone else's copy and compare it
    // byte-identically. One entry per line, tab-separated.
    //
    // Tab rather than an unprintable separator, because CUR-X.5 §5.8 makes
    // holding a copy a right and the copies are mirrored to public git. A
    // format producing diffs no human can read wastes the main advantage of
    // mirroring at all: an append shows as added lines, and a rewrite shows as
    // a modified line, visible in `git diff` with no tooling whatever.
    std::string to_canonical() const;
    static bool from_canonical(const std::string& text, Ledger* out);

    // Presentation only. Never hashed, never re-imported.
    std::string to_json(size_t max_entries = 0) const;

private:
    uint64_t hash_entry(const Entry& e) const;
    const Entry& append(Entry e);

    std::vector<Entry> entries_;
    uint64_t commitment_key_ = 0;
    std::string instance_;
    std::string schema_version_;
};

}  // namespace otp

#endif  // OTP_LEDGER_H
