#!/usr/bin/env python3
"""Merges one or more DTC JSON files into data/meb_dtc.bin for LittleFS binary search.

Usage:
    python generate_meb_dtc_bin.py [file1.json file2.json ...]

    If no files are given, defaults to data/meb_dtc.json relative to the project root.
    Duplicate codes (same 'code' value) are deduplicated; the first occurrence wins.

Record layout (148 bytes each, sorted ascending by code):
  - uint32_t code      ( 4 bytes, little-endian)
  - char     s_dsc[52] (52 bytes, null-terminated, zero-padded)
  - char     l_dsc[92] (92 bytes, null-terminated, zero-padded)
"""
import json
import struct
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)

BIN_PATH = os.path.join(PROJECT_DIR, "data", "meb_dtc.bin")

S_DSC_SIZE = 52  # 51 chars max + null terminator
L_DSC_SIZE = 92  # 91 chars max + null terminator
RECORD_SIZE = 4 + S_DSC_SIZE + L_DSC_SIZE  # 148 bytes


def main():
    # Resolve input file list
    if len(sys.argv) > 1:
        json_paths = [os.path.abspath(p) for p in sys.argv[1:]]
    else:
        json_paths = [os.path.join(PROJECT_DIR, "data", "meb_dtc.json")]

    # Validate all input files exist
    for path in json_paths:
        if not os.path.exists(path):
            print(f"ERROR: {path} not found", file=sys.stderr)
            sys.exit(1)

    # Skip regeneration if binary is already newer than all input files
    if os.path.exists(BIN_PATH):
        bin_mtime = os.path.getmtime(BIN_PATH)
        if all(bin_mtime >= os.path.getmtime(p) for p in json_paths):
            print("meb_dtc.bin is up to date, skipping generation.")
            return

    # Load and merge, first occurrence of each code wins
    merged = {}
    for path in json_paths:
        with open(path, "r", encoding="utf-8") as f:
            records = json.load(f)
        for record in records:
            code = record["code"]
            if code not in merged:
                merged[code] = record

    data = sorted(merged.values(), key=lambda r: r["code"])

    with open(BIN_PATH, "wb") as f:
        for record in data:
            code = record["code"]
            s_dsc = record["s_dsc"].encode("utf-8")[: S_DSC_SIZE - 1]
            s_dsc = s_dsc.ljust(S_DSC_SIZE, b"\x00")
            l_dsc = record["l_dsc"].encode("utf-8")[: L_DSC_SIZE - 1]
            l_dsc = l_dsc.ljust(L_DSC_SIZE, b"\x00")
            f.write(struct.pack("<I", code))
            f.write(s_dsc)
            f.write(l_dsc)

    total_input = sum(
        len(json.load(open(p, encoding="utf-8"))) for p in json_paths
    )
    print(
        f"Generated meb_dtc.bin: {len(data)} unique records from {total_input} total "
        f"across {len(json_paths)} file(s), {len(data) * RECORD_SIZE} bytes."
    )


if __name__ == "__main__":
    main()
