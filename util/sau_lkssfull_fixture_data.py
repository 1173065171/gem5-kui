#!/usr/bin/env python3
"""
Summarize data blobs from the external lkssfull_sau_stdconv/10 firmware case.

The testcase stores rodata in 32-bit hex words. Each word is little-endian in
memory, so the line "5f746573" represents bytes "73 65 74 5f".
"""

import argparse
import csv
import hashlib
import json
from collections import defaultdict
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


def extract_blob(name, addr, size, rodata_base, memory):
    offset = addr - rodata_base
    if offset < 0 or offset + size > len(memory):
        raise SystemExit(
            f"{name} at 0x{addr:08x} size {size} is outside "
            f"globala range 0x{rodata_base:08x}.."
            f"0x{rodata_base + len(memory):08x}"
        )
    return memory[offset : offset + size]


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
    blob = extract_blob(name, addr, size, rodata_base, memory)
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


def runtime_ranges(segment_blobs):
    allocs = {entry["name"]: entry for entry in heap_layout()}
    return {
        "input_heap": (allocs["input_heap"]["addr"], segment_blobs["input_sa"]),
        "bias_heap": (allocs["bias_heap"]["addr"], segment_blobs["bias_sa"]),
        "kernel_heap": (allocs["kernel_heap"]["addr"], segment_blobs["kernel_sa"]),
        "output_heap": (allocs["output_heap"]["addr"], segment_blobs["output_sa"]),
    }


def read_range(ranges, addr, size):
    for name, (start, blob) in ranges:
        offset = addr - start
        if 0 <= offset and offset + size <= len(blob):
            return name, blob[offset : offset + size]
    return None, None


def summarize_trace_data(trace_path, segment_blobs):
    ranges = runtime_ranges(segment_blobs)
    read_ranges = [
        ("input_heap", ranges["input_heap"]),
        ("kernel_heap", ranges["kernel_heap"]),
        ("bias_heap", ranges["bias_heap"]),
    ]
    write_ranges = [("output_heap", ranges["output_heap"])]
    payloads = {kind: bytearray() for kind in "ABCD"}
    sources = defaultdict(int)
    missing = []
    d_chunks = []

    with trace_path.open(encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        for lineno, row in enumerate(reader, 2):
            try:
                kind = row["kind"].strip().upper()
                addr = int(row["addr"], 0)
            except (KeyError, ValueError) as exc:
                raise SystemExit(f"{trace_path}:{lineno}: bad trace row") from exc
            if kind not in payloads:
                raise SystemExit(
                    f"{trace_path}:{lineno}: unsupported trace kind {kind!r}"
                )

            source, payload = read_range(
                write_ranges if kind == "D" else read_ranges, addr, 16
            )
            if payload is None:
                missing.append({"line": lineno, "kind": kind, "addr": addr})
                continue

            payloads[kind].extend(payload)
            sources[f"{kind}:{source}"] += 1
            if kind == "D":
                d_chunks.append((addr, payload))

    trace_data = {
        "trace": str(trace_path),
        "missing": missing,
        "sources": dict(sorted(sources.items())),
        "kinds": {},
    }
    for kind, payload in payloads.items():
        trace_data["kinds"][kind] = {
            "segments": len(payload) // 16,
            "bytes": len(payload),
            "sha256": hashlib.sha256(bytes(payload)).hexdigest(),
        }

    sorted_chunks = sorted(d_chunks)
    sorted_payload = bytearray()
    contiguous = True
    expected_addr = None
    for addr, payload in sorted_chunks:
        if expected_addr is not None and addr != expected_addr:
            contiguous = False
        sorted_payload.extend(payload)
        expected_addr = addr + len(payload)

    trace_data["d_sorted"] = {
        "segments": len(sorted_chunks),
        "unique_addresses": len({addr for addr, _payload in sorted_chunks}),
        "bytes": len(sorted_payload),
        "contiguous": contiguous,
        "sha256": hashlib.sha256(bytes(sorted_payload)).hexdigest(),
        "matches_output_sa": bytes(sorted_payload) == segment_blobs["output_sa"],
    }
    return trace_data


def dump_runtime_files(out_dir, segment_blobs):
    out_dir.mkdir(parents=True, exist_ok=True)
    ranges = runtime_ranges(segment_blobs)
    files = [
        ("input_heap.bin", "input_heap", "input_sa"),
        ("bias_heap.bin", "bias_heap", "bias_sa"),
        ("kernel_heap.bin", "kernel_heap", "kernel_sa"),
        ("output_expected.bin", "output_heap", "output_sa"),
    ]
    manifest = {"directory": str(out_dir), "files": {}}

    for filename, heap_name, segment_name in files:
        path = out_dir / filename
        blob = segment_blobs[segment_name]
        path.write_bytes(blob)
        addr, _runtime_blob = ranges[heap_name]
        manifest["files"][filename] = {
            "heap_name": heap_name,
            "segment_name": segment_name,
            "addr": addr,
            "low20": addr & 0xFFFFF,
            "size": len(blob),
            "sha256": hashlib.sha256(blob).hexdigest(),
        }

    manifest_path = out_dir / "manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    manifest["manifest"] = str(manifest_path)
    return manifest


def build_summary(case_dir, rtl_trace=None, return_blobs=False):
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
    segment_blobs = {}
    for name, size in SEGMENT_SIZES.items():
        symbol, addr = find_symbol(symbols, SYMBOL_SUFFIXES[name])
        segment_blobs[name] = extract_blob(name, addr, size, rodata_base, memory)
        segments.append(blob_summary(name, symbol, addr, size, rodata_base, memory))

    summary = {
        "case_dir": str(case_dir),
        "memory_file": str(memory_path),
        "rodata_base": rodata_base,
        "memory_bytes": len(memory),
        "segments": segments,
        "heap_layout": heap_layout(),
    }
    if rtl_trace is not None:
        summary["rtl_trace_data"] = summarize_trace_data(rtl_trace, segment_blobs)
    if return_blobs:
        return summary, segment_blobs
    return summary


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
    if "rtl_trace_data" in summary:
        trace_data = summary["rtl_trace_data"]
        print(f"rtl_trace_data: {trace_data['trace']}")
        for kind in "ABCD":
            data = trace_data["kinds"][kind]
            print(
                f"  {kind}: segments={data['segments']} "
                f"bytes={data['bytes']} sha256={data['sha256']}"
            )
        print("  sources:")
        for source, count in trace_data["sources"].items():
            print(f"    {source}: {count}")
        sorted_d = trace_data["d_sorted"]
        print(
            "  d_sorted: "
            f"segments={sorted_d['segments']} "
            f"unique={sorted_d['unique_addresses']} "
            f"bytes={sorted_d['bytes']} "
            f"contiguous={sorted_d['contiguous']} "
            f"matches_output_sa={sorted_d['matches_output_sa']} "
            f"sha256={sorted_d['sha256']}"
        )
        if trace_data["missing"]:
            print(f"  missing_payloads={len(trace_data['missing'])}")
    if "dump_runtime_files" in summary:
        dump = summary["dump_runtime_files"]
        print(f"dump_runtime_files: {dump['directory']}")
        for filename, info in dump["files"].items():
            print(
                f"  {filename}: addr=0x{info['addr']:08x} "
                f"size={info['size']} sha256={info['sha256']}"
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
    parser.add_argument(
        "--rtl-trace",
        type=Path,
        help="optional RTL sau_mem_addr_trace.csv to summarize payload data",
    )
    parser.add_argument(
        "--dump-runtime-dir",
        type=Path,
        help="optional output directory for runtime heap fixture binary files",
    )
    args = parser.parse_args()

    if args.dump_runtime_dir is not None:
        summary, segment_blobs = build_summary(
            args.case_dir, args.rtl_trace, return_blobs=True
        )
        summary["dump_runtime_files"] = dump_runtime_files(
            args.dump_runtime_dir, segment_blobs
        )
    else:
        summary = build_summary(args.case_dir, args.rtl_trace)
    if args.json:
        print(json.dumps(summary, indent=2, sort_keys=True))
    else:
        print_text(summary)


if __name__ == "__main__":
    main()
