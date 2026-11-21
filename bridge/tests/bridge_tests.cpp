// bridge_tests.cpp — OTF-1 / OTB-1 into the ledger, and back out.
//
// Same posture as the rest of this repository's tests: anyone can show that a
// record published and then checked comes back verified. What has to be shown
// is that the ways of presenting a record that should NOT verify do not -- an
// altered packet, a token lifted from another, a ledger that was rewritten --
// and that the asymmetry the ledger exists to protect survives the trip: a
// person committed in the ledger is not named by the packet that points at it.

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "otp/otp_bridge.h"
#include "sha256.hpp"

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

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string otf1_example(const char* name) {
    return read_file(std::string(OTP_BRIDGE_DIR) + "/deps/telemetry-protocol/examples/" + name);
}
std::string otb1_example(const char* name) {
    return read_file(std::string(OTP_BRIDGE_DIR) + "/deps/transparency-protocol/examples/" + name);
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string replaced(std::string text, const std::string& from, const std::string& to) {
    const size_t at = text.find(from);
    CHECK(at != std::string::npos);
    if (at != std::string::npos) text.replace(at, from.size(), to);
    return text;
}

otp::Ledger make_ledger(const char* instance = "overby-ops") {
    otp::Ledger l;
    l.set_commitment_key(0xA3F1C09E5511BB27ull);
    l.set_instance(instance, "OTF-1+OTB-1/1.0");
    return l;
}

// An OTF-1 event with no token in it.
std::string refinery() { return otf1_example("mining-refinery-report.json"); }
std::string capture() { return otf1_example("assigned-capture-event.json"); }

otp::PublishOptions replace_token() {
    otp::PublishOptions o;
    o.replace_existing_token = true;
    return o;
}
otp::Otb1Options otb1_replace(otp::VendorKind vendor) {
    otp::Otb1Options o;
    o.replace_existing_token = true;
    o.vendor = vendor;
    return o;
}

bool valid_otb1(const std::string& text) {
    otb1::Record record;
    return otb1::decode(text, record);
}

struct ShaVector { size_t length; const char* hex; };
const ShaVector kShaVectors[] = {
    {0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
    {1, "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb"},
    {3, "9834876dcfb05cb167a5c24953eba58c4ac89b1adf57f28f2f9d09af107ee8f0"},
    {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
    {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
    {57, "f13b2d724659eb3bf47f2dd6af1accc87b81f09f59f2b75e5c0bed6589dfe8c6"},
    {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
    {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
    {65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
    {111, "6374f73208854473827f6f6a3f43b1f53eaa3b82c21c1a6d69a2110b2a79baad"},
    {112, "f54353008a2553262ecdc4a34749563ba0950e8b0fc8652780b0a614b99683c1"},
    {113, "ba02731ae695aae5cd49b49d84330b63995733eb22102aca755f0179b1e0e20f"},
    {119, "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb"},
    {120, "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c"},
    {121, "e9615320128cc7a3d6078e9af05603188e5ccbf0d07d8b735d3df5e8e0c1281f"},
    {127, "c57e9278af78fa3cab38667bef4ce29d783787a2f731d4e12200270f0c32320a"},
    {128, "6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e"},
    {129, "c12cb024a2e5551cca0e08fce8f1c5e314555cc3fef6329ee994a3db752166ae"},
    {191, "311834ddbafe20677dddbf9f4beb2b5b0b9e6ee97dddb70fce4e8f62c1b7518d"},
    {192, "7cee24628d290c16183532716cc5a8a889bc951b4b0a1507c32b8e29cee01052"},
    {193, "33f93a9879ef18f9779150b2dbace6f8cc17b29e1af6be4e1048fc647489f1c2"},
    {1000, "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3"},
};

}  // namespace

static void test_sha256_matches_the_standard() {
    TEST("SHA-256 matches FIPS 180-4, including at every padding boundary");

    using otp::detail::sha256_hex;
    CHECK_EQ(sha256_hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(sha256_hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
             std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    CHECK_EQ(sha256_hex(std::string(1000000, 'a')),
             std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));

    // Where the length field no longer fits and spills into a second block:
    // 55 bytes fits, 56 does not; the same again at 119/120 and 183/184 + 8.
    for (const ShaVector& v : kShaVectors) {
        const std::string got = sha256_hex(std::string(v.length, 'a'));
        if (got != v.hex) std::printf("        length %zu: got %s\n", v.length, got.c_str());
        CHECK_EQ(got, std::string(v.hex));
    }
}

static void test_canonical_form_matches_independent_code() {
    TEST("the canonical form and digest are byte-for-byte what tools/golden.py produces");

    const std::string golden = read_file(std::string(OTP_BRIDGE_DIR) + "/tests/golden.txt");
    CHECK(!golden.empty());

    size_t checked = 0, start = 0;
    while (start < golden.size()) {
        size_t end = golden.find('\n', start);
        if (end == std::string::npos) end = golden.size();
        const std::string line = golden.substr(start, end - start);
        start = end + 1;
        if (line.empty()) continue;

        std::vector<std::string> f;
        size_t from = 0;
        for (int i = 0; i < 4; ++i) {
            const size_t tab = line.find('\t', from);
            CHECK(tab != std::string::npos);
            if (tab == std::string::npos) break;
            f.push_back(line.substr(from, tab - from));
            from = tab + 1;
        }
        if (f.size() != 4) continue;
        const std::string canonical = line.substr(from);
        const std::string& schema = f[0];
        const std::string text = read_file(std::string(OTP_BRIDGE_DIR) + "/deps/" + f[1]);

        std::string mine;
        if (schema == "otf1") {
            otf1::Packet packet;
            CHECK(otf1::decode(text, packet));
            const otf1::EventPacket* event = std::get_if<otf1::EventPacket>(&packet);
            CHECK(event != nullptr);
            if (event == nullptr) continue;
            mine = otp::canonical_otf1(*event);
        } else {
            otb1::Record record;
            CHECK(otb1::decode(text, record));
            if (f[2] == "withheld") record.financials.vendor = otp::WITHHELD_VENDOR;
            mine = otp::canonical_otb1(record);
        }
        if (mine != canonical) {
            std::printf("        %s (%s)\n        want %s\n        got  %s\n", f[1].c_str(),
                        f[2].c_str(), canonical.c_str(), mine.c_str());
        }
        CHECK_EQ(mine, canonical);
        CHECK_EQ(otp::detail::sha256_hex(mine), f[3]);
        ++checked;
    }
    CHECK(checked >= 9);
}

static void test_an_event_is_recorded_and_stamped() {
    TEST("an OTF-1 event is recorded, stamped with its coordinate, and verifies");

    otp::Ledger ledger = make_ledger();
    otp::Bridge bridge(ledger);
    const otp::Published p = bridge.publish_otf1(refinery());
    CHECK(p.ok);
    if (!p.ok) { std::printf("        %s\n", p.error.c_str()); return; }

    CHECK(!p.already_published);
    CHECK_EQ(p.seq, uint64_t{1});
    CHECK_EQ(ledger.size(), size_t{1});
    CHECK(ledger.verify().intact);

    // The stamped packet is itself valid OTF-1 and carries the coordinate.
    otf1::Packet packet;
    std::string why;
    CHECK(otf1::decode(p.stamped, packet, &why));
    const otf1::EventPacket* stamped = std::get_if<otf1::EventPacket>(&packet);
    CHECK(stamped != nullptr);
    if (stamped == nullptr) return;
    CHECK(stamped->proofOfCleanupToken && *stamped->proofOfCleanupToken == otp::to_string(p.token));
    CHECK_EQ(otp::to_string(p.token).compare(0, 6, "otp:1:"), 0);

    // What the entry says.
    const otp::Entry* e = ledger.get(1);
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK_EQ(e->citation, std::string("OTF-1"));
    CHECK_EQ(e->kind, std::string("otf1.processed"));
    CHECK_EQ(e->subject_id, std::string("R7"));
    CHECK(e->disclosure == otp::DISC_NAMED);  // a pod is a machine, not a person
    CHECK(e->standing == otp::ST_DETERMINED);
    CHECK_EQ(e->tick, uint64_t{1740906720000});  // 2025-03-02T09:12:00Z in ms
    CHECK_EQ(e->detail.compare(0, 7, "sha256:"), 0);
    CHECK_EQ(e->detail.substr(7, 64), p.digest);
    CHECK(contains(e->detail, "processed by pod R7, mission nea-2025-alpha"));
    CHECK_EQ(otp::token_for(*e).entry_hash, p.token.entry_hash);

    // And it verifies, against the stamped text.
    const otp::Verification v = bridge.verify_otf1(p.stamped);
    CHECK(v.verified());
    CHECK_EQ(v.seq, uint64_t{1});

    // The digest does not move when the token is added: that is what breaks
    // the circle between the packet, the token and the entry.
    CHECK_EQ(otp::detail::sha256_hex(otp::canonical_otf1(*stamped)), p.digest);

    // A task id, when the event completes an assignment, reaches the summary.
    otp::Published q = bridge.publish_otf1(capture());
    CHECK(q.ok);
    const otp::Entry* second = ledger.get(2);
    CHECK(second != nullptr);
    if (second == nullptr) return;
    CHECK(contains(second->detail, ", task T-0042"));
    CHECK_EQ(second->kind, std::string("otf1.captured"));
}

static void test_the_same_record_is_one_entry() {
    TEST("publishing a record again, however it is spelled, reuses its entry");

    otp::Ledger ledger = make_ledger();
    otp::Bridge bridge(ledger);
    const otp::Published first = bridge.publish_otf1(refinery());
    CHECK(first.ok);

    // The identical text.
    const otp::Published again = bridge.publish_otf1(refinery());
    CHECK(again.ok && again.already_published);
    CHECK_EQ(again.seq, first.seq);
    CHECK_EQ(again.stamped, first.stamped);

    // The already-stamped packet.
    const otp::Published stamped = bridge.publish_otf1(first.stamped);
    CHECK(stamped.ok && stamped.already_published);
    CHECK_EQ(stamped.stamped, first.stamped);

    // The same instant written with an offset instead of Z.
    const otp::Published offset = bridge.publish_otf1(
        replaced(refinery(), "2025-03-02T09:12:00Z", "2025-03-02T10:12:00+01:00"));
    CHECK(offset.ok && offset.already_published);
    CHECK_EQ(offset.digest, first.digest);

    // The same record with its members in another order and no whitespace.
    otf1::Packet packet;
    CHECK(otf1::decode(refinery(), packet));
    const std::string reordered =
        R"({"certifications":{"ODCI_contribution":0,"SMESSC":true},"action":"processed",)"
        R"("outcome":{"status":"stored","repurposedAs":"aggregate","processedKg":1200},)"
        R"("target":{"objectType":"regolith"},"location":{"zone":"NEA","altitude_km":0},)"
        R"("mission":{"type":"refinery","podId":"R7","id":"nea-2025-alpha"},)"
        R"("timestamp":"2025-03-02T09:12:00Z","version":"OTF-1"})";
    const otp::Published shuffled = bridge.publish_otf1(reordered);
    CHECK(shuffled.ok && shuffled.already_published);

    CHECK_EQ(ledger.size(), size_t{1});

    // A genuinely different record is a different entry.
    const otp::Published other = bridge.publish_otf1(replaced(refinery(), "1200", "1201"));
    CHECK(other.ok && !other.already_published);
    CHECK_EQ(ledger.size(), size_t{2});
    CHECK(other.digest != first.digest);
}

static void test_a_new_bridge_finds_what_was_already_published() {
    TEST("a bridge over a ledger filled earlier, or loaded from a copy, finds its entries");

    otp::Ledger ledger = make_ledger();
    otp::Published first;
    {
        otp::Bridge bridge(ledger);
        first = bridge.publish_otf1(refinery());
        CHECK(first.ok);
    }

    otp::Bridge later(ledger);
    const otp::Published again = later.publish_otf1(refinery());
    CHECK(again.ok && again.already_published && again.seq == first.seq);

    // A mirror: someone else loads the canonical text and checks the packet.
    otp::Ledger mirror;
    CHECK(otp::Ledger::from_canonical(ledger.to_canonical(), &mirror));
    otp::Bridge third_party(mirror);
    CHECK(third_party.verify_otf1(first.stamped).verified());
    CHECK(third_party.publish_otf1(refinery()).already_published);
}

static void test_an_altered_packet_does_not_verify() {
    TEST("a stamped packet that was altered no longer verifies");

    otp::Ledger ledger = make_ledger();
    otp::Bridge bridge(ledger);
    const otp::Published p = bridge.publish_otf1(refinery());
    CHECK(p.ok);
    CHECK(bridge.verify_otf1(p.stamped).verified());

    // Inflate the mass processed. The token is untouched and still valid.
    const std::string inflated = replaced(p.stamped, "\"processedKg\":1200", "\"processedKg\":9999");
    const otp::Verification v = bridge.verify_otf1(inflated);
    CHECK_EQ(v.verdict, otp::Verdict::EntryMismatch);
    CHECK(!v.verified());

    // Every field is covered, not just the quantities.
    CHECK_EQ(bridge.verify_otf1(replaced(p.stamped, "\"podId\":\"R7\"", "\"podId\":\"R8\"")).verdict,
             otp::Verdict::EntryMismatch);
    CHECK_EQ(bridge.verify_otf1(replaced(p.stamped, "2025-03-02T09:12:00Z", "2025-03-02T09:12:01Z")).verdict,
             otp::Verdict::EntryMismatch);
    CHECK_EQ(bridge.verify_otf1(replaced(p.stamped, "\"SMESSC\":true", "\"SMESSC\":false")).verdict,
             otp::Verdict::EntryMismatch);
    CHECK_EQ(bridge.verify_otf1(replaced(p.stamped, "\"status\":\"stored\"", "\"status\":\"failed\"")).verdict,
             otp::Verdict::EntryMismatch);
}

static void test_a_token_cannot_be_lifted_to_another_packet() {
    TEST("a valid token pasted into a different packet does not verify");

    otp::Ledger ledger = make_ledger();
    otp::Bridge bridge(ledger);
    const otp::Published a = bridge.publish_otf1(refinery());
    const otp::Published b = bridge.publish_otf1(capture());
    CHECK(a.ok && b.ok);

    // The capture event, carrying the refinery event's perfectly valid token.
    otf1::Packet packet;
    CHECK(otf1::decode(b.stamped, packet));
    otf1::EventPacket lifted = std::get<otf1::EventPacket>(packet);
    lifted.proofOfCleanupToken = otp::to_string(a.token);
    const otp::Verification v = bridge.verify_otf1(otf1::encode(lifted));

    // The coordinate alone would pass: that is the gap the digest closes.
    CHECK(otp::check_token(ledger, a.token).valid());
    CHECK_EQ(v.verdict, otp::Verdict::EntryMismatch);
    CHECK_EQ(v.seq, a.seq);
}

static void test_the_ways_a_token_can_be_wrong() {
    TEST("each way a token can fail to check out is reported as what it is");

    otp::Ledger ledger = make_ledger();
    otp::Bridge bridge(ledger);
    const otp::Published p = bridge.publish_otf1(refinery());
    CHECK(p.ok);

    // The packet as it was before stamping.
    CHECK_EQ(bridge.verify_otf1(refinery()).verdict, otp::Verdict::NoToken);

    // A legacy blockchain token: not a coordinate, and not checkable here.
    CHECK_EQ(bridge.verify_otf1(otf1_example("reclaimer-capture.json")).verdict,
             otp::Verdict::ForeignToken);

    // Shaped like a coordinate and not one.
    const std::string with_token = replaced(p.stamped, otp::to_string(p.token), "otp:1:1:ZZZZ");
    CHECK_EQ(bridge.verify_otf1(with_token).verdict, otp::Verdict::MalformedToken);
    CHECK_EQ(bridge.verify_otf1(replaced(p.stamped, otp::to_string(p.token),
                                         "otp:1:01:0000000000000000")).verdict,
             otp::Verdict::MalformedToken);

    // A well-formed coordinate this ledger never issued.
    const std::string forged = replaced(p.stamped, otp::to_string(p.token),
                                        "otp:1:1:0000000000000000");
    const otp::Verification hash = bridge.verify_otf1(forged);
    CHECK_EQ(hash.verdict, otp::Verdict::TokenInvalid);
    CHECK_EQ(hash.token_status, otp::TOKEN_HASH_MISMATCH);
    const otp::Verification none = bridge.verify_otf1(replaced(
        p.stamped, otp::to_string(p.token), "otp:1:99:" + otp::to_string(p.token).substr(otp::to_string(p.token).size() - 16)));
    CHECK_EQ(none.verdict, otp::Verdict::TokenInvalid);
    CHECK_EQ(none.token_status, otp::TOKEN_NO_SUCH_ENTRY);

    // Not OTF-1 at all, and OTF-1 that is not an event.
    CHECK_EQ(bridge.verify_otf1("{ nope").verdict, otp::Verdict::NotAPacket);
    CHECK_EQ(bridge.verify_otf1(otf1_example("node-status.json")).verdict, otp::Verdict::NotPublishable);
    CHECK_EQ(bridge.verify_otf1(otf1_example("coordinator-heartbeat.json")).verdict,
             otp::Verdict::NotPublishable);

    // An OTF-1 packet put to the OTB-1 check, and the reverse.
    CHECK_EQ(bridge.verify_otb1(p.stamped).verdict, otp::Verdict::NotAPacket);
    CHECK_EQ(bridge.verify_otf1(otb1_example("land-purchase.json")).verdict, otp::Verdict::NotAPacket);

    // The same token and packet against a ledger that is merely behind.
    otp::Ledger behind = make_ledger();
    CHECK_EQ(otp::Bridge(behind).verify_otf1(p.stamped).token_status, otp::TOKEN_NO_SUCH_ENTRY);

    // ... and against a ledger with another instance name that holds the same
    // record: the instance is inside every hash.
    otp::Ledger elsewhere = make_ledger("aevoria");
    otp::Bridge other(elsewhere);
    CHECK(other.publish_otf1(refinery()).ok);
    const otp::Verification foreign = other.verify_otf1(p.stamped);
    CHECK_EQ(foreign.verdict, otp::Verdict::TokenInvalid);
    CHECK_EQ(foreign.token_status, otp::TOKEN_HASH_MISMATCH);
}

static void test_a_rewritten_ledger_stops_vouching() {
    TEST("a ledger altered after publication no longer verifies what it recorded");

    otp::Ledger ledger = make_ledger();
    otp::Bridge bridge(ledger);
    const otp::Published a = bridge.publish_otf1(refinery());
    const otp::Published b = bridge.publish_otf1(capture());
    CHECK(a.ok && b.ok);

    // Entry 1's summary text is edited in a copy; its stored hash is untouched.
    otp::Ledger edited;
    CHECK(otp::Ledger::from_canonical(
        replaced(ledger.to_canonical(), "processed by pod R7", "processed by pod X9"), &edited));
    otp::Bridge auditor(edited);
    const otp::Verification v = auditor.verify_otf1(a.stamped);
    CHECK_EQ(v.verdict, otp::Verdict::TokenInvalid);
    CHECK_EQ(v.token_status, otp::TOKEN_CHAIN_BROKEN);
    CHECK_EQ(auditor.verify_otf1(b.stamped).token_status, otp::TOKEN_CHAIN_BROKEN);
}

static void test_only_events_are_published() {
    TEST("anything that is not a valid event is refused, and nothing is recorded");

    otp::Ledger ledger = make_ledger();
    otp::Bridge bridge(ledger);

    for (const char* name : {"node-status.json", "coordinator-heartbeat.json",
                             "task-assignment.json", "assignment-ack.json",
                             "assignment-rejected.json"}) {
        const otp::Published p = bridge.publish_otf1(otf1_example(name));
        CHECK(!p.ok);
        CHECK(contains(p.error, "only OTF-1 event packets are published"));
    }

    const otp::Published bad = bridge.publish_otf1(replaced(refinery(), "\"processed\"", "\"vanished\""));
    CHECK(!bad.ok);
    CHECK(contains(bad.error, "not a valid OTF-1 packet"));
    CHECK(contains(bad.error, "action"));  // names the field

    CHECK(!bridge.publish_otf1("").ok);
    CHECK(!bridge.publish_otf1("[]").ok);

    const otp::Published old = bridge.publish_otf1(
        replaced(refinery(), "2025-03-02T09:12:00Z", "1969-12-31T23:59:59Z"));
    CHECK(!old.ok);
    CHECK(contains(old.error, "before 1970"));

    CHECK(!bridge.publish_otb1(refinery()).ok);  // an OTF-1 event is not an OTB-1 record
    CHECK_EQ(ledger.size(), size_t{0});
}

static void test_an_unidentified_ledger_is_refused() {
    TEST("a ledger that does not say which ledger it is cannot be published into");

    otp::Ledger anonymous;  // no set_instance
    otp::Bridge bridge(anonymous);
    const otp::Published p = bridge.publish_otf1(refinery());
    CHECK(!p.ok);
    CHECK(contains(p.error, "set_instance"));
    CHECK(!bridge.publish_otb1(otb1_example("land-purchase.json")).ok);
    CHECK_EQ(anonymous.size(), size_t{0});
}

static void test_an_existing_token_is_never_destroyed() {
    TEST("a token already in a packet is not overwritten unless asked");

    otp::Ledger ledger = make_ledger();
    otp::Bridge bridge(ledger);
    const std::string legacy = otf1_example("reclaimer-capture.json");  // "0xabc123debriscredit"

    const otp::Published refused = bridge.publish_otf1(legacy);
    CHECK(!refused.ok);
    CHECK(contains(refused.error, "already carries a token"));
    CHECK(contains(refused.error, "0xabc123debriscredit"));
    CHECK_EQ(ledger.size(), size_t{0});  // nothing recorded as a side effect

    const otp::Published replaced_ok = bridge.publish_otf1(legacy, replace_token());
    CHECK(replaced_ok.ok);
    CHECK(!contains(replaced_ok.stamped, "0xabc123debriscredit"));
    CHECK(contains(replaced_ok.stamped, otp::to_string(replaced_ok.token)));
    CHECK(bridge.verify_otf1(replaced_ok.stamped).verified());

    // Now that the record is in the ledger, a copy of the packet that still has
    // the old token, or someone else's coordinate, is refused rather than
    // quietly handed the right one.
    CHECK(!bridge.publish_otf1(legacy).ok);
    const std::string foreign = replaced(replaced_ok.stamped, otp::to_string(replaced_ok.token),
                                         "otp:1:7:0123456789abcdef");
    const otp::Published conflicting = bridge.publish_otf1(foreign);
    CHECK(!conflicting.ok);
    CHECK(contains(conflicting.error, "different token"));
    // ... unless asked, in which case the ledger's own coordinate wins.
    const otp::Published fixed = bridge.publish_otf1(foreign, replace_token());
    CHECK(fixed.ok && fixed.already_published);
    CHECK_EQ(fixed.stamped, replaced_ok.stamped);
    CHECK_EQ(ledger.size(), size_t{1});
}

static void test_a_legal_entity_is_named() {
    TEST("an OTB-1 vendor established to be a legal entity is named in the ledger");

    otp::Ledger ledger = make_ledger();
    otp::Bridge bridge(ledger);
    const otp::Published p =
        bridge.publish_otb1(otb1_example("land-purchase.json"), otb1_replace(otp::VendorKind::LegalEntity));
    CHECK(p.ok);
    if (!p.ok) { std::printf("        %s\n", p.error.c_str()); return; }
    CHECK(!p.vendor_withheld);

    const otp::Entry* e = ledger.get(1);
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK_EQ(e->citation, std::string("OTB-1"));
    CHECK_EQ(e->kind, std::string("otb1.land"));
    CHECK(e->disclosure == otp::DISC_NAMED);
    CHECK_EQ(e->subject_id, std::string("AeroLand Realty LLC"));
    CHECK(contains(e->detail, "INV-2025-0001 land completed"));
    CHECK_EQ(ledger.entries_naming("AeroLand Realty LLC").size(), size_t{1});

    // The record keeps its vendor, and verifies.
    CHECK(contains(p.stamped, "AeroLand Realty LLC"));
    CHECK(bridge.verify_otb1(p.stamped).verified());
    CHECK(valid_otb1(p.stamped));
}

static void test_a_person_is_committed_and_withheld() {
    TEST("an OTB-1 vendor that is not a legal entity is committed AND withheld from the packet");

    const char* name = "AeroLand Realty LLC";  // pretend this is a sole trader

    for (otp::VendorKind kind : {otp::VendorKind::Unclassified, otp::VendorKind::NaturalPerson}) {
        otp::Ledger ledger = make_ledger();
        otp::Bridge bridge(ledger);
        const otp::Published p =
            bridge.publish_otb1(otb1_example("land-purchase.json"), otb1_replace(kind));
        CHECK(p.ok);
        if (!p.ok) { std::printf("        %s\n", p.error.c_str()); continue; }
        CHECK(p.vendor_withheld);

        // The ledger holds a commitment, not a name.
        const otp::Entry* e = ledger.get(1);
        CHECK(e != nullptr);
        if (e == nullptr) continue;
        CHECK(e->disclosure == otp::DISC_COMMITTED);
        CHECK(e->subject_id.empty());
        CHECK(e->subject_commitment != 0);
        CHECK(ledger.entries_naming(name).empty());
        CHECK(!contains(ledger.to_canonical(), name));
        CHECK(!contains(ledger.to_json(), name));

        // The packet that points at it does not name them either: committing a
        // person in the ledger while the packet names them would protect nobody.
        CHECK(!contains(p.stamped, name));
        CHECK(contains(p.stamped, otp::WITHHELD_VENDOR));
        CHECK(valid_otb1(p.stamped));

        // The transaction itself stays public, as DESIGN.md requires.
        CHECK(contains(p.stamped, "\"costUSD\":2200000"));
        CHECK(contains(p.stamped, "INV-2025-0001"));
        CHECK(contains(p.stamped, "Early Supporter Funds"));

        CHECK(bridge.verify_otb1(p.stamped).verified());

        // Publishing the ORIGINAL record is not verifiable: its digest is not the
        // one the ledger holds. Only the withheld form is the record.
        const std::string original = replaced(otb1_example("land-purchase.json"),
                                              "\"0xinv20250001\"", "\"" + otp::to_string(p.token) + "\"");
        CHECK_EQ(bridge.verify_otb1(original).verdict, otp::Verdict::EntryMismatch);

        // The original, offered again, is recognised as the same record.
        const otp::Published again =
            bridge.publish_otb1(otb1_example("land-purchase.json"), otb1_replace(kind));
        CHECK(again.ok && again.already_published && again.vendor_withheld);
        CHECK_EQ(again.stamped, p.stamped);
        CHECK_EQ(ledger.size(), size_t{1});
    }
}

static void test_classification_is_part_of_the_record() {
    TEST("classifying the same vendor differently is a different record, by design");

    // The classification is the caller's decision and an explicit one, so it is
    // part of what was published. A vendor first withheld and later named is two
    // entries, and the second one is the caller's doing.
    otp::Ledger ledger = make_ledger();
    otp::Bridge bridge(ledger);
    const otp::Published hidden = bridge.publish_otb1(otb1_example("materials-procurement.json"),
                                                      otb1_replace(otp::VendorKind::NaturalPerson));
    const otp::Published named = bridge.publish_otb1(otb1_example("materials-procurement.json"),
                                                     otb1_replace(otp::VendorKind::LegalEntity));
    CHECK(hidden.ok && named.ok);
    CHECK(!named.already_published);
    CHECK(hidden.digest != named.digest);
    CHECK_EQ(ledger.size(), size_t{2});
    const otp::Entry* first = ledger.get(1);
    const otp::Entry* second = ledger.get(2);
    CHECK(first != nullptr && second != nullptr);
    if (first == nullptr || second == nullptr) return;
    CHECK(first->disclosure == otp::DISC_COMMITTED);
    CHECK(second->disclosure == otp::DISC_NAMED);
}

static void test_otb1_default_refuses_legacy_tokens() {
    TEST("an OTB-1 record carrying a legacy token is not overwritten by default either");

    otp::Ledger ledger = make_ledger();
    otp::Bridge bridge(ledger);
    const otp::Published p = bridge.publish_otb1(otb1_example("facility-lab-build.json"));
    CHECK(!p.ok);
    CHECK(contains(p.error, "0xinv20250002"));
    CHECK_EQ(ledger.size(), size_t{0});

    CHECK(!bridge.publish_otb1("{}").ok);
    CHECK(contains(bridge.publish_otb1("{}").error, "not a valid OTB-1 record"));
    CHECK_EQ(bridge.verify_otb1(otb1_example("facility-lab-build.json")).verdict,
             otp::Verdict::ForeignToken);
}

static void test_otb1_altered_record_does_not_verify() {
    TEST("an altered OTB-1 record does not verify");

    otp::Ledger ledger = make_ledger();
    otp::Bridge bridge(ledger);
    const otp::Published p = bridge.publish_otb1(otb1_example("materials-procurement.json"),
                                                 otb1_replace(otp::VendorKind::LegalEntity));
    CHECK(p.ok);
    CHECK(bridge.verify_otb1(p.stamped).verified());

    CHECK_EQ(bridge.verify_otb1(replaced(p.stamped, "\"costUSD\":750000", "\"costUSD\":75000")).verdict,
             otp::Verdict::EntryMismatch);
    CHECK_EQ(bridge.verify_otb1(replaced(p.stamped, "StarMetals Incorporated", "StarMetals Inc")).verdict,
             otp::Verdict::EntryMismatch);
    CHECK_EQ(bridge.verify_otb1(replaced(p.stamped, "\"OTB_Audit\":true", "\"OTB_Audit\":false")).verdict,
             otp::Verdict::EntryMismatch);
    CHECK_EQ(bridge.verify_otb1(replaced(p.stamped, "Gulf Coast Depot", "Elsewhere")).verdict,
             otp::Verdict::EntryMismatch);

    // An OTB-1 token presented to the OTF-1 side is not an OTF-1 packet.
    CHECK_EQ(bridge.verify_otf1(p.stamped).verdict, otp::Verdict::NotAPacket);
}

static void test_otf1_and_otb1_share_one_ledger() {
    TEST("an OTF-1 event and an OTB-1 record append to the same ledger, and do not mix");

    otp::Ledger ledger = make_ledger();
    otp::Bridge bridge(ledger);
    const otp::Published event = bridge.publish_otf1(refinery());
    const otp::Published record = bridge.publish_otb1(otb1_example("land-purchase.json"),
                                                      otb1_replace(otp::VendorKind::LegalEntity));
    CHECK(event.ok && record.ok);
    CHECK_EQ(ledger.size(), size_t{2});
    CHECK(ledger.verify().intact);
    CHECK(bridge.verify_otf1(event.stamped).verified());
    CHECK(bridge.verify_otb1(record.stamped).verified());

    // Each token is valid, and each is about only its own kind of packet: the
    // OTB-1 record's token placed in an OTF-1 event does not verify.
    otf1::Packet packet;
    CHECK(otf1::decode(event.stamped, packet));
    otf1::EventPacket crossed = std::get<otf1::EventPacket>(packet);
    crossed.proofOfCleanupToken = otp::to_string(record.token);
    CHECK_EQ(bridge.verify_otf1(otf1::encode(crossed)).verdict, otp::Verdict::EntryMismatch);
}

int main() {
    std::printf("Overby Ledger bridge %s\n\n", otp::OTP_VERSION);

    test_sha256_matches_the_standard();
    test_canonical_form_matches_independent_code();
    test_an_event_is_recorded_and_stamped();
    test_the_same_record_is_one_entry();
    test_a_new_bridge_finds_what_was_already_published();
    test_an_altered_packet_does_not_verify();
    test_a_token_cannot_be_lifted_to_another_packet();
    test_the_ways_a_token_can_be_wrong();
    test_a_rewritten_ledger_stops_vouching();
    test_only_events_are_published();
    test_an_unidentified_ledger_is_refused();
    test_an_existing_token_is_never_destroyed();
    test_a_legal_entity_is_named();
    test_a_person_is_committed_and_withheld();
    test_classification_is_part_of_the_record();
    test_otb1_default_refuses_legacy_tokens();
    test_otb1_altered_record_does_not_verify();
    test_otf1_and_otb1_share_one_ledger();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
