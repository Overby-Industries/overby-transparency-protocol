# transparency-protocol

*A free, append-only, tamper-evident public record — for Overby's own conduct,
and for anyone else who publishes into it.*

- **Version:** 1.0.0
- **Language:** C++17, no external dependencies
- **Licence:** Apache License 2.0
- **Status:** Draft

---

## What's here

| | |
|---|---|
| [`docs/LEDGER.md`](docs/LEDGER.md) | **Start here.** What this is, why it isn't a blockchain, the disclosure asymmetry that protects the vulnerable, and how to use it — including the aggregator, `otp::Registry` |
| [`docs/DESIGN.md`](docs/DESIGN.md) | The design record: four decisions taken, what each one forces, and what remains open. Read this to understand *why* the code looks the way it does, not just what it does |
| [`include/otp/`](include/otp/) | `otp_ledger.h` — one publisher's chain. `otp_registry.h` — the aggregator that collects across many |
| [`tests/otp_tests.cpp`](tests/otp_tests.cpp) | Adversarial by design. Every claim this protocol makes has a test that tries to break it |

## In one paragraph

A publisher appends what it did to its own hash-linked ledger — free, no token,
no consensus, no fee. Anyone may hold a copy and verify it independently, and a
rewrite is detected by disagreement with those copies rather than trusted away.
Disclosure is asymmetric on purpose: institutions are named and fully
linkable; people are represented by a keyed commitment that cannot be linked
across their own entries, let alone to their identity, without a key held
separately. An aggregator (`otp::Registry`) collects from many publishers,
refuses anything that disagrees with what it already holds, notices when a
publisher goes silent, and keeps its own record of what it observed — because a
watchdog that only watches is unaccountable.

`docs/LEDGER.md` §2 makes the blockchain comparison explicit, including the one
property a public chain provides that this does not, and how to get it for
free.

## Where this sits among Overby's standards

| Repository | Defines |
|---|---|
| **`telemetry-protocol`** | OTF-1 — what a *mission* packet says: mining, debris, reclamation, SMESSC, ODCI |
| **`transparency-protocol`** (this repo) | OTB-1's packet shape, **and** the tamper-evident ledger layer neither OTF-1 nor OTB-1 defines on its own |
| **`code-of-universe-regulations`** | CUR-X.5 specifies *what* must be published and the properties the record must have; this repository is one implementation of it |

This library takes no view on governance and knows nothing about CUR. `kind`,
`citation`, and `detail` are free-form strings, so an OTF-1 debris capture, an
OTB-1 land purchase, and a CUR-X.5 determination all append to the same shape
of ledger without it knowing what any of them mean.

## Quick start

```cpp
#include <otp/otp_ledger.h>

otp::Ledger ledger;
ledger.set_commitment_key(key);
ledger.set_instance("overby-ops", "OTB-1/1.0");

ledger.publish(otp::SUBJ_INSTITUTION, "vendor-corp",
               "procurement", otp::ST_DETERMINED,
               /*tick=*/1180, /*severity=*/2,
               "OTB-1", "land purchase, West Texas");
```

See `docs/LEDGER.md` §6 for the full walkthrough, and §6.1 for the aggregator.

## Building

```
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Or without CMake: `cl /std:c++17 /EHsc /W4 /permissive- /utf-8 /Iinclude tests\otp_tests.cpp src\*.cpp`

Inside SCons, `SConscript` returns the source node list — see `docs/LEDGER.md` §7.

## Status

Draft. The hash is FNV-1a/64: fine against accident and casual tampering, not
against a determined adversary. `docs/LEDGER.md` §8 states exactly what that
means and what a publisher with real adversaries should swap in. Overstating a
transparency tool's guarantees is worse than having none.

---

*Released under the [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0), matching OTF-1 and OTB-1.*
