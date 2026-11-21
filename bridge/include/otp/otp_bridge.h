// otp_bridge.h — publish OTF-1 events and OTB-1 records into an Overby Ledger,
// and stamp them with a coordinate anyone can check.
//
// This is the layer docs/LEDGER.md §0 describes: OTF-1 and OTB-1 each have a
// field for "proof this record exists and has not been altered", and the ledger
// does that job for free. The core library supplies the coordinate (otp_token.h).
// The bridge adds the part the coordinate cannot do alone: making an entry
// COMMIT TO the packet it records.
//
// WHY THE ENTRY MUST COMMIT TO THE PACKET
// A coordinate proves an entry exists and is unaltered. It says nothing about
// what the entry is about, so a valid token pasted into an unrelated packet
// would still check out. So the entry carries a SHA-256 digest of the packet,
// and verification compares it with the packet in hand.
//
// THE CIRCLE, AND HOW IT IS BROKEN
// The token lives inside the packet, and the token is the hash of the entry,
// and the entry commits to the packet. Taken literally that is circular: the
// entry would have to contain its own hash. The way out is that the digest is
// taken over the packet WITH ITS TOKEN FIELD REMOVED. Publishing computes the
// digest, appends the entry, learns its coordinate, and only then writes the
// coordinate into the packet. Verifying removes the token again before
// computing the digest, which is why the packet before and after stamping
// yields the same digest.
//
// WHAT "THE PACKET" MEANS: THE CANONICAL FORM
// Two serialisations of one packet (different key order, whitespace, an offset
// instead of Z) must give one digest, or the same record could be published
// twice and the check could fail for a reason that has nothing to do with
// tampering. So the digest is taken over the canonical form: the packet is
// parsed and validated by the reference decoder (libotf1 or libotb1), its token
// is cleared, and it is re-encoded by the reference encoder, which writes a
// fixed member order, UTC timestamps and the shortest exact number form.
// bridge/tools/golden.py reproduces that in independent code and the tests hold
// the two to the same bytes. Anything the decoder rejects is not publishable.
//
// WHAT IS PUBLISHED
// Only OTF-1 `event` packets, and OTB-1 records. A status, heartbeat,
// assignment or ack is bookkeeping between a vehicle and its coordinator, not
// the public record, and is refused.
//
// THE ASYMMETRY, WHICH THE BRIDGE DOES NOT WEAKEN
// An OTB-1 record names a vendor. The ledger names institutions and commits
// people (docs/LEDGER.md §3), and docs/DESIGN.md §2 draws the line between a
// legal entity and a natural person. An OTB-1 record cannot say which a vendor
// is, so the CALLER must, and an unclassified vendor is treated as a person.
// For a person the bridge (a) has the ledger commit the identity, and (b)
// replaces the vendor in the record it returns with WITHHELD_VENDOR before
// digesting and stamping, because a ledger that commits a person while the
// packet that points at it names them has protected nobody. Publish only the
// `stamped` text the bridge returns, never the original record. The rule is
// docs/DESIGN.md's; whether it is the right rule for a given jurisdiction needs
// review by someone qualified before an operations ledger publishes (see the
// "Not legal advice" note there). Nothing else in a record is inspected: a
// person's name typed into `item` or `fundingSource` is not found.
//
// The ledger itself depends on no other Overby repository, and still does not.
// This bridge is a separate, optional target (OTP_BUILD_BRIDGE) that needs
// libotf1 and libotb1, taken as submodules under bridge/deps.

#ifndef OTP_BRIDGE_H
#define OTP_BRIDGE_H

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>

#include <otb1/otb1.hpp>
#include <otf1/otf1.hpp>

#include "otp/otp_ledger.h"
#include "otp/otp_token.h"

namespace otp {

// What replaces a natural person's name in a published OTB-1 record.
constexpr const char* WITHHELD_VENDOR = "[withheld: natural person]";

// Whether an OTB-1 vendor is a legal entity. A record cannot say, so the caller
// does. The default falls protective, as the ledger's own default does: wrongly
// naming a person cannot be undone, and wrongly protecting a company only means
// somebody has to ask.
enum class VendorKind {
    Unclassified,   // treated as a natural person
    LegalEntity,    // a registered company or other body: named
    NaturalPerson   // a sole trader or other individual: committed
};

struct PublishOptions {
    // The ledger's Standing for the entry. A completed event or a recorded
    // investment is a determined fact by default, as the README's example is.
    Standing standing = ST_DETERMINED;
    uint8_t severity = 0;

    // A packet may already carry something in its token field: a legacy
    // blockchain token, or a coordinate from somewhere else. The bridge will not
    // destroy it unless told to, because a token that was meaningful would be
    // lost silently.
    bool replace_existing_token = false;
};

struct Otb1Options : PublishOptions {
    VendorKind vendor = VendorKind::Unclassified;
};

struct Published {
    bool ok = false;
    std::string error;           // when !ok

    std::string stamped;         // the packet with its coordinate in the token field
    Token token;
    uint64_t seq = 0;
    std::string digest;          // SHA-256 of the canonical form, 64 hex digits

    // The same record had already been published; its existing entry was
    // reused and nothing was appended.
    bool already_published = false;

    // OTB-1 only: the vendor was withheld from `stamped` and committed in the
    // ledger, because it was not established to be a legal entity.
    bool vendor_withheld = false;
};

enum class Verdict : uint8_t {
    Verified = 0,     // the coordinate checks out AND its entry is about this packet
    NotAPacket,       // the text is not valid OTF-1 / OTB-1
    NotPublishable,   // valid OTF-1, but not an event: nothing would have been recorded
    NoToken,          // the token field is absent
    ForeignToken,     // the field holds something that is not a ledger coordinate
    MalformedToken,   // it looks like a coordinate and is not one
    TokenInvalid,     // the coordinate does not check out against this ledger
    EntryMismatch     // the coordinate is valid, but its entry is about something else
};

const char* to_string(Verdict v);

struct Verification {
    Verdict verdict = Verdict::NotAPacket;
    std::string reason;
    TokenStatus token_status = TOKEN_VALID;  // meaningful for TokenInvalid
    uint64_t seq = 0;                        // the entry examined, when there was one
    bool verified() const { return verdict == Verdict::Verified; }
};

// The canonical form the digest is taken over: the packet with its token field
// cleared, re-encoded by the reference encoder. Public so anyone building a
// verifier in another language can be held to the same bytes.
std::string canonical_otf1(const otf1::EventPacket& event);
std::string canonical_otb1(const otb1::Record& record);

class Bridge {
public:
    // `ledger` must outlive the bridge. It must already be identified
    // (set_instance), because an entry that does not say which ledger it
    // belongs to is exactly the confusion the instance field exists to prevent.
    explicit Bridge(Ledger& ledger);

    Published publish_otf1(const std::string& packet, const PublishOptions& options = {});
    Published publish_otb1(const std::string& record, const Otb1Options& options = {});

    // Is `stamped` a packet this ledger recorded? Both halves: the coordinate
    // checks out against the ledger, and the entry it names commits to this
    // packet's canonical form.
    Verification verify_otf1(const std::string& stamped) const;
    Verification verify_otb1(const std::string& stamped) const;

private:
    void refresh_index();
    Published finish(const std::string& schema, const std::string& digest,
                     const std::optional<std::string>& existing_token, bool replace,
                     const std::function<const Entry&()>& append,
                     const std::function<std::string(const std::string&)>& stamp);
    Verification check_entry(const std::string& schema, const std::string& kind_prefix,
                             const std::string& digest, const std::string& token_text) const;

    Ledger& ledger_;
    // "OTF-1:<digest>" -> seq, so publishing the same record twice finds the
    // entry already made. Rebuilt lazily from the ledger, so a ledger loaded
    // from a copy works the same as one filled in this process.
    std::map<std::string, uint64_t> index_;
    size_t indexed_ = 0;
};

}  // namespace otp

#endif  // OTP_BRIDGE_H
