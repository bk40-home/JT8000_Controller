#!/usr/bin/env python3
"""
sync_cc_defs.py — JT-8000 CC Definition Sync Checker

Parses the firmware CCDefs.h (single source of truth) and compares against
the JUCE CCDefinitions.h and HTML editor CC constant block.  Reports any
CCs that are missing, have different numbers, or have mismatched names.

USAGE:
    python sync_cc_defs.py [--firmware path/to/CCDefs.h]
                           [--juce path/to/CCDefinitions.h]
                           [--html path/to/JT8000_Editor_v8.html]

    All paths are optional.  The script searches for files relative to the
    working directory if not specified:
      - JTEENSY8000/CCDefs.h            (firmware)
      - Source/CCDefinitions.h           (JUCE plugin)
      - Source/JT8000_Editor_v8.html     (HTML editor)

OUTPUT:
    A plain-text diff report printed to stdout.  Non-zero exit code if any
    mismatches are found, so it can be used as a CI check or pre-build gate.

HOW IT WORKS:
    1. Parses `static constexpr uint8_t NAME = NN;` lines from CCDefs.h
    2. Parses `{ NN, "param_id", ... }` lines from kAllParams[] in CCDefinitions.h
    3. Parses `NAME:NN` from the CC constant block in the HTML editor
    4. Cross-references all three by CC number

LIMITATIONS:
    - Does not parse section assignments or default values (those require
      manual review when changing).  This tool catches the most common
      failure mode: a CC added to firmware but forgotten in the editors.
    - Enum option arrays (waveform names, etc.) are not compared.
"""

import re
import sys
import os
import argparse


def parse_firmware_ccdefs(path):
    """Extract CC name → number mappings from firmware CCDefs.h.
    
    Matches lines like:
        static constexpr uint8_t OSC1_WAVE = 21;
    
    Returns dict { 'OSC1_WAVE': 21, ... }
    """
    pattern = re.compile(
        r'static\s+constexpr\s+uint8_t\s+(\w+)\s*=\s*(\d+)\s*;'
    )
    ccs = {}
    try:
        with open(path, 'r', encoding='utf-8') as f:
            for line in f:
                m = pattern.search(line)
                if m:
                    name = m.group(1)
                    num = int(m.group(2))
                    # Skip non-CC constants (mode indices, counts, engine flags)
                    skip_suffixes = ('_COUNT', '_OBXA', '_VA', '_4POLE', '_2POLE',
                                     '_2POLE_BP', '_2POLE_PUSH', '_XPANDER',
                                     '_XPANDER_M')
                    if any(name.endswith(s) for s in skip_suffixes):
                        continue
                    if name.startswith('FILTER_MODE_') or name.startswith('FILTER_ENGINE_'):
                        continue
                    ccs[name] = num
    except FileNotFoundError:
        print(f"WARNING: Firmware file not found: {path}")
    return ccs


def parse_juce_ccdefs(path):
    """Extract CC numbers from kAllParams[] in CCDefinitions.h.
    
    Matches lines like:
        { 21,  "osc1_wave",  "OSC1 Wave",  Section::OSC1, ...
    
    Returns dict { cc_number: 'param_id', ... }
    """
    # Match the opening of each kAllParams entry
    pattern = re.compile(
        r'\{\s*(\d+)\s*,\s*"(\w+)"'
    )
    ccs = {}
    in_table = False
    try:
        with open(path, 'r', encoding='utf-8') as f:
            for line in f:
                if 'kAllParams[]' in line:
                    in_table = True
                    continue
                if in_table:
                    if line.strip().startswith('};'):
                        break
                    m = pattern.search(line)
                    if m:
                        cc = int(m.group(1))
                        param_id = m.group(2)
                        ccs[cc] = param_id
    except FileNotFoundError:
        print(f"WARNING: JUCE file not found: {path}")
    return ccs


def parse_html_ccs(path):
    """Extract CC name:number pairs from the HTML editor's CC constant block.
    
    Matches patterns like:
        OSC1_WAVE:21,
    
    Returns dict { 'OSC1_WAVE': 21, ... }
    """
    pattern = re.compile(r'(\w+):(\d+)')
    ccs = {}
    in_cc_block = False
    try:
        with open(path, 'r', encoding='utf-8') as f:
            for line in f:
                # The CC block starts with "const CC={" and ends with "};"
                if 'const CC={' in line or 'const CC =' in line:
                    in_cc_block = True
                if in_cc_block:
                    for m in pattern.finditer(line):
                        name = m.group(1)
                        num = int(m.group(2))
                        ccs[name] = num
                    if '};' in line:
                        in_cc_block = False
    except FileNotFoundError:
        print(f"WARNING: HTML file not found: {path}")
    return ccs


def find_file(candidates):
    """Return the first candidate path that exists, or None."""
    for c in candidates:
        if os.path.isfile(c):
            return c
    return None


def main():
    parser = argparse.ArgumentParser(description='JT-8000 CC Definition Sync Checker')
    parser.add_argument('--firmware', help='Path to firmware CCDefs.h')
    parser.add_argument('--juce', help='Path to JUCE CCDefinitions.h')
    parser.add_argument('--html', help='Path to HTML editor')
    args = parser.parse_args()

    # Auto-detect file paths if not specified
    fw_path = args.firmware or find_file([
        'JTEENSY8000/CCDefs.h',
        '../JTEENSY8000/CCDefs.h',
        'CCDefs.h',
    ])
    juce_path = args.juce or find_file([
        'Source/CCDefinitions.h',
        'CCDefinitions.h',
    ])
    html_path = args.html or find_file([
        'Source/JT8000_Editor_v8.html',
        'JT8000_Editor_v8.html',
    ])

    if not fw_path:
        print("ERROR: Cannot find firmware CCDefs.h — specify with --firmware")
        sys.exit(2)

    print(f"Firmware:  {fw_path}")
    print(f"JUCE:      {juce_path or '(not found)'}")
    print(f"HTML:      {html_path or '(not found)'}")
    print()

    # Parse all three sources
    fw_ccs = parse_firmware_ccdefs(fw_path)
    juce_ccs = parse_juce_ccdefs(juce_path) if juce_path else {}
    html_ccs = parse_html_ccs(html_path) if html_path else {}

    # Build a set of all CC numbers from firmware
    fw_by_num = {}
    for name, num in fw_ccs.items():
        fw_by_num.setdefault(num, []).append(name)

    juce_cc_nums = set(juce_ccs.keys())
    html_cc_nums = set(html_ccs.values())

    issues = 0

    # Check: firmware CCs missing from JUCE
    print("=" * 60)
    print("CCs in firmware but MISSING from JUCE CCDefinitions.h:")
    print("-" * 60)
    missing_juce = sorted(set(fw_by_num.keys()) - juce_cc_nums)
    if missing_juce:
        for cc in missing_juce:
            names = fw_by_num[cc]
            print(f"  CC {cc:3d}  {', '.join(names)}")
            issues += 1
    else:
        print("  (none — all firmware CCs present in JUCE)")
    print()

    # Check: firmware CCs missing from HTML
    print("CCs in firmware but MISSING from HTML editor:")
    print("-" * 60)
    missing_html = sorted(set(fw_by_num.keys()) - html_cc_nums)
    if missing_html:
        for cc in missing_html:
            names = fw_by_num[cc]
            print(f"  CC {cc:3d}  {', '.join(names)}")
            issues += 1
    else:
        print("  (none — all firmware CCs present in HTML)")
    print()

    # Check: JUCE CCs not in firmware (orphans)
    print("CCs in JUCE but NOT in firmware (potential orphans):")
    print("-" * 60)
    orphan_juce = sorted(juce_cc_nums - set(fw_by_num.keys()))
    if orphan_juce:
        for cc in orphan_juce:
            print(f"  CC {cc:3d}  JUCE param: {juce_ccs[cc]}")
            issues += 1
    else:
        print("  (none)")
    print()

    # Check: HTML CCs not in firmware (orphans)
    print("CCs in HTML but NOT in firmware (potential orphans):")
    print("-" * 60)
    orphan_html = sorted(html_cc_nums - set(fw_by_num.keys()))
    if orphan_html:
        for cc in orphan_html:
            names = [n for n, v in html_ccs.items() if v == cc]
            print(f"  CC {cc:3d}  HTML name: {', '.join(names)}")
            issues += 1
    else:
        print("  (none)")
    print()

    # Check: name mismatches between firmware and HTML
    print("CC number mismatches (same name, different CC#):")
    print("-" * 60)
    mismatches = 0
    for name, fw_num in fw_ccs.items():
        if name in html_ccs and html_ccs[name] != fw_num:
            print(f"  {name}: firmware=CC{fw_num}, HTML=CC{html_ccs[name]}")
            mismatches += 1
            issues += 1
    if mismatches == 0:
        print("  (none — all shared names agree on CC numbers)")
    print()

    # Summary
    print("=" * 60)
    if issues == 0:
        print("ALL CLEAR — firmware, JUCE, and HTML are in sync.")
    else:
        print(f"FOUND {issues} ISSUE(S) — manual sync required.")
    print("=" * 60)

    sys.exit(0 if issues == 0 else 1)


if __name__ == '__main__':
    main()
