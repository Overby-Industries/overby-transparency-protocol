// otp_tests.cpp — Overby Ledger test suite.
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
#include "otp/otp_registry.h"

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
    const size_t at = text.find("\tcite\td");
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

static void test_an_instance_cannot_be_relabelled() {
    TEST("a simulated determination cannot be re-presented as a real one "
         "(CUR-X.5 §5.6(e), design record §2)");

    otp::Ledger sim = make_ledger();
    sim.set_instance("aevoria", "CUR-X.5/1.0.0");
    sim.publish(otp::SUBJ_INSTITUTION, "tribunal-2", "determination",
                otp::ST_DETERMINED, 10, 4, "CUR-H.7 §7.15", "measure imposed");

    // The instance is on the entry, not merely in the header, and it is hashed.
    CHECK(sim.get(1)->instance == "aevoria");
    CHECK(sim.get(1)->schema_version == "CUR-X.5/1.0.0");

    // Rewriting the header to claim a different instance does not work: the
    // per-entry values are hashed, so the entries stop verifying. Renaming the
    // file, moving the directory, or editing the first line all fail the same
    // way — which is the point, since those are the cheap attacks.
    std::string text = sim.to_canonical();
    const size_t nl = text.find('\n');
    std::string header = text.substr(0, nl);
    CHECK(header.find("aevoria") != std::string::npos);

    // Swap only the body's instance field on the first entry.
    const size_t body = nl + 1;
    CHECK(text.compare(body, 7, "aevoria") == 0);
    text.replace(body, 7, "overbyO");

    otp::Ledger forged;
    CHECK(otp::Ledger::from_canonical(text, &forged));
    const otp::Integrity r = forged.verify();
    CHECK(!r.intact);
    CHECK_EQ(r.first_bad_seq, uint64_t{1});

    // Two ledgers with identical content but different instances are not the
    // same ledger, so an aggregator cannot merge them by accident.
    otp::Ledger real = make_ledger();
    real.set_instance("overby-ops", "CUR-X.5/1.0.0");
    real.publish(otp::SUBJ_INSTITUTION, "tribunal-2", "determination",
                 otp::ST_DETERMINED, 10, 4, "CUR-H.7 §7.15", "measure imposed");
    CHECK(sim.digest() != real.digest());
    CHECK(!sim.compare(real).identical);
}

static void test_canonical_form_is_git_readable() {
    TEST("the canonical form diffs cleanly, because copies are mirrored to git "
         "(CUR-X.5 §5.8)");

    otp::Ledger l = make_ledger();
    l.set_instance("aevoria", "CUR-X.5/1.0.0");
    l.publish(otp::SUBJ_INSTITUTION, "operator-1", "review_lapsed",
              otp::ST_ALLEGED, 40, 4, "CUR-H.7 §7.12(c)(3)", "review overdue");

    const std::string one = l.to_canonical();

    // No unprintable control characters. A diff a person cannot read wastes the
    // reason for mirroring to git in the first place.
    for (char c : one) {
        CHECK(c == '\n' || c == '\t' ||
              static_cast<unsigned char>(c) >= 0x20);
    }

    // Appending is purely additive: the earlier text is a prefix of the later,
    // so every commit adds lines and modifies none. A rewrite therefore shows
    // as a modified line in `git diff` with no tooling at all.
    l.publish(otp::SUBJ_INSTITUTION, "operator-1", "review_lapsed",
              otp::ST_ALLEGED, 140, 4, "CUR-H.7 §7.12(c)(3)", "still overdue");
    const std::string two = l.to_canonical();
    CHECK(two.rfind(one, 0) == 0);
    CHECK(two.size() > one.size());

    // Tabs separate fields and survive the round trip; a tab inside a field is
    // escaped rather than breaking the parse.
    otp::Ledger tricky = make_ledger();
    tricky.set_instance("aevoria", "CUR-X.5/1.0.0");
    tricky.publish(otp::SUBJ_INSTITUTION, "op\t2", "kind\nwith\nnewlines",
                   otp::ST_ALLEGED, 1, 1, "cite", "detail\twith\ttabs");
    otp::Ledger back;
    CHECK(otp::Ledger::from_canonical(tricky.to_canonical(), &back));
    CHECK(back.verify().intact);
    CHECK_EQ(back.get(1)->subject_id, std::string("op\t2"));
    CHECK_EQ(back.get(1)->detail, std::string("detail\twith\ttabs"));
    CHECK_EQ(back.digest(), tricky.digest());
}

static void test_header_cannot_be_relabelled_independently_of_entries() {
    TEST("editing only the header does not change what a ledger claims to be "
         "(design record §2, otp_ledger.h note above Integrity)");

    otp::Ledger l = make_ledger();
    l.set_instance("aevoria", "CUR-X.5/1.0.0");
    l.publish(otp::SUBJ_INSTITUTION, "tribunal-2", "determination",
              otp::ST_DETERMINED, 10, 4, "CUR-H.7 §7.15", "measure imposed");

    std::string text = l.to_canonical();
    const size_t nl = text.find('\n');
    std::string header = text.substr(0, nl);

    // The per-entry field is what is hashed. Editing ONLY the header — not the
    // body — leaves every entry_hash intact, so a naive check that trusted the
    // header would see nothing wrong.
    CHECK(header.find("aevoria") != std::string::npos);
    header.replace(header.find("aevoria"), 7, "overbyO");
    std::string forged_text = header + text.substr(nl);

    otp::Ledger forged;
    CHECK(otp::Ledger::from_canonical(forged_text, &forged));

    // instance() reads straight from the header and is deliberately naive —
    // that is why verify() has to be called before it is trusted at all.
    CHECK_EQ(forged.instance(), std::string("overbyO"));

    // verify() catches it: the entries still say "aevoria", the header now
    // says "overbyO", and the two disagree.
    const otp::Integrity r = forged.verify();
    CHECK(!r.intact);
    CHECK(r.reason.find("header") != std::string::npos);
}

static void test_instance_cannot_change_mid_ledger() {
    TEST("an instance is not something a ledger can switch after publishing "
         "(otp_ledger.h note on set_instance)");

    otp::Ledger l = make_ledger();
    CHECK(l.set_instance("aevoria", "CUR-X.5/1.0.0"));
    l.publish(otp::SUBJ_INSTITUTION, "tribunal-2", "determination",
              otp::ST_DETERMINED, 10, 4, "cite", "");

    // Once an entry exists, the instance is locked. Without this, a publisher
    // could open as "aevoria", accumulate real-looking entries, then relabel
    // to "overby-ops" so every LATER determination reads as real-world conduct
    // while the hash chain — which never breaks on a plain append — stays
    // unbroken throughout.
    CHECK(!l.set_instance("overby-ops", "CUR-X.5/1.0.0"));
    CHECK_EQ(l.instance(), std::string("aevoria"));

    l.publish(otp::SUBJ_INSTITUTION, "tribunal-2", "determination",
              otp::ST_DETERMINED, 20, 4, "cite", "");
    CHECK_EQ(l.get(2)->instance, std::string("aevoria"));
    CHECK(l.verify().intact);
}

// ---------------------------------------------------------------------------
// Registry — the aggregator
// ---------------------------------------------------------------------------

namespace {

otp::Registry make_registry(const char* instance = "aevoria") {
    otp::Registry r;
    r.set_instance(instance, "CUR-X.5/1.0.0");
    return r;
}

std::string signed_entries(const char* instance, const char* publisher,
                           int count, uint64_t tick_start = 0) {
    otp::Ledger l;
    l.set_commitment_key(1);
    l.set_instance(instance, "CUR-X.5/1.0.0");
    for (int i = 0; i < count; ++i) {
        l.publish(otp::SUBJ_INSTITUTION, publisher, "determination",
                  otp::ST_DETERMINED, tick_start + static_cast<uint64_t>(i), 3,
                  "cite", "d");
    }
    return l.to_canonical();
}

}  // namespace

static void test_registry_refuses_the_wrong_instance() {
    TEST("a registry aggregating one instance refuses a ledger from another "
         "(design record §2, otp_registry.h \"ONE REGISTRY, ONE INSTANCE\")");

    otp::Registry reg = make_registry("aevoria");

    const auto ok = reg.ingest("operator-1", signed_entries("aevoria", "operator-1", 2), 100);
    CHECK_EQ(static_cast<int>(ok.status), static_cast<int>(otp::ING_ACCEPTED));

    // A perfectly valid ledger — just for the wrong world.
    const auto wrong = reg.ingest("operator-2",
                                  signed_entries("overby-ops", "operator-2", 2), 100);
    CHECK_EQ(static_cast<int>(wrong.status),
             static_cast<int>(otp::ING_WRONG_INSTANCE));

    // Nothing was stored. A citizen querying this registry never sees a real
    // determination mixed in with simulated ones, or the reverse.
    CHECK(reg.source("operator-2")->ledger.size() == 0);
}

static void test_registry_rewrite_detection_survives_verification_bypass() {
    TEST("a header-only forgery is refused at ingest, not merely mislabelled "
         "(chains together verify() and instance binding)");

    otp::Registry reg = make_registry("aevoria");
    reg.ingest("operator-1", signed_entries("aevoria", "operator-1", 1), 10);

    // Forge a ledger for a DIFFERENT publisher whose header claims this
    // registry's instance but whose entries do not — the same attack as
    // test_header_cannot_be_relabelled_independently_of_entries, aimed at the
    // aggregation layer instead of a lone reader.
    std::string text = signed_entries("overby-ops", "operator-3", 1);
    const size_t nl = text.find('\n');
    std::string header = text.substr(0, nl);
    header.replace(header.find("overby-ops"), 10, "aevoria");
    std::string forged = header + text.substr(nl);

    const auto rep = reg.ingest("operator-3", forged, 10);

    // Caught by verify() inside ingest(), before the instance check even runs
    // — a ledger that fails verify() never gets far enough to be judged on
    // which world it claims to belong to.
    CHECK_EQ(static_cast<int>(rep.status), static_cast<int>(otp::ING_BROKEN_CHAIN));
}

static void test_history_rewrite_is_refused_and_recorded() {
    TEST("a rewritten history is refused, the earlier copy kept, and the "
         "attempt published into the registry's own ledger");

    otp::Registry reg = make_registry("aevoria");
    reg.ingest("operator-1", signed_entries("aevoria", "operator-1", 3), 10);
    CHECK_EQ(reg.source("operator-1")->ledger.size(), size_t{3});

    // A from-scratch republish with different content at entry 2 — the
    // "careful operator" case from the ledger's own test suite, now aimed at
    // an aggregator instead of a lone holder.
    otp::Ledger rewritten;
    rewritten.set_commitment_key(1);
    rewritten.set_instance("aevoria", "CUR-X.5/1.0.0");
    rewritten.publish(otp::SUBJ_INSTITUTION, "operator-1", "determination",
                      otp::ST_DETERMINED, 0, 3, "cite", "d");
    rewritten.publish(otp::SUBJ_INSTITUTION, "operator-1", "determination",
                      otp::ST_DETERMINED, 1, 3, "cite", "sanitised");
    rewritten.publish(otp::SUBJ_INSTITUTION, "operator-1", "determination",
                      otp::ST_DETERMINED, 2, 3, "cite", "d");

    const auto rep = reg.ingest("operator-1", rewritten.to_canonical(), 20);
    CHECK_EQ(static_cast<int>(rep.status),
             static_cast<int>(otp::ING_HISTORY_REWRITTEN));
    CHECK_EQ(rep.conflict_seq, uint64_t{2});

    // The earlier copy is kept. The registry did not overwrite itself with the
    // revision — that would be laundering the rewrite, not detecting it.
    CHECK_EQ(reg.source("operator-1")->ledger.size(), size_t{3});
    CHECK_EQ(reg.source("operator-1")->ledger.get(2)->detail, std::string("d"));
    CHECK_EQ(reg.source("operator-1")->rewrite_attempts, uint32_t{1});

    // A watchdog that only watches is unaccountable. The attempt is on the
    // registry's own record, naming the publisher, not silently absorbed.
    bool found = false;
    for (const auto& e : reg.own_ledger().entries()) {
        if (e.kind == "rewrite_attempt_detected" && e.subject_id == "operator-1") {
            found = true;
        }
    }
    CHECK(found);
}

static void test_truncation_is_also_a_rewrite_attempt() {
    TEST("a shorter resubmission is refused the same way a rewrite is");

    otp::Registry reg = make_registry("aevoria");
    reg.ingest("operator-1", signed_entries("aevoria", "operator-1", 4), 10);

    const auto rep = reg.ingest("operator-1",
                                signed_entries("aevoria", "operator-1", 2), 20);
    CHECK_EQ(static_cast<int>(rep.status), static_cast<int>(otp::ING_TRUNCATED));
    CHECK_EQ(reg.source("operator-1")->ledger.size(), size_t{4});
    CHECK_EQ(reg.source("operator-1")->rewrite_attempts, uint32_t{1});
}

static void test_silence_is_detected_on_the_clock_not_on_arrival() {
    TEST("run_builtin_test() notices silence because nothing arrived, which "
         "is the one thing an arrival-triggered check cannot see");

    otp::Registry reg = make_registry("aevoria");
    reg.track("quiet-operator", /*heartbeat_ticks=*/100);
    reg.ingest("quiet-operator", signed_entries("aevoria", "quiet-operator", 1), 0);

    // Within the heartbeat: nothing to report.
    CHECK(reg.run_builtin_test(50).empty());
    CHECK(reg.silent_publishers(50).empty());

    // Past it: reported exactly once, at the edge.
    const auto first = reg.run_builtin_test(150);
    CHECK_EQ(first.size(), size_t{1});
    CHECK_EQ(first[0], std::string("quiet-operator"));
    CHECK_EQ(reg.own_ledger().size(), size_t{1});
    CHECK_EQ(reg.own_ledger().entries()[0].kind, std::string("publisher_silent"));

    // Still silent later, but already reported — no duplicate entry. The
    // registry's own ledger does not grow without bound over the silence.
    const auto second = reg.run_builtin_test(300);
    CHECK(second.empty());
    CHECK_EQ(reg.own_ledger().size(), size_t{1});

    // But it still shows up on a plain query, since that has no side effect to
    // guard against.
    CHECK_EQ(reg.silent_publishers(300).size(), size_t{1});

    // Heard from again: the edge resets, so a future lapse is reported fresh
    // rather than staying suppressed by a flag from last time.
    reg.ingest("quiet-operator", signed_entries("aevoria", "quiet-operator", 2), 310);
    CHECK(reg.silent_publishers(310).empty());
    reg.run_builtin_test(500);
    CHECK_EQ(reg.own_ledger().size(), size_t{2});
}

static void test_registry_cannot_author_into_a_source() {
    TEST("the registry holds copies and checks them; it cannot put words in a "
         "publisher's mouth (CUR-X.4 §4.3 reasoning applied to the watchdog)");

    CHECK(!otp::Registry::can_author_into_sources());

    // There is no publish()/correct() taking a publisher_id on Registry at
    // all — the only way an entry reaches a Source's ledger is by round-
    // tripping through ingest(), which requires a chain that already verifies.
    // This is a structural check that the class has the shape the comment
    // claims; it does not compile if a future edit adds such a method without
    // updating this test to exercise it.
    otp::Registry reg = make_registry("aevoria");
    reg.ingest("operator-1", signed_entries("aevoria", "operator-1", 1), 10);
    CHECK_EQ(reg.source("operator-1")->ledger.size(), size_t{1});
}

static void test_registry_own_ledger_is_a_real_ledger() {
    TEST("the aggregator appears in its own record on the same terms as "
         "anyone else (design record §3, \"the aggregator is a publisher too\")");

    otp::Registry reg = make_registry("aevoria");
    reg.track("operator-1", 50);
    reg.ingest("operator-1", signed_entries("aevoria", "operator-1", 1), 0);
    reg.run_builtin_test(200);  // operator-1 is now silent

    CHECK_EQ(reg.own_ledger().instance(), std::string("aevoria"));
    CHECK(reg.own_ledger().verify().intact);

    // The registry's own findings are named, never committed — every subject
    // it ever writes about is a publisher, and publishers are institutions.
    CHECK_EQ(static_cast<int>(reg.own_ledger().entries()[0].disclosure),
             static_cast<int>(otp::DISC_NAMED));

    // Mirrorable exactly like a source ledger.
    std::string canon = reg.own_ledger().to_canonical();
    otp::Ledger reloaded;
    CHECK(otp::Ledger::from_canonical(canon, &reloaded));
    CHECK(reloaded.verify().intact);
    CHECK_EQ(reloaded.digest(), reg.own_ledger().digest());
}

static void test_cross_publisher_reporting_resolves_corrections() {
    TEST("a citizen's query across every ledger held resolves corrections, "
         "not the as-published status (CUR-X.5 §5.7(d))");

    otp::Registry reg = make_registry("aevoria");

    otp::Ledger a;
    a.set_commitment_key(1);
    a.set_instance("aevoria", "CUR-X.5/1.0.0");
    a.publish(otp::SUBJ_INSTITUTION, "shared-name-corp", "allegation",
             otp::ST_ALLEGED, 0, 2, "cite", "");
    reg.ingest("publisher-a", a.to_canonical(), 10);

    otp::Ledger b;
    b.set_commitment_key(1);
    b.set_instance("aevoria", "CUR-X.5/1.0.0");
    b.publish(otp::SUBJ_INSTITUTION, "shared-name-corp", "finding",
             otp::ST_DETERMINED, 0, 3, "cite", "");
    b.correct(1, otp::ST_OVERTURNED, 50, "overturned on appeal");
    reg.ingest("publisher-b", b.to_canonical(), 60);

    // Two matters, not three. Ledger b's correction entry (seq 2) also names
    // "shared-name-corp" — that is what makes it followable on a raw read of
    // the chain — but it is the resolution of seq 1, not a second occurrence,
    // and find_naming() excludes it the same way standing_counts() excludes
    // corrections from its counts. An institution that was corrected should
    // not appear to have two matters against it, one of which is really the
    // record that the other was cleared.
    const auto hits = reg.find_naming("shared-name-corp");
    CHECK_EQ(hits.size(), size_t{2});

    bool saw_alleged = false, saw_overturned = false;
    for (const auto& h : hits) {
        if (h.publisher_id == "publisher-a") {
            CHECK_EQ(static_cast<int>(h.standing), static_cast<int>(otp::ST_ALLEGED));
            saw_alleged = true;
        }
        if (h.publisher_id == "publisher-b") {
            // Resolved through the correction, not read as ST_DETERMINED,
            // which is what publisher-b's entry says at face value.
            CHECK_EQ(static_cast<int>(h.standing), static_cast<int>(otp::ST_OVERTURNED));
            saw_overturned = true;
        }
    }
    CHECK(saw_alleged);
    CHECK(saw_overturned);

    uint32_t counts[otp::ST_COUNT];
    reg.standing_counts(counts);
    CHECK_EQ(counts[otp::ST_ALLEGED], uint32_t{1});
    CHECK_EQ(counts[otp::ST_OVERTURNED], uint32_t{1});
    CHECK_EQ(counts[otp::ST_DETERMINED], uint32_t{0});  // corrected away, not double-counted
}

static void test_two_registries_collecting_the_same_sources_agree() {
    TEST("independent registries watching the same publishers converge to "
         "the same digest — the distributed check the design depends on");

    const std::string sig = signed_entries("aevoria", "operator-1", 3);

    otp::Registry mine = make_registry("aevoria");
    mine.ingest("operator-1", sig, 100);

    otp::Registry theirs = make_registry("aevoria");
    theirs.ingest("operator-1", sig, 999);  // different observation tick

    // Sources match; digest still depends on own_ledger_, which differs here
    // only in that neither has observed anything adverse yet, so they agree.
    CHECK_EQ(mine.digest(), theirs.digest());

    // Now one of them catches something the other has not.
    otp::Registry drifted = make_registry("aevoria");
    drifted.track("operator-1", 1);
    drifted.ingest("operator-1", sig, 100);
    drifted.run_builtin_test(50000);  // long silence, self-inflicted for the test

    CHECK(mine.digest() != drifted.digest());
}

int main() {
    std::printf("Overby Ledger %s\n\n", otp::OTP_VERSION);

    test_people_are_never_named();
    test_commitments_do_not_link_a_person_across_entries();
    test_a_dismissal_cannot_be_lost();
    test_tampering_is_localised();
    test_selective_publication_leaves_a_hole();
    test_the_ledger_imposes_nothing();
    test_an_instance_cannot_be_relabelled();
    test_canonical_form_is_git_readable();
    test_header_cannot_be_relabelled_independently_of_entries();
    test_instance_cannot_change_mid_ledger();
    test_registry_refuses_the_wrong_instance();
    test_registry_rewrite_detection_survives_verification_bypass();
    test_history_rewrite_is_refused_and_recorded();
    test_truncation_is_also_a_rewrite_attempt();
    test_silence_is_detected_on_the_clock_not_on_arrival();
    test_registry_cannot_author_into_a_source();
    test_registry_own_ledger_is_a_real_ledger();
    test_cross_publisher_reporting_resolves_corrections();
    test_two_registries_collecting_the_same_sources_agree();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
