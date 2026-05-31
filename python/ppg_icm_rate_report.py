#!/usr/bin/env python3
"""Summarize PPG/ICM isolation experiment evidence from SD pulls."""

from __future__ import annotations

import argparse
import csv
import re
from dataclasses import dataclass, field
from pathlib import Path


CSV_CANDIDATES = ("ecg_samples.csv", "ecg_001.csv")
LOG_CANDIDATES = ("log.txt", "log_001.txt", "diag_summary.txt")


@dataclass
class ModalityRate:
    file_count: int = 0
    first_tick: int | None = None
    last_tick: int | None = None
    actual_sps: float = 0.0


@dataclass
class ExperimentResult:
    root: Path
    csv_path: Path | None = None
    log_path: Path | None = None
    modalities: dict[str, ModalityRate] = field(default_factory=dict)
    config: dict[str, str] = field(default_factory=dict)
    diagnostics: dict[str, int] = field(default_factory=dict)


def _find_first(root: Path, names: tuple[str, ...]) -> Path | None:
    for name in names:
        direct = root / name
        if direct.exists():
            return direct
    for name in names:
        matches = sorted(root.rglob(name))
        if matches:
            return matches[0]
    return None


def _parse_key_values(line: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for part in line.split(",", 1)[1].split(","):
        if "=" not in part:
            continue
        key, value = part.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def _parse_int(value: str) -> int | None:
    try:
        return int(value, 0)
    except ValueError:
        return None


def scan_csv(path: Path) -> dict[str, ModalityRate]:
    raw: dict[str, list[int]] = {"PPG": [], "IMU": [], "ECG": []}
    with path.open("r", encoding="utf-8", errors="replace", newline="") as f:
        reader = csv.reader(f)
        next(reader, None)
        for row in reader:
            if len(row) < 2:
                continue
            kind = row[1].strip()
            if kind not in raw:
                continue
            try:
                raw[kind].append(int(row[0]))
            except ValueError:
                continue

    result: dict[str, ModalityRate] = {}
    for kind, ticks in raw.items():
        rate = ModalityRate(file_count=len(ticks))
        if ticks:
            rate.first_tick = ticks[0]
            rate.last_tick = ticks[-1]
            span_ms = rate.last_tick - rate.first_tick
            if span_ms > 0:
                rate.actual_sps = rate.file_count * 1000.0 / span_ms
        result[kind] = rate
    return result


def scan_log(path: Path) -> tuple[dict[str, str], dict[str, int]]:
    config: dict[str, str] = {}
    diagnostics: dict[str, int] = {}

    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(("RATE_ISO_CONFIG,", "RATE_ISO_PPG_REG,", "RATE_ISO_ICM_REG,")):
            config.update(_parse_key_values(line))
            continue
        if line.startswith(("RATE_ISO_I2C,", "PPG_STATS,", "IMU_STATS,")):
            for key, value in _parse_key_values(line).items():
                parsed = _parse_int(value)
                if parsed is not None:
                    diagnostics[key] = parsed

    return config, diagnostics


def analyze_experiment(root: Path) -> ExperimentResult:
    root = Path(root)
    result = ExperimentResult(root=root)

    csv_path = _find_first(root, CSV_CANDIDATES)
    if csv_path is not None:
        result.csv_path = csv_path
        result.modalities = scan_csv(csv_path)

    log_path = _find_first(root, LOG_CANDIDATES)
    if log_path is not None:
        result.log_path = log_path
        result.config, result.diagnostics = scan_log(log_path)

    return result


def print_result(result: ExperimentResult) -> None:
    print(f"root={result.root}")
    print(f"csv={result.csv_path or ''}")
    print(f"log={result.log_path or ''}")
    if result.config:
        print("config=" + ",".join(f"{k}={v}" for k, v in sorted(result.config.items())))
    for kind in ("PPG", "IMU", "ECG"):
        rate = result.modalities.get(kind, ModalityRate())
        first = "" if rate.first_tick is None else str(rate.first_tick)
        last = "" if rate.last_tick is None else str(rate.last_tick)
        print(
            f"{kind},file_count={rate.file_count},first_tick={first},"
            f"last_tick={last},actual_sps={rate.actual_sps:.2f}"
        )
    if result.diagnostics:
        print("diagnostics=" + ",".join(f"{k}={v}" for k, v in sorted(result.diagnostics.items())))


def main() -> int:
    parser = argparse.ArgumentParser(description="Summarize PPG/ICM rate experiment evidence")
    parser.add_argument("roots", nargs="+", help="One or more SD pull roots")
    args = parser.parse_args()

    for idx, root in enumerate(args.roots):
        if idx:
            print()
        print_result(analyze_experiment(Path(root)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
