// otp_token_tests.cpp — the ledger coordinate.
//
// Same style as otp_tests.cpp, and for the same reason: anyone can show that a
// coordinate for an entry that exists checks out. What has to be shown is that
// the ways of presenting one that should NOT check out do not.

#include <cstdio>
#include <string>

#include "otp/otp_ledger.h"
#include "otp/otp_token.h"

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

otp::Ledger make_ledger(const char* instance = "overby-ops") {
    otp::Ledger l;
    l.set_commitment_key(0xA3F1C09E5511BB27ull);
    l.set_instance(instance, "OTB-1/1.0");
    return l;
}

void publish_three(otp::Ledger& l) {
    l.publish(otp::SUBJ_INSTITUTION, "vendor-a", "procurement", otp::ST_DETERMINED, 10, 0,
              "OTB-1", "first");
    l.publish(otp::SUBJ_INSTITUTION, "vendor-b", "procurement", otp::ST_DETERMINED, 20, 0,
              "OTB-1", "second");
    l.publish(otp::SUBJ_INSTITUTION, "vendor-c", "procurement", otp::ST_DETERMINED, 30, 0,
              "OTB-1", "third");
}

// A copy of `l` with one substring of its canonical text swapped.
otp::Ledger tampered(const otp::Ledger& l, const std::string& from, const std::string& to) {
    std::string text = l.to_canonical();
    const size_t at = text.find(from);
    CHECK(at != std::string::npos);
    if (at != std::string::npos) text.replace(at, from.size(), to);
    otp::Ledger out;
    CHECK(otp::Ledger::from_canonical(text, &out));
    return out;
}

bool refuses(const std::string& text) {
    otp::Token t;
    std::string why;
    return !otp::parse_token(text, &t, &why) && !why.empty();
}

}  // namespace

static void test_a_coordinate_is_the_entrys_position_and_hash() {
    TEST("a coordinate is the entry's sequence number and hash, written one way");

    otp::Ledger l = make_ledger();
    publish_three(l);
    const otp::Entry* e = l.get(2);
    CHECK(e != nullptr);
    if (e == nullptr) return;

    const otp::Token t = otp::token_for(*e);
    CHECK_EQ(t.seq, uint64_t{2});
    CHECK_EQ(t.entry_hash, e->entry_hash);
    CHECK_EQ(t.version, otp::TOKEN_VERSION);

    const std::string text = otp::to_string(t);
    CHECK_EQ(text.size(), size_t(8 + 16));  // "otp:1:2:" then 16 hex digits
    CHECK_EQ(text.compare(0, 8, "otp:1:2:"), 0);

    // Fixed-width lower-case hex, including a hash with leading zeros.
    otp::Token padded{1, 7, 0x00000000000000ABull};
    CHECK_EQ(otp::to_string(padded), std::string("otp:1:7:00000000000000ab"));
    otp::Token big{1, 18446744073709551615ull, 0xFFFFFFFFFFFFFFFFull};
    CHECK_EQ(otp::to_string(big), std::string("otp:1:18446744073709551615:ffffffffffffffff"));
}

static void test_round_trip() {
    TEST("what to_string writes, parse_token reads back exactly");

    for (uint64_t seq : {uint64_t{1}, uint64_t{9}, uint64_t{10}, uint64_t{8412},
                         uint64_t{4294967296ull}, uint64_t{18446744073709551615ull}}) {
        for (uint64_t hash : {uint64_t{0}, uint64_t{1}, uint64_t{0x9f3c1a4e77b20d51ull},
                              uint64_t{0xFFFFFFFFFFFFFFFFull}}) {
            otp::Token in{1, seq, hash};
            otp::Token out;
            CHECK(otp::parse_token(otp::to_string(in), &out));
            CHECK(out.seq == seq && out.entry_hash == hash && out.version == 1);
        }
    }

    // The example in docs/LEDGER.md §0.
    otp::Token doc;
    CHECK(otp::parse_token("otp:1:8412:9f3c1a4e77b20d51", &doc));
    CHECK_EQ(doc.seq, uint64_t{8412});
    CHECK_EQ(doc.entry_hash, uint64_t{0x9f3c1a4e77b20d51ull});
}

static void test_anything_else_is_refused() {
    TEST("a string that is not exactly a coordinate is refused, never guessed at");

    CHECK(refuses(""));
    CHECK(refuses("otp"));
    CHECK(refuses("otp:"));
    CHECK(refuses("otp:1"));
    CHECK(refuses("otp:1:5"));
    CHECK(refuses("otp:1:5:"));
    CHECK(refuses("otp:1:5:9f3c1a4e77b20d5"));        // 15 digits
    CHECK(refuses("otp:1:5:9f3c1a4e77b20d511"));      // 17 digits
    CHECK(refuses("otp:1:5:9F3C1A4E77B20D51"));       // upper case
    CHECK(refuses("otp:1:5:9f3c1a4e77b20d5g"));       // not hex
    CHECK(refuses("otp:1:5:0x9f3c1a4e77b20d5"));      // a prefix is not a digit
    CHECK(refuses("OTP:1:5:9f3c1a4e77b20d51"));       // the prefix is lower case
    CHECK(refuses(" otp:1:5:9f3c1a4e77b20d51"));
    CHECK(refuses("otp:1:5:9f3c1a4e77b20d51 "));
    CHECK(refuses("otp:1:5:9f3c1a4e77b20d51\n"));     // a stray newline
    CHECK(refuses("otp:1:5:9f3c1a4e77b20d51:extra"));
    CHECK(refuses("otp:1:05:9f3c1a4e77b20d51"));      // leading zero: one number, one spelling
    CHECK(refuses("otp:1:0:9f3c1a4e77b20d51"));       // sequence numbers start at 1
    CHECK(refuses("otp:1:+5:9f3c1a4e77b20d51"));
    CHECK(refuses("otp:1:-5:9f3c1a4e77b20d51"));
    CHECK(refuses("otp:1::9f3c1a4e77b20d51"));
    CHECK(refuses("otp:1:18446744073709551616:9f3c1a4e77b20d51"));  // 2^64: would wrap
    CHECK(refuses("otp:1:99999999999999999999:9f3c1a4e77b20d51"));
    CHECK(refuses("otp:2:5:9f3c1a4e77b20d51"));       // a version this library does not know
    CHECK(refuses("otp:0:5:9f3c1a4e77b20d51"));
    CHECK(refuses("otp:01:5:9f3c1a4e77b20d51"));
    CHECK(refuses("otp::5:9f3c1a4e77b20d51"));
    CHECK(refuses("0xabc123debriscredit"));           // a legacy chain token
    CHECK(refuses("0xinv20250001"));

    CHECK(otp::looks_like_token("otp:anything at all"));  // shaped like one, not necessarily one
    CHECK(!otp::looks_like_token("0xabc123debriscredit"));
    CHECK(!otp::looks_like_token(""));

    // A failed parse leaves the caller's token untouched.
    otp::Token untouched{1, 77, 88};
    CHECK(!otp::parse_token("otp:1:5:zzzz", &untouched));
    CHECK(untouched.seq == 77 && untouched.entry_hash == 88);
}

static void test_a_good_coordinate_checks_out() {
    TEST("a coordinate for an entry in an intact ledger is valid");

    otp::Ledger l = make_ledger();
    publish_three(l);
    for (uint64_t seq = 1; seq <= 3; ++seq) {
        const otp::TokenCheck c = otp::check_token(l, otp::token_for(*l.get(seq)));
        CHECK(c.valid());
        CHECK_EQ(c.status, otp::TOKEN_VALID);
    }
    CHECK_EQ(std::string(otp::to_string(otp::TOKEN_VALID)), std::string("valid"));
}

static void test_a_missing_entry_is_not_the_same_as_an_invalid_one() {
    TEST("a ledger that is behind reports a missing entry, not a forged one");

    otp::Ledger full = make_ledger();
    publish_three(full);
    const otp::Token third = otp::token_for(*full.get(3));

    // A copy taken before the third entry existed.
    otp::Ledger behind = make_ledger();
    behind.publish(otp::SUBJ_INSTITUTION, "vendor-a", "procurement", otp::ST_DETERMINED, 10, 0,
                   "OTB-1", "first");
    behind.publish(otp::SUBJ_INSTITUTION, "vendor-b", "procurement", otp::ST_DETERMINED, 20, 0,
                   "OTB-1", "second");
    const otp::TokenCheck c = otp::check_token(behind, third);
    CHECK_EQ(c.status, otp::TOKEN_NO_SUCH_ENTRY);
    CHECK(!c.valid());
    CHECK(!c.reason.empty());

    otp::Ledger empty = make_ledger();
    CHECK_EQ(otp::check_token(empty, third).status, otp::TOKEN_NO_SUCH_ENTRY);
}

static void test_a_wrong_hash_is_refused() {
    TEST("a coordinate with the wrong hash, or from another ledger, does not check out");

    otp::Ledger l = make_ledger();
    publish_three(l);

    otp::Token forged = otp::token_for(*l.get(2));
    forged.entry_hash ^= 1;  // one bit
    const otp::TokenCheck c = otp::check_token(l, forged);
    CHECK_EQ(c.status, otp::TOKEN_HASH_MISMATCH);

    // The same entries published into a ledger with another instance name. The
    // instance is inside every hash, so a coordinate cannot be carried from a
    // simulated ledger to a real one, or back.
    otp::Ledger other = make_ledger("aevoria");
    publish_three(other);
    CHECK_EQ(otp::check_token(l, otp::token_for(*other.get(2))).status, otp::TOKEN_HASH_MISMATCH);
    CHECK(otp::token_for(*l.get(2)).entry_hash != otp::token_for(*other.get(2)).entry_hash);

    // A coordinate for one entry presented as another's.
    otp::Token shifted = otp::token_for(*l.get(1));
    shifted.seq = 2;
    CHECK_EQ(otp::check_token(l, shifted).status, otp::TOKEN_HASH_MISMATCH);
}

static void test_an_altered_entry_fails_it_and_what_follows() {
    TEST("an entry altered after publication breaks its coordinate and the ones after it");

    otp::Ledger l = make_ledger();
    publish_three(l);
    const otp::Token t1 = otp::token_for(*l.get(1));
    const otp::Token t2 = otp::token_for(*l.get(2));
    const otp::Token t3 = otp::token_for(*l.get(3));

    // Entry 2's text is rewritten; its stored hash is left as it was.
    const otp::Ledger edited = tampered(l, "second", "SECOND");
    CHECK(!edited.verify().intact);

    // What precedes the alteration is still vouched for.
    CHECK_EQ(otp::check_token(edited, t1).status, otp::TOKEN_VALID);

    // The altered entry, and everything built on it, is not.
    CHECK_EQ(otp::check_token(edited, t2).status, otp::TOKEN_CHAIN_BROKEN);
    CHECK_EQ(otp::check_token(edited, t3).status, otp::TOKEN_CHAIN_BROKEN);
    CHECK(!otp::check_token(edited, t2).reason.empty());

    // Alteration at the tail leaves every earlier coordinate valid.
    const otp::Ledger tail = tampered(l, "third", "THIRD");
    CHECK_EQ(otp::check_token(tail, t1).status, otp::TOKEN_VALID);
    CHECK_EQ(otp::check_token(tail, t2).status, otp::TOKEN_VALID);
    CHECK_EQ(otp::check_token(tail, t3).status, otp::TOKEN_CHAIN_BROKEN);
}

static void test_a_removed_or_reordered_entry_is_caught() {
    TEST("a ledger with an entry removed or moved does not vouch for what follows");

    otp::Ledger l = make_ledger();
    publish_three(l);
    const otp::Token t1 = otp::token_for(*l.get(1));
    const otp::Token t3 = otp::token_for(*l.get(3));

    // Drop entry 2's line from the canonical text.
    const std::string text = l.to_canonical();
    std::string pruned;
    size_t start = 0, line = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size() - 1;
        if (line != 2) pruned += text.substr(start, end - start + 1);  // line 0 is the header
        start = end + 1;
        ++line;
    }
    otp::Ledger holed;
    CHECK(otp::Ledger::from_canonical(pruned, &holed));
    CHECK(!holed.verify().intact);
    CHECK_EQ(otp::check_token(holed, t1).status, otp::TOKEN_VALID);
    // Entry 3 is still there with its own hash, and the ledger no longer
    // answers for it: its predecessor is missing.
    CHECK_EQ(otp::check_token(holed, t3).status, otp::TOKEN_CHAIN_BROKEN);
}

int main() {
    std::printf("Overby Ledger coordinate %s\n\n", otp::OTP_VERSION);

    test_a_coordinate_is_the_entrys_position_and_hash();
    test_round_trip();
    test_anything_else_is_refused();
    test_a_good_coordinate_checks_out();
    test_a_missing_entry_is_not_the_same_as_an_invalid_one();
    test_a_wrong_hash_is_refused();
    test_an_altered_entry_fails_it_and_what_follows();
    test_a_removed_or_reordered_entry_is_caught();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
