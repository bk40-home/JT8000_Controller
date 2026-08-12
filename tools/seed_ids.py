#!/usr/bin/env python3
# =============================================================================
# seed_ids.py — add a generated-id field (`juce_id`, `v1_cc`) to params.yaml
# -----------------------------------------------------------------------------
# WHY
#   gen_params.py's own header says the JUCE emitter was always intended:
#   "the ESP32 and JUCE emitters are added in Phase 5 and will reuse the same
#   loaded/validated model". It is the one emitter never written, which is why
#   the plugin drifted: CCDefinitions.h is hand-authored, and 18 parameters
#   that exist in the firmware have no APVTS entry at all.
#
#   Before an emitter can be written, params.yaml has to know each parameter's
#   APVTS id.
#
# WHY NOT JUST DERIVE THE ID FROM THE KEY
#   Because APVTS ids are PERSISTED. They appear in every saved patch, in every
#   host session's automation, and in every DAW project that has ever loaded the
#   plugin. Renaming osc1_fine to osc1_fine_tune would silently break all of it.
#
#   So the 142 existing ids are preserved exactly as CCDefinitions.h declares
#   them, and only the 18 parameters that never had one get a fresh id derived
#   from the key. Nothing that has ever been saved changes.
#
# HOW THE EXISTING IDS WERE RECOVERED
#   By joining JtBridgeTable.h (v1 CC -> ParamID, with the yaml key in the
#   comment) against CCDefinitions.h (v1 CC -> APVTS id). Four more matched by
#   name. See key2apvts.json.
#
# EDITING STYLE
#   Line-based insertion, not yaml.safe_load + dump: round-tripping would strip
#   every comment and the "v1: CC::..." provenance notes, which are the only
#   record of where these parameters came from.
#
# USAGE
#   python3 seed_juce_ids.py --mapping key2apvts.json --yaml params.yaml
#   python3 seed_juce_ids.py ... --check     # report, change nothing
# =============================================================================

import argparse
import json
import re
import sys


def derive_id(key):
    """Fresh APVTS id for a parameter that never had one.

    Dots become underscores; nothing else is abbreviated. The existing ids use
    hand-shortened forms (amp_a, fe_a, dly_sync), but a NEW id has no history to
    preserve, and a predictable one is worth more than a terse one.
    """
    return key.replace(".", "_")


def main():
    ap = argparse.ArgumentParser(description="Seed juce_id into params.yaml")
    ap.add_argument("--yaml", default="params.yaml")
    ap.add_argument("--mapping", default="key2apvts.json")
    ap.add_argument("--field", default="juce_id",
                    help="field to insert (juce_id, v1_cc)")
    ap.add_argument("--check", action="store_true",
                    help="report what would change; write nothing")
    args = ap.parse_args()

    mapping = json.load(open(args.mapping, encoding="utf-8"))

    with open(args.yaml, encoding="utf-8", newline="") as f:
        lines = f.read().split("\n")

    out = []
    added = preserved = derived = skipped = 0
    key_re = re.compile(r"^(\s*)key:\s*([A-Za-z0-9_.]+)\s*$")

    for i, line in enumerate(lines):
        out.append(line)
        m = key_re.match(line)
        if not m:
            continue

        indent, key = m.group(1), m.group(2)

        # Already seeded: leave it alone, so the script is idempotent and safe
        # to re-run after params.yaml grows.
        if i + 1 < len(lines) and lines[i + 1].strip().startswith(args.field + ":"):
            skipped += 1
            continue

        if key in mapping:
            jid = mapping[key]
            preserved += 1
            note = ""
        else:
            jid = derive_id(key)
            derived += 1
            note = "   # new: no existing binding"

        out.append(f"{indent}{args.field}: {jid}{note}")
        added += 1

    print(f"parameters seen      : {preserved + derived + skipped}")
    print(f"  already seeded     : {skipped}")
    print(f"  existing id kept   : {preserved}")
    print(f"  new id derived     : {derived}")

    if args.check:
        print("\n--check: nothing written")
        return

    if added == 0:
        print("nothing to do")
        return

    with open(args.yaml, "w", encoding="utf-8", newline="") as f:
        f.write("\n".join(out))
    print(f"\nwrote {args.yaml}: {added} juce_id lines added")


if __name__ == "__main__":
    sys.exit(main())
