import csv
import tempfile
import unittest
import wave
from pathlib import Path
from typing import Optional

from python.sd_four_modal_validator import evaluate_sequence


def write_session(root: Path, seq: int, duration_ms: int, ecg: int, ppg: int, imu: int, mic_bytes: int, mic_ms: Optional[int] = None) -> None:
    text = "\n".join([
        f"seq={seq}",
        f"ecg_file=0:/ecg_{seq:03d}.csv",
        f"mic_file=0:/mic_{seq:03d}.wav",
        f"log_file=0:/log_{seq:03d}.txt",
        f"duration_ms={duration_ms}",
        f"ecg_samples={ecg}",
        f"ppg_samples={ppg}",
        f"imu_samples={imu}",
        f"mic_bytes={mic_bytes}",
    ])
    if mic_ms is not None:
        text += f"\nmic_ms={mic_ms}"
    (root / f"session_{seq:03d}.txt").write_text(text + "\n", encoding="utf-8")


def write_csv(root: Path, seq: int, duration_ms: int, ecg_span: int, ppg_span: int, imu_span: int, ecg_rows: int = 5100) -> None:
    rows = [
        [0, "META_START", seq, 0, 0, 0, 0, 0, 0],
        [120, "PPG", 0, 1, 1, 0, 0, 0, 0],
        [120 + ppg_span, "PPG", 1, 1, 1, 0, 0, 0, 0],
        [130, "IMU", 0, 1, 1, 1, 1, 1, 1],
        [130 + imu_span, "IMU", 1, 1, 1, 1, 1, 1, 1],
    ]
    ecg_rows = max(2, ecg_rows)
    for i in range(ecg_rows):
        ts = 100 + (ecg_span * i // (ecg_rows - 1))
        rows.append([ts, "ECG", i, 1, 0, 0, 0, 0, 0])
    with (root / f"ecg_{seq:03d}.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["timestamp_ms", "type", "seq", "v1", "v2", "v3", "v4", "v5", "v6"])
        w.writerows(rows)


def write_wav(root: Path, seq: int, duration_s: float) -> None:
    rate = 16000
    frames = int(rate * duration_s)
    with wave.open(str(root / f"mic_{seq:03d}.wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b"\0\0" * frames)


def write_wav_with_rate_and_frames(root: Path, seq: int, rate: int, frames: int) -> None:
    with wave.open(str(root / f"mic_{seq:03d}.wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b"\0\0" * frames)


class FourModalValidatorTest(unittest.TestCase):
    def test_accepts_overlapping_four_modal_session(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            write_session(root, 1, duration_ms=10000, ecg=5100, ppg=125, imu=520, mic_bytes=320000)
            write_csv(root, 1, duration_ms=10000, ecg_span=9900, ppg_span=9800, imu_span=9800)
            write_wav(root, 1, duration_s=10.0)

            result = evaluate_sequence(root, "001")

            self.assertTrue(result.ok, result.problems)

    def test_rejects_ecg_only_long_session_with_short_other_modalities(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            write_session(root, 3, duration_ms=40_000_000, ecg=10_000_000, ppg=92, imu=382, mic_bytes=3_383_296)
            write_csv(root, 3, duration_ms=40_000_000, ecg_span=40_000_000, ppg_span=7_200, imu_span=7_300)
            write_wav(root, 3, duration_s=90.0)

            result = evaluate_sequence(root, "003")

            self.assertFalse(result.ok)
            self.assertTrue(any("PPG" in p for p in result.problems))
            self.assertTrue(any("IMU" in p for p in result.problems))
            self.assertTrue(any("MIC" in p for p in result.problems))

    def test_accepts_legacy_wav_header_when_session_mic_ms_matches(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            write_session(root, 4, duration_ms=600004, ecg=157140, ppg=7595, imu=31293,
                          mic_bytes=22405120, mic_ms=599207)
            write_csv(root, 4, duration_ms=600004, ecg_span=599996, ppg_span=599950,
                      imu_span=599985, ecg_rows=262000)
            write_wav_with_rate_and_frames(root, 4, rate=4360, frames=11202560)

            result = evaluate_sequence(root, "004")

            self.assertTrue(result.ok, result.problems)

    def test_rejects_four_modal_session_with_low_ecg_rate(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            write_session(root, 1, duration_ms=30004, ecg=2500, ppg=378, imu=1569, mic_bytes=1130496)
            write_csv(root, 1, duration_ms=30004, ecg_span=29993, ppg_span=29924,
                      imu_span=29979, ecg_rows=2500)
            write_wav(root, 1, duration_s=30.36)

            result = evaluate_sequence(root, "001")

            self.assertFalse(result.ok)
            self.assertTrue(any("ECG rate too low" in p for p in result.problems))


if __name__ == "__main__":
    unittest.main()
