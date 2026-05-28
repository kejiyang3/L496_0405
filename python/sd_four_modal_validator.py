#!/usr/bin/env python3
"""Validate SD-card four-modal recording evidence.

This tool inspects session_XXX.txt, ecg_XXX.csv, and mic_XXX.wav files.
It treats SD artifacts as authoritative evidence and flags sessions where
ECG continues while PPG, IMU, or MIC only cover a small part of the window.
"""

from __future__ import annotations

import argparse
import csv
import re
import wave
from dataclasses import dataclass, field
from pathlib import Path


MIN_COVERAGE_RATIO = 0.80
MAX_MIC_DURATION_RATIO_ERROR = 0.25


@dataclass
class SequenceResult:
    seq: str
    ok: bool
    duration_ms: int
    counts: dict[str, int] = field(default_factory=dict)
    spans_ms: dict[str, int] = field(default_factory=dict)
    mic_duration_s: float = 0.0
    problems: list[str] = field(default_factory=list)


def parse_session(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.exists():
        return values
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def scan_csv(path: Path) -> tuple[dict[str, int], dict[str, int]]:
    counts: dict[str, int] = {}
    first_ts: dict[str, int] = {}
    last_ts: dict[str, int] = {}

    if not path.exists() or path.stat().st_size == 0:
        return counts, {}

    with path.open("r", encoding="utf-8", errors="replace", newline="") as f:
        reader = csv.reader(f)
        next(reader, None)
        for row in reader:
            if len(row) < 2:
                continue
            kind = row[1].strip()
            if kind not in {"ECG", "PPG", "IMU"}:
                continue
            try:
                ts = int(row[0])
            except ValueError:
                continue
            counts[kind] = counts.get(kind, 0) + 1
            first_ts.setdefault(kind, ts)
            last_ts[kind] = ts

    spans = {
        kind: max(0, last_ts[kind] - first_ts[kind])
        for kind in first_ts.keys() & last_ts.keys()
    }
    return counts, spans


def wav_duration_s(path: Path) -> float:
    if not path.exists() or path.stat().st_size < 44:
        return 0.0
    try:
        with wave.open(str(path), "rb") as w:
            rate = w.getframerate()
            if rate <= 0:
                return 0.0
            return w.getnframes() / rate
    except (wave.Error, OSError, EOFError):
        return 0.0


def session_mic_duration_s(session: dict[str, str]) -> float:
    try:
        mic_ms = int(session.get("mic_ms", "0") or "0")
        mic_bytes = int(session.get("mic_bytes", "0") or "0")
    except ValueError:
        return 0.0
    if mic_ms <= 0 or mic_bytes <= 0:
        return 0.0
    return mic_ms / 1000.0


def _seq_int(seq: str) -> int:
    return int(seq)


def evaluate_sequence(root: Path, seq: str) -> SequenceResult:
    seq = f"{_seq_int(seq):03d}"
    session = parse_session(root / f"session_{seq}.txt")
    duration_ms = int(session.get("duration_ms", "0") or "0")
    counts, spans = scan_csv(root / f"ecg_{seq}.csv")
    mic_s = wav_duration_s(root / f"mic_{seq}.wav")
    session_mic_s = session_mic_duration_s(session)
    problems: list[str] = []

    if duration_ms <= 0:
        problems.append("SESSION duration is zero or missing")

    expected_span = duration_ms * MIN_COVERAGE_RATIO
    for kind in ("ECG", "PPG", "IMU"):
        if counts.get(kind, 0) <= 0:
            problems.append(f"{kind} has no CSV rows")
            continue
        if duration_ms > 1000 and spans.get(kind, 0) < expected_span:
            problems.append(
                f"{kind} span too short: {spans.get(kind, 0)} ms < {expected_span:.0f} ms"
            )

    mic_evidence_s = mic_s if mic_s > 0 else session_mic_s
    if mic_evidence_s <= 0:
        problems.append("MIC WAV has no audio duration")
    elif duration_ms > 1000:
        session_s = duration_ms / 1000.0
        ratio_error = abs(mic_evidence_s - session_s) / session_s
        session_mic_ratio_error = (
            abs(session_mic_s - session_s) / session_s if session_mic_s > 0 else None
        )
        if (
            ratio_error > MAX_MIC_DURATION_RATIO_ERROR
            and (
                session_mic_ratio_error is None
                or session_mic_ratio_error > MAX_MIC_DURATION_RATIO_ERROR
            )
        ):
            problems.append(
                f"MIC duration mismatch: wav={mic_s:.2f}s session={session_s:.2f}s"
            )
        elif ratio_error > MAX_MIC_DURATION_RATIO_ERROR and session_mic_s > 0:
            mic_evidence_s = session_mic_s

    return SequenceResult(
        seq=seq,
        ok=not problems,
        duration_ms=duration_ms,
        counts=counts,
        spans_ms=spans,
        mic_duration_s=mic_evidence_s,
        problems=problems,
    )


def find_sequences(root: Path) -> list[str]:
    seqs = []
    for path in root.glob("session_*.txt"):
        match = re.search(r"session_(\d+)\.txt$", path.name)
        if match:
            seqs.append(match.group(1))
    return sorted(seqs, key=int)


def print_result(result: SequenceResult) -> None:
    status = "PASS" if result.ok else "FAIL"
    print(f"{status} seq={result.seq} duration_ms={result.duration_ms}")
    print(f"  counts={result.counts}")
    print(f"  spans_ms={result.spans_ms}")
    print(f"  mic_duration_s={result.mic_duration_s:.2f}")
    for problem in result.problems:
        print(f"  - {problem}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate four-modal SD-card sessions")
    parser.add_argument("root", nargs="?", default="H:/", help="SD card root, default H:/")
    parser.add_argument("--seq", help="Only validate one sequence, e.g. 003")
    args = parser.parse_args()

    root = Path(args.root)
    if args.seq:
        sequences = [args.seq]
    else:
        sequences = find_sequences(root)

    if not sequences:
        print(f"No session_XXX.txt files found in {root}")
        return 2

    results = [evaluate_sequence(root, seq) for seq in sequences]
    for result in results:
        print_result(result)

    return 0 if all(r.ok for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
