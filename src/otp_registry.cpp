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
        case ING_WRONG_INSTANCE: return "wrong_instance";
        case ING_BROKEN_CHAIN: return "broken_chain";
        case ING_HISTORY_REWRITTEN: return "history_rewritten";
        case ING_TRUNCATED: return "truncated";
        case ING_STATUS_COUNT: break;
    }
    return "unknown";
}

bool Registry::set_instance(const std::string& instance,
                            const std::string& schema_version) {
    // track() alone registers intent to watch a publisher and holds no data, so
    // it does not block this. What blocks it is any actual ingest — a source
    // ledger holding entries, or the registry's own ledger already having
    // recorded an observation.
    if (own_ledger_.size() > 0) return false;
    for (const Source& s : sources_) {
        if (s.ledger.size() > 0) return false;
    }
    instance_ = instance;
    schema_version_ = schema_version;
    return own_ledger_.set_instance(instance, schema_version);
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

void Registry::publish_own(SubjectKind kind, const std::string& subject_id,
                           const std::string& event_kind, uint64_t tick,
                           const std::string& detail) {
    // Institutions only, always. Every subject own_ledger_ ever names is a
    // publisher — which by design record §3 is an institution or enterprise,
    // never a being on Commonwealth account — so this never touches the
    // commitment machinery §5.5 reserves for people.
    own_ledger_.publish(kind, subject_id, event_kind, ST_DETERMINED, tick, 2,
                        "OTP registry observation", detail);
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

    // Verify before storing, and verify before even checking whose instance it
    // claims — a ledger has to be internally consistent before its instance()
    // can be trusted at all, per the note in otp_ledger.h above Integrity: the
    // header is not itself hashed, and verify() is what ties it to the entries
    // that are.
    const Integrity integ = incoming.verify();
    if (!integ.intact) {
        rep.status = ING_BROKEN_CHAIN;
        rep.conflict_seq = integ.first_bad_seq;
        rep.reason = integ.reason;
        src->last_ingest_tick = now_tick;
        return rep;
    }

    // Design record §2: a reader must never mistake one instance for another.
    // This registry aggregates exactly one; anything else is refused outright,
    // before it can pollute any cross-publisher report.
    if (!instance_.empty() && incoming.instance() != instance_) {
        rep.status = ING_WRONG_INSTANCE;
        rep.reason = "ledger belongs to instance \"" + incoming.instance() +
                     "\"; this registry aggregates \"" + instance_ + "\"";
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
        publish_own(SUBJ_INSTITUTION, publisher_id, "rewrite_attempt_detected",
                   now_tick, "submitted ledger shorter than the held copy");
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
            publish_own(SUBJ_INSTITUTION, publisher_id,
                       "rewrite_attempt_detected", now_tick,
                       "entries disagreed with a previously held copy from "
                       "seq " + std::to_string(held[i].seq));
            return rep;
        }
    }

    src->last_ingest_tick = now_tick;

    // Heard from again: whatever silence was in progress is over. Reset the
    // edge so a future lapse is reported fresh rather than staying suppressed
    // by a flag left over from last time.
    src->silence_reported = false;

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

std::vector<std::string> Registry::run_builtin_test(uint64_t now_tick) {
    std::vector<std::string> newly_silent;
    for (Source& s : sources_) {
        if (s.heartbeat_ticks == 0) continue;
        const bool silent_now = now_tick >= s.last_ingest_tick + s.heartbeat_ticks;

        if (silent_now && !s.silence_reported) {
            s.silence_reported = true;
            newly_silent.push_back(s.publisher_id);
            publish_own(SUBJ_INSTITUTION, s.publisher_id, "publisher_silent",
                       now_tick,
                       "no ingest since tick " +
                           std::to_string(s.last_ingest_tick) +
                           "; heartbeat " +
                           std::to_string(s.heartbeat_ticks));
        }
        // Recovery (silent_now false while silence_reported true) is cleared in
        // ingest(), at the moment a fresh submission actually arrives — not
        // here, because run_builtin_test() runs on the clock and has nothing
        // new to base a recovery on.
    }
    return newly_silent;
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
            // A correction naming the same subject is not a second occurrence
            // of it — it is the resolution of the first, and latest_standing()
            // already folds it into that entry's reported standing below. Same
            // exclusion standing_counts() applies, for the same reason: without
            // it, an institution that was corrected would appear to have two
            // matters against it, one of which is really the record of the
            // other having been cleared.
            const Entry* e = s.ledger.get(seq);
            if (e != nullptr && e->corrects_seq != 0) continue;

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
    // The registry's own observations are part of what it holds; two
    // registries whose sources agree but whose own histories differ (one
    // caught a rewrite attempt, one has not run its built-in test yet) are
    // not in the same state, and the digest should say so.
    mix_u64(h, own_ledger_.digest());
    return h;
}

std::string Registry::to_json() const {
    std::ostringstream os;
    os << "{\"protocol\":\"OTP\",\"view\":\"registry\",\"instance\":\""
       << json_escape(instance_) << "\",\"digest\":\"" << digest()
       << "\",\"ownLedgerEntries\":" << own_ledger_.size() << ",\"sources\":[";
    bool first = true;
    for (const Source& s : sources_) {
        if (!first) os << ",";
        first = false;
        os << "{\"publisher\":\"" << json_escape(s.publisher_id)
           << "\",\"entries\":" << s.ledger.size() << ",\"digest\":\""
           << s.ledger.digest() << "\",\"lastIngestTick\":"
           << s.last_ingest_tick << ",\"rewriteAttempts\":"
           << s.rewrite_attempts << ",\"silenceReported\":"
           << (s.silence_reported ? "true" : "false") << "}";
    }
    os << "]}";
    return os.str();
}

}  // namespace otp
