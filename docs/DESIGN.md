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

### 6.1 Append-only versus erasure — RESOLVED

**Decision: personal data never enters the ledger. The ledger holds commitments;
identities live in a separate erasable store. Erasure destroys the store entry
and leaves the commitment permanently unresolvable.**

The conflict is real — an append-only record cannot delete, and the operations
instance is subject to erasure rights. It dissolves once the ledger stops holding
the thing that would have to be deleted.

A commitment whose store entry has been destroyed, and whose key is gone, is not
personal data. It is an opaque number that nobody — including the publisher — can
link to a person. So the chain stays intact and unbroken, and the person is no
longer identifiable. Both obligations are satisfied without either being weakened.

Three consequences:

**The store is deliberately outside the chain.** It is ordinary mutable storage,
because it has to be. Erasure is a delete, and a delete is what append-only
forbids — so the erasable material is kept where deletion is legal and the
immutable material holds nothing that can be demanded back.

**Erasure is itself published.** Destroying a store entry is conduct, and an
entry recording *that a subject exercised erasure* is appended to the chain. It
names nobody. Without it, erasure would be indistinguishable from a record that
never existed, which is a hole a publisher could hide in.

**The sole-trader boundary.** The line is not "vendor versus person" but **legal
entity versus natural person**. A registered company is NAMED. A sole trader
trading under their own name is a natural person and is COMMITTED — same rule as
everywhere else, applied to the case that made it look unclear.

This does not gut OTB-1's purpose, because the purpose is showing where money
went. The transaction stays fully public — amount, category, date, purpose,
funding source. Only the counterparty's identity is committed, and only where the
counterparty is a natural person. "Consultancy, $40,000, Category Y, Q3" is the
transparency OTB-1 promises; the consultant's name is not what a reader needs to
audit spending.

> **Not legal advice.** Financial records carry statutory retention that in many
> jurisdictions overrides erasure, which may make this moot for most OTB-1
> entries. The design is built to satisfy the stricter reading so it does not
> depend on which way that falls. It needs review by someone qualified before the
> operations instance publishes.

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

### 6.6 Schema governance — RESOLVED

**Decision: narrowing is harder than widening, schema changes are themselves
published, and every entry records the schema version it was published under.**

Whoever can change the schema can decide what stops being visible. Four rules,
three of which are enforceable in code:

**1. The ratchet.** Adding an event type to the publishable set is an ordinary
amendment. Removing one is an entrenched amendment at the highest threshold
FOUNDATION-002 §10 establishes. Transparency therefore only widens easily, and
narrowing costs what it should. Same asymmetry as `registered_subject_class`,
where the classification moves toward protection freely and away from it not at
all.

**2. A schema change is an entry.** It goes in the chain, in sequence, under the
old schema. A publisher who narrows what they publish cannot do it quietly, and
the narrowing is as durable as everything else in the record.

**3. Entries carry their schema version** and are never retroactively
reinterpreted. A reader knows which rules were in force when each entry was made,
so a later narrowing cannot be applied backwards to make old entries mean less.

**4. Narrowing requires external agreement** — an auditor, insurer, or agency
whose concurrence is recorded. This one is a commitment rather than a mechanism,
and it is the only one of the four that code cannot enforce.

### The limit, stated plainly

**Nothing here stops an operator abandoning the protocol entirely.** A publisher
who simply stops publishing, or forks to a private format, is beyond the reach of
any rule the protocol contains. That is not a gap to be closed — it is where
technical measures end.

What the design does instead is make abandonment *loud*. Mirrors stop advancing.
The aggregator's heartbeat check reports the publisher silent. The last entry
under the old schema stays in every clone anybody took. A publisher can leave;
they cannot leave quietly, and cannot make it look as though they never
published. That is the honest boundary of what this can promise.

---

## 7. What changed in the sketch, and status

Recorded so history is legible rather than left to be reverse-engineered from
diffs.

**Applied, all in `otp_ledger.h`/`.cpp`:**

- Instance identifier added to `Entry`, inside the hash — §2
- Canonical separator changed from `\x1f` to tab-with-escaping — §5
- Obligation lapses added to the publishable set — §4 (specified in CUR-X.5
  §5.3, not in this repository, which stays schema-agnostic)
- The publishable set moved out of code and into a CUR title — §4, CUR-X.5 §5.2
- Standalone anchoring component dropped; git mirroring subsumes it — §5

**Two gaps found only while building the aggregator, closed before it shipped:**

- `Ledger::set_instance()` was callable after entries existed, silently
  relabelling every later entry. It now refuses once the ledger is non-empty.
- `verify()` checked entries against each other but never against the ledger's
  own header — so a copy whose header alone had been edited would report the
  wrong `instance()` while still verifying. `verify()` now checks every entry
  against the header, closing that.

**`otp_registry.h`/`.cpp` — no longer a sketch, built and tested (§6.1 of
`LEDGER.md`):**

- Bound to one instance, refusing any other, matching §2's "impossible to
  confuse" requirement rather than trusting callers to keep instances separate
- Keeps its own append-only ledger of what it observed — a silent publisher, a
  rewrite attempt — because a watchdog that only watches is unaccountable
- `run_builtin_test()`, named to match `CURStateMachine`'s method of the same
  shape: a check that runs because the clock advanced, which is the only way
  to notice that nothing arrived
