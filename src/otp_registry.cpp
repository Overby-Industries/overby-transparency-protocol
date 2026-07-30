#include "otp/otp_registry.h"

#include <algorithm>
#include <sstream>

namespace otp {
namespace {

constexpr uint64_t FNV_OFFSET = 1469598103934665603ull;
constexpr uint64_t FNV_PRIME = 1099511628211ull;

void mix_u64(uint64_t& h, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        h ^= static_cast<uint8_t>(v >> (i * 8));
        h *= FNV_PRIME;
    }
}

void mix_str(uint64_t& h, const std::string& s) {
    for (char c : s) {
        h ^= static_cast<uint8_t>(c);
        h *= FNV_PRIME;
    }
    h ^= 0xFF;
    h *= FNV_PRIME;
}

std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') { out += "\\n"; }
        else { out += c; }
    }
    return out;
}

}  // namespace

const char* to_string(IngestStatus s) {
    switch (s) {
        case ING_ACCEPTED: return "accepted";
        case ING_UNCHANGED: return "unchanged";
        case ING_MALFORMED: return "malformed";
        case ING_BROKEN_CHAIN: return "broken_chain";
        case ING_HISTORY_REWRITTEN: return "history_rewritten";
        case ING_TRUNCATED: return "truncated";
        case ING_STATUS_COUNT: break;
    }
    return "unknown";
}

void Registry::track(const std::string& publisher_id, uint64_t heartbeat_ticks) {
    if (find_mutable(publisher_id) != nullptr) return;
    Source s;
    s.publisher_id = publisher_id;
    s.heartbeat_ticks = heartbeat_ticks;
    sources_.push_back(std::move(s));
}

Source* Registry::find_mutable(const std::string& publisher_id) {
    for (Source& s : sources_) {
        if (s.publisher_id == publisher_id) return &s;
    }
    return nullptr;
}

const Source* Registry::source(const std::string& publisher_id) const {
    for (const Source& s : sources_) {
        if (s.publisher_id == publisher_id) return &s;
    }
    return nullptr;
}

IngestReport Registry::ingest(const std::string& publisher_id,
                              const std::string& canonical_text,
                              uint64_t now_tick) {
    IngestReport rep;
    rep.publisher_id = publisher_id;

    track(publisher_id);
    Source* src = find_mutable(publisher_id);
    rep.entries_before = src->ledger.size();
    rep.entries_after = rep.entries_before;

    Ledger incoming;
    if (!Ledger::from_canonical(canonical_text, &incoming)) {
        rep.status = ING_MALFORMED;
        rep.reason = "could not parse the submitted ledger";
        return rep;
    }

    // Verify before storing. A registry that keeps whatever it is handed
    // launders it, and a broken chain arriving from a publisher is a finding
    // about that publisher rather than a copy worth holding.
    const Integrity integ = incoming.verify();
    if (!integ.intact) {
        rep.status = ING_BROKEN_CHAIN;
        rep.conflict_seq = integ.first_bad_seq;
        rep.reason = integ.reason;
        src->last_ingest_tick = now_tick;
        return rep;
    }

    // A copy shorter than what we hold is not an update. Entries do not
    // un-happen, and the only ways to produce this are truncation and a rewrite
    // that dropped something.
    if (incoming.size() < src->ledger.size()) {
        rep.status = ING_TRUNCATED;
        rep.conflict_seq = incoming.size() + 1;
        rep.reason = "submitted ledger is shorter than the copy already held";
        ++src->rewrite_attempts;
        src->last_rewrite_tick = now_tick;
        src->last_ingest_tick = now_tick;
        return rep;
    }

    // THE LOAD-BEARING CHECK. Every entry we already hold must appear
    // unchanged. A publisher rewriting history necessarily disagrees with what
    // was collected earlier, and the earlier copy is kept — overwriting
    // ourselves with the revision is precisely the laundering this class exists
    // to refuse.
    const std::vector<Entry>& held = src->ledger.entries();
    const std::vector<Entry>& fresh = incoming.entries();
    for (size_t i = 0; i < held.size(); ++i) {
        if (held[i].entry_hash != fresh[i].entry_hash) {
            rep.status = ING_HISTORY_REWRITTEN;
            rep.conflict_seq = held[i].seq;
            rep.reason =
                "submitted ledger disagrees with entries already held; the "
                "earlier copy is retained";
            ++src->rewrite_attempts;
            src->last_rewrite_tick = now_tick;
            src->last_ingest_tick = now_tick;
            return rep;
        }
    }

    src->last_ingest_tick = now_tick;

    if (incoming.size() == src->ledger.size()) {
        rep.status = ING_UNCHANGED;
        return rep;
    }

    // Strictly extends what we hold. This is the only path that stores.
    src->ledger = std::move(incoming);
    rep.status = ING_ACCEPTED;
    rep.entries_after = src->ledger.size();
    return rep;
}

std::vector<std::string> Registry::silent_publishers(uint64_t now_tick) const {
    std::vector<std::string> out;
    for (const Source& s : sources_) {
        if (s.heartbeat_ticks == 0) continue;
        if (now_tick >= s.last_ingest_tick + s.heartbeat_ticks) {
            out.push_back(s.publisher_id);
        }
    }
    return out;
}

std::vector<const Source*> Registry::publishers_with_rewrite_attempts() const {
    std::vector<const Source*> out;
    for (const Source& s : sources_) {
        if (s.rewrite_attempts > 0) out.push_back(&s);
    }
    return out;
}

std::vector<Registry::Hit> Registry::find_naming(
    const std::string& subject_id) const {
    std::vector<Hit> out;
    for (const Source& s : sources_) {
        for (uint64_t seq : s.ledger.entries_naming(subject_id)) {
            Hit h;
            h.publisher_id = s.publisher_id;
            h.seq = seq;
            // Resolved through corrections, never as published. A cross-ledger
            // report that showed allegations at face value would be the
            // permanent-accusation abuse rebuilt at the aggregation layer.
            h.standing = s.ledger.latest_standing(seq);
            out.push_back(h);
        }
    }
    return out;
}

void Registry::standing_counts(uint32_t out[ST_COUNT]) const {
    for (int i = 0; i < ST_COUNT; ++i) out[i] = 0;
    for (const Source& s : sources_) {
        for (const Entry& e : s.ledger.entries()) {
            if (e.corrects_seq != 0) continue;  // count subjects, not edits
            const Standing st = s.ledger.latest_standing(e.seq);
            if (st < ST_COUNT) ++out[st];
        }
    }
}

uint64_t Registry::digest() const {
    // Publisher-ordered, so two registries collecting the same sources agree
    // regardless of the order they happened to collect in.
    std::vector<const Source*> ordered;
    ordered.reserve(sources_.size());
    for (const Source& s : sources_) ordered.push_back(&s);
    std::sort(ordered.begin(), ordered.end(),
              [](const Source* a, const Source* b) {
                  return a->publisher_id < b->publisher_id;
              });

    uint64_t h = FNV_OFFSET;
    mix_u64(h, ordered.size());
    for (const Source* s : ordered) {
        mix_str(h, s->publisher_id);
        mix_u64(h, s->ledger.digest());
    }
    return h;
}

std::string Registry::to_json() const {
    std::ostringstream os;
    os << "{\"protocol\":\"OTP\",\"view\":\"registry\",\"digest\":\"" << digest()
       << "\",\"sources\":[";
    bool first = true;
    for (const Source& s : sources_) {
        if (!first) os << ",";
        first = false;
        os << "{\"publisher\":\"" << json_escape(s.publisher_id)
           << "\",\"entries\":" << s.ledger.size() << ",\"digest\":\""
           << s.ledger.digest() << "\",\"lastIngestTick\":"
           << s.last_ingest_tick << ",\"rewriteAttempts\":"
           << s.rewrite_attempts << "}";
    }
    os << "]}";
    return os.str();
}

}  // namespace otp
