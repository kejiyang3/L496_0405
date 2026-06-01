#!/usr/bin/env python3
"""Verify one pulled recorder session directory.

This is a PC-side integrity helper for later hardware validation agents. It
does not decide final experiment PASS; it surfaces the facts needed to notice
short sessions, missing WAV files, bad WAV headers, and CSV/count mismatches.
"""

from __future__ import annotations

import argparse
import csv
import json
import struct
from pathlib import Path
from typing import Any


CSV_TYPES = ("ECG", "PPG", "IMU")


def parse_key_value_file(path: Path) -> dict[str, str]:
    data: dict[str, str] = {}
    if not path.exists():
        return data
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        data[key.strip()] = value.strip()
    return data


def parse_csv(path: Path) -> dict[str, Any]:
    counts = {name: 0 for name in CSV_TYPES}
    first_tick: int | None = None
    last_tick: int | None = None

    if not path.exists():
        return {
            "csv_first_tick": 0,
            "csv_last_tick": 0,
            "csv_duration_ms": 0,
            "ecg_file_count": 0,
            "ppg_file_count": 0,
            "icm_file_count": 0,
            "ecg_actual_sps": 0.0,
            "ppg_actual_sps": 0.0,
            "icm_actual_sps": 0.0,
        }

    with path.open("r", encoding="utf-8", errors="replace", newline="") as f:
        reader = csv.reader(f)
        next(reader, None)
        for row in reader:
            if len(row) < 2 or row[0] == "BLOCK":
                continue
            try:
                tick = int(row[0])
            except ValueError:
                continue
            kind = row[1].strip()
            if kind not in counts:
                continue
            counts[kind] += 1
            if first_tick is None:
                first_tick = tick
            last_tick = tick

    first = first_tick or 0
    last = last_tick or 0
    duration = max(0, last - first)

    def sps(kind: str) -> float:
        return (counts[kind] * 1000.0 / duration) if duration > 0 else 0.0

    return {
        "csv_first_tick": first,
        "csv_last_tick": last,
        "csv_duration_ms": duration,
        "ecg_file_count": counts["ECG"],
        "ppg_file_count": counts["PPG"],
        "icm_file_count": counts["IMU"],
        "ecg_actual_sps": sps("ECG"),
        "ppg_actual_sps": sps("PPG"),
        "icm_actual_sps": sps("IMU"),
    }


def parse_wav(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {
            "wav_exists": False,
            "wav_size": 0,
            "wav_sample_rate": 0,
            "wav_data_chunk_size": 0,
            "wav_duration_s": 0.0,
        }

    blob = path.read_bytes()
    sample_rate = 0
    data_size = 0
    if len(blob) >= 44 and blob[0:4] == b"RIFF" and blob[8:12] == b"WAVE":
        sample_rate = struct.unpack_from("<I", blob, 24)[0]
        cursor = 12
        while cursor + 8 <= len(blob):
            chunk_id = blob[cursor : cursor + 4]
            chunk_size = struct.unpack_from("<I", blob, cursor + 4)[0]
            cursor += 8
            if chunk_id == b"data":
                data_size = chunk_size
                break
            cursor += chunk_size + (chunk_size & 1)

    duration = (data_size / 2.0 / sample_rate) if sample_rate > 0 else 0.0
    return {
        "wav_exists": True,
        "wav_size": len(blob),
        "wav_sample_rate": sample_rate,
        "wav_data_chunk_size": data_size,
        "wav_duration_s": duration,
    }


def as_int(value: str | None, default: int = 0) -> int:
    try:
        return int(value or "")
    except ValueError:
        return default


def verify_session(session_dir: str | Path) -> dict[str, Any]:
    root = Path(session_dir)
    session = parse_key_value_file(root / "session.txt")
    csv_info = parse_csv(root / "ecg_samples.csv")
    wav_info = parse_wav(root / "audio.wav")

    requested_ms = as_int(session.get("requested_duration_ms"))
    actual_ms = as_int(session.get("actual_duration_ms"), as_int(session.get("duration_ms")))

    mic_status = session.get("mic_status", "").strip()
    if not mic_status:
        mic_status = "MIC_OK" if wav_info["wav_exists"] else "MIC_NO_WAV"

    core_candidate = (
        csv_info["csv_duration_ms"] > 0
        and csv_info["ecg_file_count"] > 0
        and csv_info["ppg_file_count"] >= 0
        and csv_info["icm_file_count"] >= 0
    )
    mic_candidate = (
        wav_info["wav_exists"]
        and wav_info["wav_sample_rate"] == 8000
        and wav_info["wav_data_chunk_size"] > 0
        and mic_status not in ("MIC_NO_WAV", "MIC_WAV_HEADER_FAIL")
    )

    result: dict[str, Any] = {
        "session_id": session.get("session_id", ""),
        "requested_duration_ms": requested_ms,
        "actual_duration_ms": actual_ms,
        "mic_status": mic_status,
        "core_pass_candidate": core_candidate,
        "mic_status_candidate": mic_candidate,
    }
    result.update(csv_info)
    result.update(wav_info)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify one pulled session directory")
    parser.add_argument("session_dir", type=Path)
    parser.add_argument("--json", action="store_true", help="Print JSON instead of text")
    args = parser.parse_args()

    result = verify_session(args.session_dir)
    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        for key in (
            "session_id",
            "requested_duration_ms",
            "actual_duration_ms",
            "csv_first_tick",
            "csv_last_tick",
            "csv_duration_ms",
            "ecg_file_count",
            "ppg_file_count",
            "icm_file_count",
            "ecg_actual_sps",
            "ppg_actual_sps",
            "icm_actual_sps",
            "wav_exists",
            "wav_size",
            "wav_sample_rate",
            "wav_data_chunk_size",
            "wav_duration_s",
            "mic_status",
            "core_pass_candidate",
            "mic_status_candidate",
        ):
            value = result.get(key)
            if isinstance(value, float):
                print(f"{key}={value:.3f}")
            else:
                print(f"{key}={value}")

    return 0 if result["core_pass_candidate"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
