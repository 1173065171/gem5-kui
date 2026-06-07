#!/usr/bin/env python3
"""
Summarize data blobs from the external lkssfull_sau_stdconv/10 firmware case.

The testcase stores rodata in 32-bit hex words. Each word is little-endian in
memory, so the line "5f746573" represents bytes "73 65 74 5f".
"""

import argparse
import hashlib
import json
from pathlib import Path


DEFAULT_CASE_DIR = Path(
    "/home/zbn/code/npu_lpnpu/testcase/lkssfull_sau_stdconv/"
    "10_sau_regress_INT16_SAU_NORMCONV_TEST_ID_0"
)

SEGMENT_SIZES = {
    "bias_sa": 32,
    "kernel_sa": 1008,
    "output_sa": 8192,
    "input_sa": 8064,
}

SYMBOL_SUFFIXES = {
    "bias_sa": "bias_sa",
    "kernel_sa": "kernel_sa",
    "output_sa": "output_sa",
    "input_sa": "input_sa",
}

HEAP_BASE = 0x20020000
HEAP_INITIAL_UNITS = 1790
HEAP_ALLOCS = [
    ("input_heap", 0x1F80),
    ("bias_heap", 32),
    ("kernel_heap", 1008),
    ("output_heap", 0x2000),
]


def parse_int(text):
    return int(text.strip().replace("_", ""), 16)


def parse_symbols(path):
    symbols = {}
    with path.open(encoding="utf-8") as handle:
        for line in handle:
            fields = line.split()
            if len(fields) < 3:
                continue
            try:
                addr = parse_int(fields[0])
            except ValueError:
                continue
            symbols[fields[-1]] = addr
    return symbols


def find_symbol(symbols, suffix):
    matches = [
        (name, addr)
        for name, addr in symbols.items()
        if name == suffix or name.endswith(suffix)
    ]
    if not matches:
        raise SystemExit(f"missing symbol ending with {suffix!r}")
    matches.sort(key=lambda item: (len(item[0]), item[0]))
    return matches[0]


def parse_word_hex(path):
    data = bytearray()
    with path.open(encoding="utf-8") as handle:
        for lineno, line in enumerate(handle, 1):
            text = line.strip()
            if not text:
                continue
            if len(text) != 8:
                raise SystemExit(
                    f"{path}:{lineno}: expected 8 hex digits, got {text!r}"
                )
            word = int(text, 16)
            data.extend(word.to_bytes(4, "little"))
    return bytes(data)


def heap_layout():
    free_units = HEAP_INITIAL_UNITS
    layout = []
    for name, size in HEAP_ALLOCS:
        units = ((size + 15) & ~15) // 16 + 1
        free_units -= units
        header = HEAP_BASE + free_units * 16
        addr = header + 16
        layout.append(
            {
                "name": name,
                "requested_size": size,
                "allocation_units": units,
                "header": header,
                "addr": addr,
                "low20": addr & 0xFFFFF,
            }
        )
    return layout


def blob_summary(name, symbol, addr, size, rodata_base, memory):
    offset = addr - rodata_base
    if offset < 0 or offset + size > len(memory):
        raise SystemExit(
            f"{name} at 0x{addr:08x} size {size} is outside "
            f"globala range 0x{rodata_base:08x}.."
            f"0x{rodata_base + len(memory):08x}"
        )

    blob = memory[offset : offset + size]
    summary = {
        "name": name,
        "symbol": symbol,
        "addr": addr,
        "offset": offset,
        "size": size,
        "sha256": hashlib.sha256(blob).hexdigest(),
        "nonzero_bytes": sum(1 for byte in blob if byte),
        "first32": blob[:32].hex(),
        "last32": blob[-32:].hex(),
    }

    if name == "output_sa":
        halfwords = [
            int.from_bytes(blob[i : i + 2], "little", signed=True)
            for i in range(0, len(blob), 2)
        ]
        summary["signed16"] = {
            "count": len(halfwords),
            "min": min(halfwords),
            "max": max(halfwords),
            "nonzero": sum(1 for value in halfwords if value),
            "first32": halfwords[:32],
            "last32": halfwords[-32:],
        }

    return summary


def build_summary(case_dir):
    symbol_path = case_dir / "elf_symbols.txt"
    memory_path = case_dir / "globala.hex"
    if not symbol_path.is_file():
        raise SystemExit(f"missing {symbol_path}")
    if not memory_path.is_file():
        raise SystemExit(f"missing {memory_path}")

    symbols = parse_symbols(symbol_path)
    rodata_base = symbols.get("__rodata_start__", 0x20010000)
    memory = parse_word_hex(memory_path)

    segments = []
    for name, size in SEGMENT_SIZES.items():
        symbol, addr = find_symbol(symbols, SYMBOL_SUFFIXES[name])
        segments.append(blob_summary(name, symbol, addr, size, rodata_base, memory))

    return {
        "case_dir": str(case_dir),
        "memory_file": str(memory_path),
        "rodata_base": rodata_base,
        "memory_bytes": len(memory),
        "segments": segments,
        "heap_layout": heap_layout(),
    }


def print_text(summary):
    print(f"case_dir: {summary['case_dir']}")
    print(
        "memory: "
        f"{summary['memory_file']} base=0x{summary['rodata_base']:08x} "
        f"bytes={summary['memory_bytes']}"
    )
    for segment in summary["segments"]:
        print(
            f"{segment['name']}: symbol={segment['symbol']} "
            f"addr=0x{segment['addr']:08x} offset=0x{segment['offset']:x} "
            f"size={segment['size']} nonzero={segment['nonzero_bytes']} "
            f"sha256={segment['sha256']}"
        )
        print(f"  first32={segment['first32']}")
        print(f"  last32 ={segment['last32']}")
        if "signed16" in segment:
            stats = segment["signed16"]
            print(
                "  signed16="
                f"count={stats['count']} min={stats['min']} "
                f"max={stats['max']} nonzero={stats['nonzero']}"
            )
    print("heap_layout:")
    for alloc in summary["heap_layout"]:
        print(
            f"  {alloc['name']}: size={alloc['requested_size']} "
            f"units={alloc['allocation_units']} "
            f"header=0x{alloc['header']:08x} "
            f"addr=0x{alloc['addr']:08x} "
            f"low20=0x{alloc['low20']:05x}"
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--case-dir",
        type=Path,
        default=DEFAULT_CASE_DIR,
        help="external testcase directory",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="print machine-readable JSON instead of text",
    )
    args = parser.parse_args()

    summary = build_summary(args.case_dir)
    if args.json:
        print(json.dumps(summary, indent=2, sort_keys=True))
    else:
        print_text(summary)


if __name__ == "__main__":
    main()
