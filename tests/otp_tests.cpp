// otp_tests.cpp — Overby Transparency Protocol test suite.
//
// No external framework, for the same reason libcur has none: a transparency
// ledger whose tests need a package manager is a ledger that does not get
// tested by the people it is meant to be checkable by.
//
// The tests that matter are adversarial. Anyone can verify that appending
// works. What has to be demonstrated is that the four abuses in otp_ledger.h
// are actually prevented, and each has a test that tries to commit it.

#include <cstdio>
#include <string>

#include "otp/otp_ledger.h"

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_current = "";

void check(bool ok, const char* expr, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL  %s:%d  %s\n", g_current, line, expr);
    }
}

#define CHECK(e) check((e), #e, __LINE__)
#define CHECK_EQ(a, b) check((a) == (b), #a " == " #b, __LINE__)

struct Scope {
    explicit Scope(const char* n) { g_current = n; std::printf("%s\n", n); }
};
#define TEST(n) Scope scope_##__LINE__(n)

otp::Ledger make_ledger() {
    otp::Ledger l;
    l.set_commitment_key(0xA3F1C09E5511BB27ull);
    return l;
}

}  // namespace

// ---------------------------------------------------------------------------

static void test_people_are_never_named() {
    TEST("a person's identity does not enter the published record");

    otp::Ledger l = make_ledger();
    l.publish(otp::SUBJ_PERSON, "citizen-8812", "report_received",
              otp::ST_ALLEGED, 10, 3, "CUR-H.6 §6.8(a)", "report filed");

    const otp::Entry* e = l.get(1);
    CHECK(e != nullptr);
    if (e == nullptr) return;

    // The identity is gone from the entry, not merely marked private. A field
    // that still holds the name is a field that leaks the first time anyone
    // serialises the struct.
    CHECK_EQ(static_cast<int>(e->disclosure), static_cast<int>(otp::DISC_COMMITTED));
    CHECK(e->subject_id.empty());
    CHECK(e->subject_commitment != 0);

    // And it is absent from every output form.
    CHECK(l.to_json().find("citizen-8812") == std::string::npos);
    CHECK(l.to_canonical().find("citizen-8812") == std::string::npos);

    // An institution IS named, and that asymmetry is the design.
    l.publish(otp::SUBJ_INSTITUTION, "habitat-operator-4", "audit_lapsed",
              otp::ST_DETERMINED, 11, 2, "CUR-FOUNDATION-010 §5", "");
    CHECK(l.to_json().find("habitat-operator-4") != std::string::npos);

    // The unclassified default falls protective.
    CHECK_EQ(static_cast<int>(otp::disclosure_for(otp::SUBJ_PERSON)),
             static_cast<int>(otp::DISC_COMMITTED));
}

static void test_commitments_do_not_link_a_person_across_entries() {
    TEST("holding the record does not let a reader group a person's entries");

    otp::Ledger l = make_ledger();
    l.publish(otp::SUBJ_PERSON, "citizen-8812", "report_received",
              otp::ST_ALLEGED, 10, 3, "CUR-H.6 §6.8(a)", "");
    l.publish(otp::SUBJ_PERSON, "citizen-8812", "report_received",
              otp::ST_ALLEGED, 40, 3, "CUR-H.6 §6.8(a)", "");

    // Same person, two entries, different commitments. Without this a reader
    // could count how many times one unnamed person appears, which is most of
    // the way to identifying them.
    CHECK(l.get(1)->subject_commitment != l.get(2)->subject_commitment);

    // A party holding the key can still find them — that is what the key is
    // for, and CUR-H.6 §6.12 governs who holds it.
    CHECK_EQ(l.entries_committed_to(l.commitment_for("citizen-8812", 1)).size(),
             size_t{1});

    // A reader with the record but the wrong key computes nothing useful, so a
    // guess at someone's identity cannot be tested against the ledger.
    otp::Ledger wrong;
    wrong.set_commitment_key(1);
    CHECK(wrong.commitment_for("citizen-8812", 1) !=
          l.commitment_for("citizen-8812", 1));

    // Institutions remain fully linkable, which is the point of naming them.
    l.publish(otp::SUBJ_INSTITUTION, "tribunal-2", "review_lapsed",
              otp::ST_ALLEGED, 50, 4, "CUR-H.7 §7.12(c)(3)", "");
    l.publish(otp::SUBJ_INSTITUTION, "tribunal-2", "review_lapsed",
              otp::ST_ALLEGED, 90, 4, "CUR-H.7 §7.12(c)(3)", "");
    CHECK_EQ(l.entries_naming("tribunal-2").size(), size_t{2});
}

static void test_a_dismissal_cannot_be_lost() {
    TEST("an allegation later dismissed reads as dismissed");

    otp::Ledger l = make_ledger();
    l.publish(otp::SUBJ_INSTITUTION, "enterprise-7", "allegation",
              otp::ST_ALLEGED, 10, 3, "CUR-X.4 §4.3", "reported");

    CHECK_EQ(static_cast<int>(l.latest_standing(1)),
             static_cast<int>(otp::ST_ALLEGED));
    CHECK(!otp::is_finding(l.latest_standing(1)));

    const otp::Entry* c = l.correct(1, otp::ST_DISMISSED, 60, "not established");
    CHECK(c != nullptr);

    // The original is untouched — a record that edits itself cannot show that
    // it was ever wrong — but every reader resolving through latest_standing()
    // sees the dismissal.
    CHECK_EQ(static_cast<int>(l.get(1)->standing),
             static_cast<int>(otp::ST_ALLEGED));
    CHECK_EQ(static_cast<int>(l.latest_standing(1)),
             static_cast<int>(otp::ST_DISMISSED));

    // The JSON carries the resolved standing first, so a consumer reading the
    // obvious field gets the correct answer rather than the stale one.
    const std::string js = l.to_json();
    CHECK(js.find("\"standing\":\"dismissed\"") != std::string::npos);
    CHECK(js.find("\"asPublished\":\"alleged\"") != std::string::npos);

    // A determination overturned later reads as overturned, not erased.
    l.publish(otp::SUBJ_INSTITUTION, "enterprise-9", "finding",
              otp::ST_DETERMINED, 70, 4, "CUR-X.4 §4.9", "");
    CHECK(otp::is_finding(l.latest_standing(3)));
    l.correct(3, otp::ST_OVERTURNED, 200, "overturned on appeal");
    CHECK_EQ(static_cast<int>(l.latest_standing(3)),
             static_cast<int>(otp::ST_OVERTURNED));
    CHECK(!otp::is_finding(l.latest_standing(3)));
    CHECK_EQ(l.size(), size_t{4});  // nothing removed
}

static void test_tampering_is_localised() {
    TEST("altering or removing an entry is detected, and where");

    otp::Ledger l = make_ledger();
    for (int i = 0; i < 5; ++i) {
        l.publish(otp::SUBJ_INSTITUTION, "operator-1", "entry",
                  otp::ST_DETERMINED, static_cast<uint64_t>(i), 1, "cite", "d");
    }
    CHECK(l.verify().intact);

    // Round-trip through the canonical form, unaltered.
    otp::Ledger copy;
    CHECK(otp::Ledger::from_canonical(l.to_canonical(), &copy));
    CHECK(copy.verify().intact);
    CHECK_EQ(copy.digest(), l.digest());
    CHECK(l.compare(copy).identical);

    // Now edit an entry in the copy, as a publisher rewriting history would.
    std::string text = l.to_canonical();
    const size_t at = text.find("\x1f" "cite\x1f" "d");
    CHECK(at != std::string::npos);
    text.replace(at + 1, 4, "XXXX");

    otp::Ledger edited;
    CHECK(otp::Ledger::from_canonical(text, &edited));

    // The edit is caught, and the report says which entry rather than merely
    // that the ledger changed.
    const otp::Integrity r = edited.verify();
    CHECK(!r.intact);
    CHECK_EQ(r.first_bad_seq, uint64_t{1});
    CHECK(r.reason.find("altered") != std::string::npos);

    // Note what compare() does NOT catch here, because the distinction is the
    // architecture rather than a shortcoming. This tamperer edited the content
    // and left the stored hash alone, so the copy is internally inconsistent —
    // verify() catches it above, and compare() reports the two copies agreeing
    // because they still claim the same chain of hashes.
    CHECK(l.compare(edited).identical);

    // The two checks answer different questions and neither subsumes the other:
    //
    //   verify()   is this copy internally consistent?
    //   compare()  do two copies agree on the chain they claim?
    //
    // A careful operator rewriting history does not leave stale hashes. They
    // republish from scratch, and the result verifies perfectly — which is
    // exactly the attack a hash chain alone cannot answer, blockchains
    // included. It is caught by distribution, and only by distribution.
    otp::Ledger rewritten = make_ledger();
    for (int i = 0; i < 5; ++i) {
        rewritten.publish(otp::SUBJ_INSTITUTION, "operator-1", "entry",
                          otp::ST_DETERMINED, static_cast<uint64_t>(i), 1,
                          "cite", i == 2 ? "sanitised" : "d");
    }
    CHECK(rewritten.verify().intact);  // flawless, and false

    const otp::Divergence d = l.compare(rewritten);
    CHECK(!d.identical);
    CHECK_EQ(d.first_differing_seq, uint64_t{3});  // the first altered entry
    CHECK(l.digest() != rewritten.digest());
}

static void test_selective_publication_leaves_a_hole() {
    TEST("omitting an entry is visible before any hash is checked");

    otp::Ledger l = make_ledger();
    for (int i = 0; i < 4; ++i) {
        l.publish(otp::SUBJ_INSTITUTION, "operator-2", "entry",
                  otp::ST_DETERMINED, static_cast<uint64_t>(i), 1, "c", "d");
    }

    // Drop the third line of the body — the inconvenient entry.
    std::string text = l.to_canonical();
    size_t pos = 0;
    for (int i = 0; i < 3; ++i) pos = text.find('\n', pos) + 1;
    const size_t end = text.find('\n', pos) + 1;
    text.erase(pos, end - pos);

    otp::Ledger pruned;
    CHECK(otp::Ledger::from_canonical(text, &pruned));
    CHECK_EQ(pruned.size(), size_t{3});

    const otp::Integrity r = pruned.verify();
    CHECK(!r.intact);
    CHECK(r.reason.find("missing") != std::string::npos ||
          r.reason.find("gap") != std::string::npos);

    // Truncating the tail instead — the other way to publish selectively — is
    // caught by comparison against any other held copy.
    otp::Ledger truncated;
    std::string t2 = l.to_canonical();
    t2.erase(t2.rfind('\n', t2.size() - 2) + 1);
    CHECK(otp::Ledger::from_canonical(t2, &truncated));
    CHECK(truncated.verify().intact);  // internally consistent, and still short
    const otp::Divergence d = l.compare(truncated);
    CHECK(!d.identical);
    CHECK(d.length_differs);
}

static void test_the_ledger_imposes_nothing() {
    TEST("a record is not a court");

    // Nothing here produces a measure, a restriction, or a consequence.
    CHECK(!otp::Ledger::imposes_consequences());

    // An allegation is not a finding, and the distinction is available to
    // consumers rather than left for each to re-derive.
    CHECK(!otp::is_finding(otp::ST_ALLEGED));
    CHECK(!otp::is_finding(otp::ST_UNDER_REVIEW));
    CHECK(!otp::is_finding(otp::ST_DISMISSED));
    CHECK(!otp::is_finding(otp::ST_OVERTURNED));
    CHECK(otp::is_finding(otp::ST_DETERMINED));
    CHECK(otp::is_finding(otp::ST_REMEDIED));
}

int main() {
    std::printf("Overby Transparency Protocol %s\n\n", otp::OTP_VERSION);

    test_people_are_never_named();
    test_commitments_do_not_link_a_person_across_entries();
    test_a_dismissal_cannot_be_lost();
    test_tampering_is_localised();
    test_selective_publication_leaves_a_hole();
    test_the_ledger_imposes_nothing();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
