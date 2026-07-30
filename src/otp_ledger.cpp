#include "otp/otp_ledger.h"

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
    // Terminator, so "ab"+"c" and "a"+"bc" hash differently. Without it a
    // publisher could move text between adjacent fields and leave the hash
    // unchanged, which is a rewrite the chain would not catch.
    h ^= 0xFF;
    h *= FNV_PRIME;
}

// Field separator for the canonical form. Chosen because it cannot appear in
// the text fields after escaping, so parsing is unambiguous.
constexpr char SEP = '\x1f';

std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\') { out += "\\\\"; }
        else if (c == '\n') { out += "\\n"; }
        else if (c == SEP) { out += "\\u"; }
        else { out += c; }
    }
    return out;
}

std::string unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) { out += s[i]; continue; }
        ++i;
        if (s[i] == '\\') out += '\\';
        else if (s[i] == 'n') out += '\n';
        else if (s[i] == 'u') out += SEP;
        else out += s[i];
    }
    return out;
}

std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

}  // namespace

const char* to_string(Disclosure d) {
    switch (d) {
        case DISC_NAMED: return "named";
        case DISC_COMMITTED: return "committed";
        case DISC_COUNT: break;
    }
    return "unknown";
}

const char* to_string(Standing s) {
    switch (s) {
        case ST_ALLEGED: return "alleged";
        case ST_UNDER_REVIEW: return "under_review";
        case ST_DETERMINED: return "determined";
        case ST_DISMISSED: return "dismissed";
        case ST_REMEDIED: return "remedied";
        case ST_OVERTURNED: return "overturned";
        case ST_COUNT: break;
    }
    return "unknown";
}

Disclosure disclosure_for(SubjectKind k) {
    // Only an institution is named. Everything else — including an unclassified
    // subject — is committed. The default falls protective because wrongly
    // naming a person cannot be undone and wrongly protecting an institution
    // only means somebody has to ask.
    return k == SUBJ_INSTITUTION ? DISC_NAMED : DISC_COMMITTED;
}

uint64_t Ledger::commitment_for(const std::string& subject_id,
                                uint64_t seq) const {
    // Keyed, and salted with the sequence number. The sequence salt is what
    // makes two entries about the same person carry different commitments, so
    // holding the ledger does not let a reader group them.
    uint64_t h = FNV_OFFSET;
    mix_u64(h, commitment_key_);
    mix_u64(h, seq);
    mix_str(h, subject_id);
    mix_u64(h, commitment_key_);  // suffix too, so length games do not help
    return h;
}

uint64_t Ledger::hash_entry(const Entry& e) const {
    uint64_t h = FNV_OFFSET;
    mix_u64(h, e.prev_hash);
    mix_u64(h, e.seq);
    mix_u64(h, e.tick);
    mix_str(h, e.kind);
    mix_str(h, e.citation);
    mix_str(h, e.detail);
    mix_u64(h, e.severity);
    mix_u64(h, static_cast<uint64_t>(e.disclosure));
    mix_str(h, e.subject_id);
    mix_u64(h, e.subject_commitment);
    mix_u64(h, static_cast<uint64_t>(e.standing));
    mix_u64(h, e.corrects_seq);
    return h;
}

const Entry& Ledger::append(Entry e) {
    e.seq = entries_.empty() ? 1 : entries_.back().seq + 1;
    e.prev_hash = entries_.empty() ? 0 : entries_.back().entry_hash;

    // The commitment depends on seq, which is only known here, so it is
    // computed after the sequence is assigned rather than by the caller.
    if (e.disclosure == DISC_COMMITTED && !e.subject_id.empty()) {
        e.subject_commitment = commitment_for(e.subject_id, e.seq);
        // The identity does not enter the published entry. This is the line
        // that makes the asymmetry real rather than advisory.
        e.subject_id.clear();
    }

    e.entry_hash = hash_entry(e);
    entries_.push_back(std::move(e));
    return entries_.back();
}

const Entry& Ledger::publish(SubjectKind kind, const std::string& subject_id,
                             const std::string& event_kind, Standing standing,
                             uint64_t tick, uint8_t severity,
                             const std::string& citation,
                             const std::string& detail) {
    Entry e;
    e.tick = tick;
    e.kind = event_kind;
    e.citation = citation;
    e.detail = detail;
    e.severity = severity;
    e.standing = standing;
    e.disclosure = disclosure_for(kind);
    e.subject_id = subject_id;
    return append(std::move(e));
}

const Entry* Ledger::correct(uint64_t corrects_seq, Standing new_standing,
                             uint64_t tick, const std::string& detail) {
    const Entry* original = get(corrects_seq);
    if (original == nullptr) return nullptr;

    Entry e;
    e.tick = tick;
    e.kind = "correction";
    e.citation = original->citation;
    e.detail = detail;
    e.severity = original->severity;
    e.standing = new_standing;
    e.corrects_seq = corrects_seq;

    // A correction carries the disclosure of what it corrects. Publishing a
    // dismissal more openly than the allegation it clears would be its own
    // small injustice, and publishing it less openly would bury it.
    e.disclosure = original->disclosure;
    if (e.disclosure == DISC_NAMED) e.subject_id = original->subject_id;

    const Entry& appended = append(std::move(e));

    // A committed correction is linked to the original by corrects_seq rather
    // than by a shared commitment, so the pair is followable without either
    // entry naming anyone.
    return &appended;
}

Standing Ledger::latest_standing(uint64_t seq) const {
    const Entry* original = get(seq);
    if (original == nullptr) return ST_ALLEGED;

    Standing current = original->standing;
    for (const Entry& e : entries_) {
        if (e.corrects_seq == seq) current = e.standing;
    }
    return current;
}

Integrity Ledger::verify() const {
    Integrity r;
    uint64_t expected_prev = 0;
    uint64_t expected_seq = 1;

    for (size_t i = 0; i < entries_.size(); ++i) {
        const Entry& e = entries_[i];

        if (e.seq != expected_seq) {
            r.intact = false;
            r.first_bad_seq = e.seq;
            r.first_bad_index = i;
            r.reason = "sequence gap or reordering: an entry is missing or moved";
            return r;
        }
        if (e.prev_hash != expected_prev) {
            r.intact = false;
            r.first_bad_seq = e.seq;
            r.first_bad_index = i;
            r.reason = "chain break: previous entry was altered or removed";
            return r;
        }
        if (e.entry_hash != hash_entry(e)) {
            r.intact = false;
            r.first_bad_seq = e.seq;
            r.first_bad_index = i;
            r.reason = "entry altered after publication";
            return r;
        }

        expected_prev = e.entry_hash;
        ++expected_seq;
    }
    return r;
}

uint64_t Ledger::digest() const {
    // The last entry's hash already commits to every entry before it, so the
    // digest is just that — with the count folded in so an empty ledger and a
    // truncated one are distinguishable.
    uint64_t h = FNV_OFFSET;
    mix_u64(h, entries_.size());
    if (!entries_.empty()) mix_u64(h, entries_.back().entry_hash);
    return h;
}

Divergence Ledger::compare(const Ledger& other) const {
    Divergence d;
    d.mine = entries_.size();
    d.theirs = other.entries_.size();
    d.length_differs = d.mine != d.theirs;

    const size_t n = d.mine < d.theirs ? d.mine : d.theirs;
    for (size_t i = 0; i < n; ++i) {
        if (entries_[i].entry_hash != other.entries_[i].entry_hash) {
            d.identical = false;
            d.first_differing_seq = entries_[i].seq;
            return d;
        }
    }
    if (d.length_differs) {
        d.identical = false;
        d.first_differing_seq = n == 0 ? 0 : entries_[n - 1].seq + 1;
    }
    return d;
}

std::vector<uint64_t> Ledger::entries_naming(
    const std::string& subject_id) const {
    std::vector<uint64_t> out;
    for (const Entry& e : entries_) {
        if (e.disclosure == DISC_NAMED && e.subject_id == subject_id) {
            out.push_back(e.seq);
        }
    }
    return out;
}

std::vector<uint64_t> Ledger::entries_committed_to(uint64_t commitment) const {
    std::vector<uint64_t> out;
    for (const Entry& e : entries_) {
        if (e.disclosure == DISC_COMMITTED && e.subject_commitment == commitment) {
            out.push_back(e.seq);
        }
    }
    return out;
}

const Entry* Ledger::get(uint64_t seq) const {
    for (const Entry& e : entries_) {
        if (e.seq == seq) return &e;
    }
    return nullptr;
}

std::string Ledger::to_canonical() const {
    std::ostringstream os;
    os << "OTP\t" << OTP_VERSION << "\n";
    for (const Entry& e : entries_) {
        os << e.seq << SEP << e.tick << SEP << escape(e.kind) << SEP
           << escape(e.citation) << SEP << escape(e.detail) << SEP
           << static_cast<unsigned>(e.severity) << SEP
           << static_cast<unsigned>(e.disclosure) << SEP
           << escape(e.subject_id) << SEP << e.subject_commitment << SEP
           << static_cast<unsigned>(e.standing) << SEP << e.corrects_seq << SEP
           << e.prev_hash << SEP << e.entry_hash << "\n";
    }
    return os.str();
}

bool Ledger::from_canonical(const std::string& text, Ledger* out) {
    if (out == nullptr) return false;
    out->entries_.clear();

    std::istringstream is(text);
    std::string line;
    if (!std::getline(is, line)) return false;
    if (line.rfind("OTP\t", 0) != 0) return false;

    while (std::getline(is, line)) {
        if (line.empty()) continue;

        std::vector<std::string> f;
        std::string cur;
        for (char c : line) {
            if (c == SEP) { f.push_back(cur); cur.clear(); }
            else { cur += c; }
        }
        f.push_back(cur);
        if (f.size() != 13) return false;

        Entry e;
        try {
            e.seq = std::stoull(f[0]);
            e.tick = std::stoull(f[1]);
            e.kind = unescape(f[2]);
            e.citation = unescape(f[3]);
            e.detail = unescape(f[4]);
            e.severity = static_cast<uint8_t>(std::stoul(f[5]));
            e.disclosure = static_cast<Disclosure>(std::stoul(f[6]));
            e.subject_id = unescape(f[7]);
            e.subject_commitment = std::stoull(f[8]);
            e.standing = static_cast<Standing>(std::stoul(f[9]));
            e.corrects_seq = std::stoull(f[10]);
            e.prev_hash = std::stoull(f[11]);
            e.entry_hash = std::stoull(f[12]);
        } catch (...) {
            return false;
        }

        // Imported verbatim, hashes included. A copy is loaded exactly as it
        // was received and then checked with verify() — recomputing the hashes
        // on import would silently launder a tampered copy into a valid-looking
        // one, which is the opposite of the point.
        out->entries_.push_back(std::move(e));
    }
    return true;
}

std::string Ledger::to_json(size_t max_entries) const {
    std::ostringstream os;
    os << "{\"protocol\":\"OTP\",\"version\":\"" << OTP_VERSION << "\""
       << ",\"count\":" << entries_.size() << ",\"digest\":\"" << digest()
       << "\",\"entries\":[";

    size_t emitted = 0;
    for (const Entry& e : entries_) {
        if (max_entries != 0 && emitted >= max_entries) break;
        if (emitted != 0) os << ",";
        os << "{\"seq\":" << e.seq << ",\"tick\":" << e.tick << ",\"kind\":\""
           << json_escape(e.kind) << "\",\"citation\":\""
           << json_escape(e.citation) << "\",\"detail\":\""
           << json_escape(e.detail) << "\",\"severity\":"
           << static_cast<unsigned>(e.severity) << ",\"disclosure\":\""
           << to_string(e.disclosure) << "\"";
        if (e.disclosure == DISC_NAMED) {
            os << ",\"subject\":\"" << json_escape(e.subject_id) << "\"";
        } else {
            os << ",\"commitment\":\"" << e.subject_commitment << "\"";
        }
        os << ",\"standing\":\"" << to_string(latest_standing(e.seq))
           << "\",\"asPublished\":\"" << to_string(e.standing) << "\"";
        if (e.corrects_seq != 0) os << ",\"corrects\":" << e.corrects_seq;
        os << ",\"hash\":\"" << e.entry_hash << "\"}";
        ++emitted;
    }
    os << "]}";
    return os.str();
}

}  // namespace otp
