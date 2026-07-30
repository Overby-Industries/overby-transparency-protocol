# Ledger Design Record

*Decisions taken, their consequences, and what remains open.*

- **Status:** Decisions agreed, specification in progress, **nothing built yet**
- **Date:** 2026-07-30

This document exists because the ledger touches five repositories and two very
different kinds of subject matter, and the interaction surface has to be settled
before any of it is written. Nothing here is implementation. Where a decision
forces a further decision, that is recorded in §6 rather than resolved quietly.

---

## 1. The four decisions

| # | Question | Decision |
|---|---|---|
| 1 | Real operations, simulation, or both? | **Both — one protocol, separate ledgers** |
| 2 | One ledger or one per publisher? | **One per publisher; an aggregator reads across** |
| 3 | What volume is published? | **Determinations and outcomes only** |
| 4 | Who holds independent copies? | **Public git mirrors from day one** |

---

## 2. Decision 1 — one protocol, separate ledgers

Two instances that share code and format and share nothing else.

| | Aevoria instance | Overby operations instance |
|---|---|---|
| Publishes | CUR determinations, measures, obligation lapses | OTF-1 mission telemetry, OTB-1 spending |
| Subject matter | Simulated governance | Real vendors, real money, real people |
| Operator | Commonwealth institutions | Overby Industries |
| Retention | Permanent (PDDC §12.7(b)(4)) | Whatever law requires |
| Commitment key | Held under CUR-H.6 §6.12 | Held by Overby |

### What this forces

**The protocol must contain no Overby-specific and no CUR-specific concept.**
Already true — `kind`, `citation`, and `detail` are free-form strings and the
library takes no view on governance. This decision makes that a requirement
rather than a nicety, and it is the property that lets a third party adopt the
protocol without adopting Aevoria.

**The two instances must be impossible to confuse.** A reader must never mistake
an Aevoria determination for a real-world one, and an aggregator must never
present them in a single list as though they were commensurable. That means an
instance identifier inside the canonical form and inside the hash — not a
filename, not a directory, not a field a mirror can strip. **This is a change to
the entry format and it is the only format change these decisions force.**

**The real instance inherits obligations the simulation does not.** Financial
record retention, contract confidentiality, and — most awkwardly — statutory
erasure rights. See §6.1, which is the sharpest unresolved problem in this
document.

---

## 3. Decision 2 — per-publisher ledgers, aggregator reads across

Each system owns its own chain and its own sequence numbering. The aggregator
holds copies and verifies them.

### What this forces

**Publishing pushes; reporting pulls.** The publisher assigns the sequence at the
moment of the event. If the aggregator numbered entries on arrival, an event that
never arrived would leave no hole, and gap-detection — the cheapest and most
reliable check in the whole design — would stop working.

**The aggregator cannot author.** It holds copies and checks them. A party that
only verifies cannot be the party that falsified, and that is a stronger position
than authorship, not a weaker one.

**Divergence is an alarm, not an update.** A publisher who rewrites history
necessarily submits something disagreeing with what was collected earlier. The
aggregator keeps the earlier copy and reports the conflict. Without this rule the
aggregator is an efficient mechanism for laundering a rewrite.

**The aggregator publishes its own ledger.** Its observations — this publisher
went silent, that one attempted a rewrite — are themselves conduct worth
recording, and recording them in its own chain means the watchdog is watched on
the same terms as everyone else. It is a publisher like any other and appears in
its own mirror.

**Every publisher needs a stable identifier**, unique within an instance and
never reused. §6.3.

---

## 4. Decision 3 — determinations and outcomes only

Publishing every transition, including refusals, produces a record that is
technically complete and practically unreadable. Volume is concealment by other
means, and it is the abuse a body reaches for when it cannot lawfully omit.

### What this forces, and it is the important one

**"Determinations only" hides inaction unless lapses are published.**

This is the sharpest consequence of the decision and it nearly went unnoticed. If
the record contains only determinations, then a body that simply never determines
anything produces a clean record. The absence of a finding is indistinguishable
from the absence of misconduct — which is exactly the failure CUR-H.6 §6.8(a) and
CUR-H.7 §7.12(c)(3) exist to prevent, arriving through the publication layer
instead of the enforcement layer.

So obligation lapses are publishable events, first-class. `run_builtin_test()`
already detects them on the clock. **The obligation register and the ledger are
complementary and neither works alone**: the register makes inaction detectable,
and the ledger makes it public.

### The publishable set

Derived from the corpus rather than chosen. A provision requires publication, so
it publishes:

| Event | Source |
|---|---|
| Determinations and their measures | CUR-H.7 §7.15(e), CUR-X.4 §4.9 |
| Corrections, dismissals, overturns | CREF §15 |
| Obligation lapses | CUR-H.6 §6.8(a), CUR-H.7 §7.12(c)(3), FOUNDATION-010 §5, CUR-N.5 §5.2B |
| Advocate appointments and declared interests | CUR-A §7.7(h), CUR-E §1.6(g) |
| Determinations of death | CUR-H.5 §5.5A(i) |
| Capture risk band changes | FOUNDATION-003 §12 |
| Protected Mode entry and exit | FOUNDATION-002 |

**What must publish is specified in CUR, not decided in code.** The library
implements a list the corpus states. A publication predicate living only in C++
is a predicate someone can quietly narrow.

### The completeness trade

Publishing a subset means the ledger alone cannot prove nothing was omitted. Two
things carry that instead: sequence gaps within the published stream, and the
internal log remaining complete and auditable on request. The claim is therefore
*"everything that had to be published, was"* — not *"everything that happened is
here."* Overstating that would be the same failure as overstating the hash.

---

## 5. Decision 4 — public git mirrors

Canonical text committed to a public repository each period.

### What this forces, and one thing it gives free

**Mirroring and anchoring are the same act.** Git commits are hash-linked and
timestamped, and a public host means third parties clone. A separate anchoring
mechanism is unnecessary — the mirror repository *is* the anchor, and it costs
nothing. This removes a component from the design.

**The canonical format should be git-readable.** The current separator is `\x1f`,
chosen for unambiguous parsing. That produces a diff no human can review, which
wastes the main advantage of mirroring to git in the first place. Tab-separated
with escaping gives the same parse guarantees and a readable diff. **This is the
second format change these decisions force**, and it should be made before
anything is published rather than after.

**Append-only must produce append-only diffs.** One entry per line, never
reflowed, so every commit is purely additive and a rewrite shows up as a
modification to an existing line — visible in `git diff` without any tooling at
all.

**Force-push is the remaining hole.** A mirror can have its history rewritten.
The answer is not a protected branch, which the operator controls anyway; it is
that anyone who has cloned holds the old history, and divergence is detectable by
comparison. That is real but only as real as the number of clones, which is
honest to state and not to overstate.

---

## 6. Open questions

### 6.1 Append-only versus erasure — the real instance

An append-only ledger and a statutory right to erasure are in direct conflict,
and the real-operations instance is subject to the second.

The likely resolution is that erasable personal data never enters the ledger.
Commitments already handle people. But OTB-1 records vendors, and a sole trader
is both a vendor and a person — so the boundary is not clean. **This needs a
decision before the operations instance publishes anything**, because entries
cannot be withdrawn afterwards. Publishing first and resolving later is not
available.

### 6.2 What an instance identifier looks like

Forced by Decision 1. It must be inside the hash so it cannot be stripped or
relabelled. Otherwise open.

### 6.3 Publisher identifiers

Stable, unique within an instance, never reused. A reused identifier silently
merges two publishers' histories. Scheme undecided.

### 6.4 Commitment key custody and rotation

CUR-H.6 §6.12 says who holds it for the Aevoria instance. Rotation is unaddressed
— rotating breaks the ability to locate a person's historical entries, which may
be a feature or a catastrophe depending on why you are rotating.

### 6.5 Publication cadence

Per event, per tick, or per period. Interacts with mirroring: a commit per event
is unreadable, a commit per month leaves a long window in which a rewrite is
undetectable.

### 6.6 Schema governance — the one an auditor will probe

**Whoever can change the schema can decide what stops being visible.** If Overby
can unilaterally revise OTF-1, OTB-1, or the publishable set, then the guarantee
reduces to *"we publish what we currently choose to publish"* — which is what
every corporate transparency report already promises and no regulator credits.

This is a governance question, not a technical one. It is cheap to answer now and
expensive to answer after the first external party relies on the record.

---

## 7. What changes in the existing sketch

Recorded so the sketch is not mistaken for agreed design:

- **Add** an instance identifier to `Entry`, inside the hash — §2
- **Change** the canonical separator from `\x1f` to tab-with-escaping — §5
- **Keep** `otp_registry.h/.cpp` as the aggregator, unchanged in shape — §3
- **Drop** the standalone anchoring component; git mirroring subsumes it — §5
- **Add** obligation lapses to the publishable set — §4
- **Move** the publishable set out of code and into a CUR title — §4
