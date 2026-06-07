#!/usr/bin/env python3
"""
Summarize KuiSau/gem5 and RTL SAU memory traces.

The script uses the same request-kind values and 53-bit FNV-style order hash as
KuiSau. It can check gem5 stats.txt files, summarize an RTL-exported trace, or
compare both against the known RTL-gated representative cases.

Supported RTL trace formats:

- CSV with kind/source and addr/sram_mem_addr columns.
- CSV with sram_rd_enable, sram_wr_enable, sram_mem_addr, and either source/kind
  or enough RTL control columns to infer kind. With no explicit source, writes
  are OutputD; reads with bias_rd_valid are VectorC; other reads use
  input_switch bit 0 (input_switch/input_switch_d/input_switch_bit), where 0
  means RTL horizontal/MatrixB and 1 means RTL vertical/MatrixA.
- Whitespace text lines in one of these forms:
  "kind addr" or "cycle kind addr".
"""

import argparse
import csv
from collections import namedtuple
from pathlib import Path


BASE = 0x20000000
ADDR_A = BASE
ADDR_B = BASE + 0x1000
ADDR_C = BASE + 0x2000
ADDR_D = BASE + 0x3000
UNIT_SIZE = 16

HASH_MASK = (1 << 53) - 1
FNV_INIT = 1469598103934665603 & HASH_MASK
FNV_PRIME = 1099511628211


Kind = namedtuple("Kind", ["name", "value", "prefix", "count_key"])


KINDS = {
    "A": Kind("MatrixA", 1, "flowReadAAddr", "flowReadARequests"),
    "B": Kind("MatrixB", 2, "flowReadBAddr", "flowReadBRequests"),
    "C": Kind("VectorC", 3, "flowReadCAddr", "flowReadCRequests"),
    "D": Kind("OutputD", 4, "flowWriteDAddr", "flowWriteDRequests"),
}

KIND_ALIASES = {
    "a": "A",
    "matrixa": "A",
    "read_a": "A",
    "reada": "A",
    "vertical": "A",
    "vertical_addr": "A",
    "v": "A",
    "b": "B",
    "matrixb": "B",
    "read_b": "B",
    "readb": "B",
    "horizontal": "B",
    "horizontal_addr": "B",
    "h": "B",
    "c": "C",
    "vectorc": "C",
    "bias": "C",
    "bias_addr": "C",
    "read_c": "C",
    "readc": "C",
    "d": "D",
    "output": "D",
    "outputd": "D",
    "write_d": "D",
    "writed": "D",
}


Event = namedtuple("Event", ["kind", "addr", "cycle"])
Event.__new__.__defaults__ = (None,)


def parse_int(text):
    if text is None:
        raise ValueError("missing integer")
    if isinstance(text, int):
        return text
    value = text.strip().replace("_", "")
    if not value:
        raise ValueError("empty integer")
    if "'" in value:
        # Accept Verilog-ish values such as 2'b01, 20'h1000, or 'd16.
        _width, literal = value.split("'", 1)
        base = literal[0].lower()
        digits = literal[1:]
        if base == "h":
            return int(digits, 16)
        if base == "b":
            return int(digits, 2)
        if base == "d":
            return int(digits, 10)
    return int(value, 0)


def parse_bool(text):
    if text is None:
        return False
    if isinstance(text, int):
        return text != 0
    value = text.strip().lower()
    if value in {"", "0", "false", "f", "no", "n", "x", "z"}:
        return False
    return bool(parse_int(value)) if value[:1].isdigit() else True


def normalize_kind(value: str) -> str:
    key = value.strip().lower().replace("-", "_")
    try:
        return KIND_ALIASES[key]
    except KeyError as exc:
        raise ValueError(f"unknown request kind/source {value!r}") from exc


def addrs(base, count, kernel, step):
    return [
        base + i * step + j * UNIT_SIZE
        for i in range(count)
        for j in range(kernel)
    ]


def case_events(case):
    name = case.replace("-", "_").replace("+", "_").lower()
    aliases = {
        "normal_conv_stride_rtl": "normal_conv_stride",
        "normal_stride_rtl": "normal_conv_stride",
        "normal_conv_stride_shift_rtl": "normal_conv_stride_shift",
        "normal_stride_shift_rtl": "normal_conv_stride_shift",
        "pointwise_rtl": "pointwise",
        "gemm_shift_rtl": "gemm_shift",
    }
    name = aliases.get(name, name)

    if name == "normal_conv_stride":
        seq = (
            [Event("A", x) for x in addrs(ADDR_A, 2, 3, 32)]
            + [Event("B", x) for x in addrs(ADDR_B, 4, 1, 16)]
            + [Event("D", x) for x in addrs(ADDR_D, 16, 1, 16)]
        )
    elif name == "normal_conv_stride_shift":
        seq = (
            [Event("A", x) for x in addrs(ADDR_A, 2, 6, 32)]
            + [Event("B", x) for x in addrs(ADDR_B, 4, 1, 16)]
            + [Event("D", x) for x in addrs(ADDR_D, 32, 1, 16)]
        )
    elif name == "pointwise":
        seq = (
            [Event("A", x) for x in addrs(ADDR_A, 16, 1, 16)]
            + [Event("B", x) for x in addrs(ADDR_B, 16, 1, 16)]
            + [Event("C", x) for x in addrs(ADDR_C, 2, 1, 16)]
            + [Event("D", x) for x in addrs(ADDR_D, 16, 1, 16)]
        )
    elif name == "gemm_shift":
        seq = (
            [Event("A", x) for x in addrs(ADDR_A, 16, 2, 32)]
            + [Event("B", x) for x in addrs(ADDR_B, 16, 1, 16)]
            + [Event("D", x) for x in addrs(ADDR_D, 32, 1, 16)]
        )
    else:
        raise ValueError(f"unknown built-in case {case!r}")
    return seq


def fixed_case_summary(case):
    name = case.replace("-", "_").replace("+", "_").lower()
    aliases = {
        "stdconv_10": "lkssfull_sau_stdconv_10",
        "rtl_stdconv_10": "lkssfull_sau_stdconv_10",
        "lkssfull_sau_stdconv": "lkssfull_sau_stdconv_10",
    }
    name = aliases.get(name, name)

    if name == "lkssfull_sau_stdconv_10":
        return {
            "flowReadARequests": 2032,
            "flowReadAAddrFirst": 537029632,
            "flowReadAAddrLast": 537022528,
            "flowReadAAddrSum": 1091228802304,
            "flowReadAAddrXor": 0,
            "flowReadBRequests": 992,
            "flowReadBAddrFirst": 537022560,
            "flowReadBAddrLast": 537030576,
            "flowReadBAddrSum": 532730306816,
            "flowReadBAddrXor": 15360,
            "flowReadCRequests": 32,
            "flowReadCAddrFirst": 537022480,
            "flowReadCAddrLast": 537022512,
            "flowReadCAddrSum": 17184719872,
            "flowReadCAddrXor": 0,
            "flowWriteDRequests": 512,
            "flowWriteDAddrFirst": 537013280,
            "flowWriteDAddrLast": 537021456,
            "flowWriteDAddrSum": 274952892416,
            "flowWriteDAddrXor": 0,
            "flowTraceRequests": 3568,
            "flowTraceAddrFirst": 537022560,
            "flowTraceAddrLast": 537021456,
            "flowTraceAddrSum": 1916096721408,
            "flowTraceAddrXor": 15360,
            "flowTraceOrderHash": 8164653382730703,
        }
    return None


def case_summary(case):
    fixed = fixed_case_summary(case)
    if fixed is not None:
        return fixed
    return summarize_events(case_events(case))


def summarize_events(events):
    summary = {}
    events = list(events)

    for short, kind in KINDS.items():
        addrs_for_kind = [event.addr for event in events if event.kind == short]
        summary[kind.count_key] = len(addrs_for_kind)
        summary[f"{kind.prefix}First"] = addrs_for_kind[0] if addrs_for_kind else 0
        summary[f"{kind.prefix}Last"] = addrs_for_kind[-1] if addrs_for_kind else 0
        summary[f"{kind.prefix}Sum"] = sum(addrs_for_kind)
        xor_value = 0
        for addr in addrs_for_kind:
            xor_value ^= addr
        summary[f"{kind.prefix}Xor"] = xor_value

    trace_xor = 0
    trace_sum = 0
    order_hash = FNV_INIT
    for event in events:
        addr = event.addr
        trace_xor ^= addr
        trace_sum += addr
        token = (KINDS[event.kind].value << 56) ^ addr
        for _ in range(8):
            order_hash ^= token & 0xff
            order_hash = (order_hash * FNV_PRIME) & HASH_MASK
            token >>= 8

    summary["flowTraceRequests"] = len(events)
    summary["flowTraceAddrFirst"] = events[0].addr if events else 0
    summary["flowTraceAddrLast"] = events[-1].addr if events else 0
    summary["flowTraceAddrSum"] = trace_sum
    summary["flowTraceAddrXor"] = trace_xor
    summary["flowTraceOrderHash"] = order_hash
    return summary


def read_gem5_stats(path):
    stats = {}
    for line in path.read_text().splitlines():
        parts = line.split()
        if len(parts) < 2 or not parts[0].startswith("system.kuisau."):
            continue
        key = parts[0].split(".")[-1]
        if key.startswith("flowRead") or key.startswith("flowWrite") or key.startswith("flowTrace"):
            try:
                stats[key] = int(float(parts[1]))
            except ValueError:
                pass
    return stats


def nonempty_rows(path):
    rows = []
    for line in path.read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            rows.append(line)
    return rows


def read_trace(path):
    rows = nonempty_rows(path)
    if not rows:
        return []
    if "," in rows[0]:
        return read_csv_trace(rows)
    return read_text_trace(rows)


def read_csv_trace(rows):
    events = []
    reader = csv.DictReader(rows)
    if reader.fieldnames is None:
        return events

    field_map = {name.lower(): name for name in reader.fieldnames}

    def get(row, *names):
        for name in names:
            field = field_map.get(name.lower())
            if field and row.get(field) not in {None, ""}:
                return row[field]
        return None

    for row in reader:
        rd_enable = get(row, "sram_rd_enable", "rd_enable", "read")
        wr_enable = get(row, "sram_wr_enable", "wr_enable", "write")
        if rd_enable is not None or wr_enable is not None:
            is_read = parse_bool(rd_enable)
            is_write = parse_bool(wr_enable)
            if not is_read and not is_write:
                continue
        else:
            is_read = False
            is_write = False

        addr_text = get(row, "addr", "address", "sram_mem_addr", "mem_addr")
        if addr_text is None:
            raise ValueError("trace row missing addr/sram_mem_addr column")
        addr = parse_int(addr_text)
        cycle_text = get(row, "cycle", "time", "tick")
        cycle = parse_int(cycle_text) if cycle_text is not None else None

        kind_text = get(row, "kind", "source", "request_kind")
        if kind_text:
            kind = normalize_kind(kind_text)
        elif is_write:
            kind = "D"
        elif is_read:
            bias_valid = get(row, "bias_rd_valid", "bias_valid")
            if parse_bool(bias_valid):
                kind = "C"
            else:
                switch = get(row, "input_switch", "input_switch_d", "input_switch_bit")
                if switch is None:
                    raise ValueError(
                        "read trace row needs kind/source, bias_rd_valid, or input_switch"
                    )
                kind = "A" if parse_int(switch) & 1 else "B"
        else:
            continue
        events.append(Event(kind, addr, cycle))
    return events


def read_text_trace(rows):
    events = []
    for line in rows:
        parts = line.split()
        if len(parts) == 2:
            kind_text, addr_text = parts
            cycle = None
        elif len(parts) >= 3:
            cycle_text, kind_text, addr_text = parts[:3]
            cycle = parse_int(cycle_text)
        else:
            raise ValueError(f"bad trace line: {line!r}")
        events.append(Event(normalize_kind(kind_text), parse_int(addr_text), cycle))
    return events


def split_flows(events):
    flows = []
    current = []
    seen_d_run = False
    for event in events:
        if current and seen_d_run and event.kind != "D":
            flows.append(current)
            current = []
            seen_d_run = False
        current.append(event)
        if event.kind == "D":
            seen_d_run = True
    if current:
        flows.append(current)
    return flows


def format_addr(addr):
    return "-" if addr is None else f"0x{addr:x}"


def format_cycle(cycle):
    return "-" if cycle is None else str(cycle)


def flow_kind_values(flow, kind):
    return [event.addr for event in flow if event.kind == kind]


def flow_order(flow):
    order = []
    prev = None
    for event in flow:
        if event.kind != prev:
            order.append(event.kind)
            prev = event.kind
    return ",".join(order)


def print_flow_summary(label, events):
    flows = split_flows(events)
    print(f"{label} flows: {len(flows)}")
    for index, flow in enumerate(flows):
        counts = {
            kind: len(flow_kind_values(flow, kind))
            for kind in KINDS
        }
        first = {
            kind: values[0] if values else None
            for kind in KINDS
            for values in [flow_kind_values(flow, kind)]
        }
        last = {
            kind: values[-1] if values else None
            for kind in KINDS
            for values in [flow_kind_values(flow, kind)]
        }
        start_cycle = flow[0].cycle
        end_cycle = flow[-1].cycle
        print(
            f"  flow[{index:02d}] events={len(flow)} "
            f"cycles={format_cycle(start_cycle)}..{format_cycle(end_cycle)} "
            f"order={flow_order(flow)} "
            f"counts A/B/C/D={counts['A']}/{counts['B']}/{counts['C']}/{counts['D']}"
        )
        print(
            "    first "
            f"A={format_addr(first['A'])} B={format_addr(first['B'])} "
            f"C={format_addr(first['C'])} D={format_addr(first['D'])}; "
            "last "
            f"A={format_addr(last['A'])} B={format_addr(last['B'])} "
            f"C={format_addr(last['C'])} D={format_addr(last['D'])}"
        )


def expected_stat_keys():
    keys = []
    for kind in KINDS.values():
        keys.append(kind.count_key)
        keys += [
            f"{kind.prefix}First",
            f"{kind.prefix}Last",
            f"{kind.prefix}Sum",
            f"{kind.prefix}Xor",
        ]
    keys += [
        "flowTraceRequests",
        "flowTraceAddrFirst",
        "flowTraceAddrLast",
        "flowTraceAddrSum",
        "flowTraceAddrXor",
        "flowTraceOrderHash",
    ]
    return keys


def compare(label, actual, expected):
    ok = True
    print(label)
    for key in expected_stat_keys():
        if key not in actual:
            print(f"  {key}: actual=<missing> expected={expected[key]} BAD")
            ok = False
            continue
        match = actual[key] == expected[key]
        state = "OK" if match else "BAD"
        print(f"  {key}: actual={actual[key]} expected={expected[key]} {state}")
        ok = ok and match
    return ok


def print_summary(label, summary):
    print(label)
    for key in expected_stat_keys():
        if key in summary:
            print(f"  {key}: {summary[key]}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--case", help="built-in RTL-gated representative case")
    parser.add_argument("--gem5-stats", type=Path, help="gem5 m5out stats.txt")
    parser.add_argument("--rtl-trace", type=Path, help="RTL trace CSV/text file")
    parser.add_argument(
        "--show-flows",
        action="store_true",
        help="split --rtl-trace into D-terminated flows and print each flow",
    )
    parser.add_argument(
        "--dump-expected",
        action="store_true",
        help="print the built-in expected summary for --case",
    )
    args = parser.parse_args()

    failed = False
    expected = None
    if args.case:
        expected = case_summary(args.case)
        if args.dump_expected:
            print_summary(f"expected {args.case}", expected)

    if args.gem5_stats:
        gem5 = read_gem5_stats(args.gem5_stats)
        if expected is not None:
            failed |= not compare(f"gem5 {args.gem5_stats}", gem5, expected)
        else:
            print_summary(f"gem5 {args.gem5_stats}", gem5)

    rtl_events = None
    if args.rtl_trace:
        rtl_events = read_trace(args.rtl_trace)
        rtl = summarize_events(rtl_events)
        if expected is not None:
            failed |= not compare(f"rtl {args.rtl_trace}", rtl, expected)
        else:
            print_summary(f"rtl {args.rtl_trace}", rtl)

    if args.show_flows:
        if rtl_events is None:
            parser.error("--show-flows requires --rtl-trace")
        print_flow_summary(f"rtl {args.rtl_trace}", rtl_events)

    if not any([args.dump_expected, args.gem5_stats, args.rtl_trace, args.show_flows]):
        parser.error(
            "nothing to do; pass --dump-expected, --gem5-stats, "
            "--rtl-trace, or --show-flows"
        )

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
