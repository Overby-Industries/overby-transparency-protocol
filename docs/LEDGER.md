# Overby Ledger

*A free, append-only, tamper-evident record for OTF-1 and OTB-1 packets.*

- **Version:** 1.0.0
- **Language:** C++17, no external dependencies
- **Licence:** Apache 2.0, matching OTF-1 and OTB-1
- **Status:** Draft

## 0. Where this sits

Overby already has two schema standards. They answer the same question about
different subject matter:

| Standard | Repository | Defines |
|---|---|---|
| **OTF-1** Overby Telemetry Format | `telemetry-protocol` | What a *mission* packet says — mining, debris, reclamation, SMESSC, ODCI |
| **OTB-1** Overby Transparency for Business | `transparency-protocol` | What a *spending* packet says — investments, vendors, procurement |

Both define the shape of one packet. **Neither defines how a sequence of packets
is made tamper-evident**, and both currently fill that hole with a blockchain
token — `ProofOfCleanupToken` in OTF-1, `ProofOfInvestmentToken` in OTB-1.

This library is that missing layer, and it does the token's job for free. It is
schema-agnostic on purpose: `kind`, `citation`, and `detail` are free-form, so an
OTF-1 debris capture and an OTB-1 land purchase append to the same ledger without
the ledger knowing what either means.

### Replacing the token

Those two fields want one property: *proof this record exists and has not been
altered.* An entry hash provides exactly that, at zero cost:

```json
"certifications": {
  "SMESSC": true,
  "ODCI_contribution": 5.0,
  "ProofOfCleanupToken": "otp:1:8412:9f3c1a4e77b20d51"
}
```

The field stays and the schema does not change. What changes is that the value is
a ledger coordinate — protocol, sequence, entry hash — rather than a chain token,
so issuing one costs nothing and verifying one needs no wallet, no node, and no
gas. §2 covers what this does and does not buy against a chain, including the one
property a chain genuinely provides, and how to get it for approximately nothing.

---

## 1. What this is

A ledger that publishes what an institution did, in a form anyone can check and
nobody can quietly revise. It costs nothing to run, nothing to read, and nothing
to file into.

It is deliberately small. It records, and it does nothing else — `Ledger::imposes_consequences()`
returns `false` unconditionally, because a record that can act on a subject is a
court, and this is a record.

## 2. Why not a blockchain

A blockchain buys one specific thing: agreement on ordering among parties who do
not trust each other and share no authority. It is expensive because that is
expensive. Proof-of-work burns energy, proof-of-stake burns capital, and either
way somebody pays per entry.

An institution publishing its own conduct is not in that situation. Nobody
disputes who is publishing, and no rival chain is competing to be canonical.
Paying a consensus cost buys nothing that was needed.

What is actually needed is **tamper-evidence**, and a hash chain provides it for
free. Each entry commits to the one before it, so altering or removing an entry
breaks every hash after it — and the break is localised to the edit rather than
merely reporting that something somewhere changed. Appending is O(1). Verifying
is a linear walk. The marginal cost of an entry is a few dozen bytes.

No token. No mining. No staking. No fee. No gas. No wallet.

### The honest limit

A hash chain does not stop an operator who controls every copy from rewriting
the whole ledger and recomputing every hash. **Nothing cryptographic fixes
that** — blockchains included. They address it by distributing copies among
parties with divergent interests, and so does this.

Distribution is the free part. `to_canonical()` and `from_canonical()` are core
API rather than conveniences, and `compare()` reports the first sequence at
which two copies disagree. A ledger that diverges from the copies held by others
is detected by anyone who bothers to look, and holding a copy costs a text file.

The test suite demonstrates both halves and the fact that neither subsumes the
other:

| Attack | Caught by |
|---|---|
| Entry edited in place, stale hash left behind | `verify()` |
| Entry removed | `verify()` — sequence gap, before any hash is checked |
| Tail truncated | `compare()` against any other copy |
| Whole ledger republished with recomputed hashes | `compare()` only |

That last row is the one worth internalising. A careful operator rewriting
history leaves a ledger that verifies perfectly. Only distribution catches it.

### The one thing a chain buys that this does not — and how to get it free

A hash chain proves an entry has not been altered *relative to the entries around
it*. It does not prove **when** the entry was made. An operator could rewrite the
whole ledger tonight and present it as though it had always read that way, and a
reader seeing only that copy could not tell.

Public chains are usually reached for at exactly this point, because a chain
gives an ordering witnessed by parties with no reason to cooperate in a
backdating. But that property is obtainable without paying per entry, by
**anchoring**: publishing the ledger's `digest()` periodically somewhere the
publisher does not control.

An anchor costs one small write per period and covers **every entry since the
last anchor**. That is the entire difference — cost becomes O(1) per period
instead of O(1) per entry, which is precisely the cost that made a chain
unattractive. Adequate anchors, roughly in ascending order of effort:

- a signed tag in a public git repository, mirrored by anyone who clones it
- an Archive.org snapshot of a digest page
- a DNS TXT record
- a line in any published periodical
- one transaction on any public chain, if one is wanted — monthly, not per event

Any of these establishes *this digest existed by this date*. A rewritten ledger
then has to disagree with an anchor somebody else holds a copy of, which is the
same detection distribution provides, extended backwards through time.

### A note on the token fields in OTF-1 and OTB-1

Both standards describe their Proof-of-Cleanup and Proof-of-Investment tokens as
blockchain-verified. That framing is worth a second look, and not only on cost.

The audience those standards target — agencies, regulators, insurers — has spent
a decade watching tokenised environmental credits fail, and "blockchain-verified"
now carries weight in the wrong direction with exactly those readers. The
registries they actually recognise for carbon are operated registries with serial
numbers and retirement records, not chains.

More to the point, the whitepaper's own thesis is that Overby becomes the
standards body — the ISO of space operations. **A standards body is a trusted
registry.** If Overby is the registry, a chain pays consensus costs to avoid
trusting itself. If Overby is not the registry, issuing a token does not make it
one. Either way the chain is not doing the work the positioning needs.

What does that work: an operated registry, a hash-chained ledger anyone can
verify, distributed copies, and periodic anchoring. Free, auditable, and already
the shape regulators accept.

## 3. The asymmetry, which is the whole design

A record equally transparent about an institution and about a person who
reported harm **is not neutral**. It is a weapon pointed at the person, because
the institution can absorb exposure and the person cannot. A register of
everyone ever accused of anything, searchable by name, is how a transparency
system becomes an instrument for the exact harm it was built to surface.

So disclosure is asymmetric, and structurally rather than by policy:

| Subject | Disclosure | Linkable across entries? |
|---|---|---|
| Institution, enterprise, office, authorisation | **named** | Yes — pattern over an institution is what the record exists to reveal |
| A person, or any subject not established to be an institution | **committed** | No — pattern over a person is what enables targeting |

A committed subject's identity is replaced by a per-entry keyed commitment. Two
entries about the same person carry **different** commitments, so a reader
holding the ledger cannot even count how many times one unnamed person appears —
which is most of the way to identifying them.

There is deliberately **no by-name lookup for a committed subject**, and no
parameter by which a caller elects to name one. `publish()` derives disclosure
from `SubjectKind` and cannot be overridden. The absence is the protection.

The default falls protective: an unclassified subject is committed, because
wrongly naming a person cannot be undone while wrongly protecting an institution
only means somebody has to ask.

## 4. Why this is separate from both schemas

The obvious place for this was inside the corpus that publishes into it. It is
not there on purpose, and the same reasoning keeps it out of OTF-1 and OTB-1:
one implementation serves all three, and a ledger owned by one publisher is
verifiable only by whoever already trusts that publisher.

A ledger maintained inside the body it audits is verifiable only by whoever
already trusts that body. The Code of Universe Regulations disqualifies a party
holding an interest in its own determination (CUR-H.5 §5.5A(f)), and a
transparency ledger owned by the institution it reports on has exactly that
shape. Separation is not packaging convenience; it is the same principle applied
to the tooling.

Practically, it also means one implementation serves every publisher. This
library has **no dependency on any other Overby repository**, takes no view on
governance, and knows nothing about the CUR corpus — `kind` and `citation` are
free-form strings so the protocol never has to learn the vocabulary of the
things it publishes.

## 5. Four abuses this is built against

1. **Exposure of the vulnerable** — §3 above.
2. **The permanent accusation.** `Standing` distinguishes an allegation from a
   finding. Corrections are *appended and bound* to the original, never applied
   in place, and `latest_standing()` resolves through them. An allegation later
   dismissed reads as dismissed, and a dismissal cannot be lost by a consumer
   reading only the first entry. The original is never altered, because a record
   that edits itself cannot show that it was ever wrong — and a pattern of being
   wrong is exactly what a reader needs.
3. **Selective publication.** Sequence numbers are gap-free by construction, so
   an omission is a hole visible before any hash is checked.
4. **Quiet revision.** The chain, plus distributed copies.

## 6. Usage

```cpp
#include <otp/otp_ledger.h>

otp::Ledger ledger;
ledger.set_commitment_key(key);   // held separately from the ledger

// An institution. Named, and linkable across every entry concerning it.
ledger.publish(otp::SUBJ_INSTITUTION, "habitat-operator-4",
               "review_lapsed", otp::ST_ALLEGED,
               /*tick=*/1180, /*severity=*/4,
               "CUR-H.7 §7.12(c)(3)", "review overdue");

// A person. The identity never enters the entry.
ledger.publish(otp::SUBJ_PERSON, "citizen-8812",
               "report_received", otp::ST_ALLEGED,
               1180, 3, "CUR-H.6 §6.8(a)", "report filed");

// Determined, then overturned. Both are published; neither is erased.
ledger.correct(1, otp::ST_DETERMINED, 1400, "established on review");
ledger.correct(1, otp::ST_OVERTURNED, 2900, "overturned on appeal");

ledger.latest_standing(1);   // ST_OVERTURNED

// Anyone may hold a copy and check it against the publisher's.
otp::Ledger mine;
otp::Ledger::from_canonical(text, &mine);
mine.verify().intact;              // internally consistent?
theirs.compare(mine).identical;    // do we agree on the same chain?
```

## 7. Building

```
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Or directly, without CMake:

```
cl /std:c++17 /EHsc /W4 /permissive- /utf-8 /Iinclude tests\otp_tests.cpp src\*.cpp
```

Inside an SCons project, `SConscript` returns the source node list — the same
shape `libcur` uses, so a project already consuming one consumes the other
identically:

```python
env.Append(CPPPATH=["otp/include"])
otp_sources = SConscript("otp/SConscript", exports="env")
sources = Glob("src/*.cpp") + otp_sources
```

## 8. Status and what is not done

The hash is FNV-1a, 64-bit. That is **fine for accident and casual tampering and
not sufficient against a determined adversary** — 64 bits is brute-forceable and
FNV is not collision-resistant. A publisher with real adversaries should swap
`hash_entry()` for SHA-256; the chain structure is unchanged and the canonical
serialisation already isolates the hash so the substitution is local. It is
stated here rather than left for someone to discover, because a transparency
tool that overstates its own guarantees is worse than one that has none.

The commitment scheme has the same caveat and one more: a party holding the key
can test a guess at any identity. That is inherent to keyed commitments and is
why the key is held under access control rather than published.

Not yet built: streaming append to disk, multi-publisher federation, a signature
layer binding a ledger to a publisher key, and anchoring helpers (see 2).

---

*Released under [CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/) — free to use, share, and build upon.*
