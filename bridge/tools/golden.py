#!/usr/bin/env python3
"""Reproduce the bridge's canonical form and digests in independent code.

The bridge records a packet in the ledger as a SHA-256 digest of its canonical
form (see include/otp/otp_bridge.h): the packet with its token field removed,
written with a fixed member order, UTC timestamps and the shortest exact number
form. For that digest to mean anything to a verifier written in another
language, the form has to be reproducible from the description alone. This
script is that: it shares no code with the C++, only the specification.

    python bridge/tools/golden.py            write tests/golden.txt
    python bridge/tools/golden.py --check    fail if tests/golden.txt is out of date

The C++ test suite reads tests/golden.txt and requires its own canonical form
and digest to match every line. CI runs --check, so the file cannot drift from
the examples it was made from.

Each line of golden.txt is five tab-separated fields:

    schema   path-under-bridge/deps   variant   sha256   canonical-form

`variant` is "as-is", or for OTB-1 also "withheld": the same record with its
vendor replaced by the placeholder the bridge uses for a natural person.

The canonical form, in full:

  * Parse the packet; take the fields below, in the order below. Optional
    fields that are absent stay absent. The token field is never included.
  * OTF-1 event:  version, kind ("event" even if the packet left it out),
    timestamp, mission{id, podId, type}, location{altitude_km, inclination_deg,
    zone}, target{catalogId, objectType, size_cm, mass_kg}, action,
    outcome{processedKg, repurposedAs, status}, certifications{SMESSC,
    ODCI_contribution}, taskId.
  * OTB-1 record: version, timestamp, investment{id, type, item, status},
    financials{costUSD, vendor, fundingSource}, location{site,
    coordinates{lat, lon}}, certifications{SMESSC_Applicable, OTB_Audit}.
  * No whitespace. Strings are escaped minimally: \\\" \\\\ \\b \\f \\n \\r \\t,
    other control characters below 0x20 as \\u00xx in lower case, everything
    else (including non-ASCII) as itself, in UTF-8.
  * Timestamps are UTC with a trailing Z, and a .mmm fraction only when the
    milliseconds are not zero; an offset such as +01:00 is applied, and a
    fraction finer than a millisecond is truncated.
  * Numbers: an integral value smaller than 9e15 is written as an integer.
    Anything else is the first of %.15g, %.16g, %.17g that reads back as
    exactly the same double.
"""
import argparse
import hashlib
import json
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path

BRIDGE = Path(__file__).resolve().parent.parent
DEPS = BRIDGE / "deps"
GOLDEN = BRIDGE / "tests" / "golden.txt"
WITHHELD_VENDOR = "[withheld: natural person]"

sys.path.insert(0, str(DEPS / "telemetry-protocol" / "tools"))
import validate  # noqa: E402  (telemetry-protocol's validator: real RFC 3339 parsing)


def timestamp(text):
    ms = validate.parse_timestamp_ms(text)
    if ms is None:
        raise ValueError(f"not a timestamp: {text!r}")
    moment = datetime(1970, 1, 1, tzinfo=timezone.utc) + timedelta(milliseconds=ms)
    out = moment.strftime("%Y-%m-%dT%H:%M:%S")
    if ms % 1000:
        out += f".{ms % 1000:03d}"
    return out + "Z"


def number(x):
    if isinstance(x, bool):
        raise TypeError("a boolean is not a number")
    x = float(x)
    if x == int(x) and abs(x) < 9.0e15:
        return str(int(x))
    for precision in (15, 16, 17):
        text = "%.*g" % (precision, x)
        if float(text) == x:
            return text
    return text


def emit(value):
    """Serialise an ordered dict, string, bool or number."""
    if isinstance(value, dict):
        return "{" + ",".join(json.dumps(k) + ":" + emit(v) for k, v in value.items()) + "}"
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, str):
        return json.dumps(value, ensure_ascii=False)
    return number(value)


def pick(source, names):
    return {n: source[n] for n in names if n in source}


def otf1_event(p):
    out = {"version": "OTF-1", "kind": "event", "timestamp": timestamp(p["timestamp"])}
    out["mission"] = pick(p["mission"], ["id", "podId", "type"])
    out["location"] = pick(p["location"], ["altitude_km", "inclination_deg", "zone"])
    if "target" in p:
        out["target"] = pick(p["target"], ["catalogId", "objectType", "size_cm", "mass_kg"])
    out["action"] = p["action"]
    out["outcome"] = pick(p["outcome"], ["processedKg", "repurposedAs", "status"])
    out["certifications"] = pick(p["certifications"], ["SMESSC", "ODCI_contribution"])
    if "taskId" in p:
        out["taskId"] = p["taskId"]
    return emit(out)


def otb1_record(p, withheld):
    out = {"version": "OTB-1", "timestamp": timestamp(p["timestamp"])}
    out["investment"] = pick(p["investment"], ["id", "type", "item", "status"])
    financials = pick(p["financials"], ["costUSD", "vendor", "fundingSource"])
    if withheld:
        financials["vendor"] = WITHHELD_VENDOR
    out["financials"] = financials
    location = {"site": p["location"]["site"]}
    if "coordinates" in p["location"]:
        location["coordinates"] = pick(p["location"]["coordinates"], ["lat", "lon"])
    out["location"] = location
    out["certifications"] = pick(p["certifications"], ["SMESSC_Applicable", "OTB_Audit"])
    return emit(out)


def lines():
    out = []
    for repo, schema in (("telemetry-protocol", "otf1"), ("transparency-protocol", "otb1")):
        for path in sorted((DEPS / repo / "examples").glob("*.json")):
            packet = json.loads(path.read_text(encoding="utf-8"))
            rel = path.relative_to(DEPS).as_posix()
            if schema == "otf1":
                if packet.get("kind", "event") != "event":
                    continue  # only events are published
                variants = [("as-is", otf1_event(packet))]
            else:
                variants = [("as-is", otb1_record(packet, False)),
                            ("withheld", otb1_record(packet, True))]
            for variant, canonical in variants:
                digest = hashlib.sha256(canonical.encode("utf-8")).hexdigest()
                out.append("\t".join([schema, rel, variant, digest, canonical]))
    return "\n".join(out) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    text = lines()
    if args.check:
        if not GOLDEN.exists() or GOLDEN.read_text(encoding="utf-8") != text:
            print(f"{GOLDEN} is out of date: run python bridge/tools/golden.py", file=sys.stderr)
            return 1
        print(f"{GOLDEN.name} is in sync with the examples ({text.count(chr(10))} lines)")
        return 0
    GOLDEN.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {GOLDEN} ({text.count(chr(10))} lines)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
