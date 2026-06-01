import tempfile
import unittest
import wave
from pathlib import Path

from tools.verify_session_integrity import verify_session


class VerifySessionIntegrityTest(unittest.TestCase):
    def test_reports_session_csv_and_wav_integrity(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "session.txt").write_text(
                "\n".join(
                    [
                        "session_id=20260530_210000_T00001000",
                        "session_dir=/REC/20260530_210000_T00001000",
                        "requested_duration_ms=120000",
                        "duration_ms=120000",
                        "mic_status=MIC_OK",
                    ]
                )
                + "\n",
                encoding="utf-8",
            )
            (root / "ecg_samples.csv").write_text(
                "\n".join(
                    [
                        "timestamp_ms,type,seq,v1,v2,v3,v4,v5,v6",
                        "1000,ECG,0,1,0,0,0,0,0",
                        "1010,PPG,0,1,2,0,0,0,0",
                        "1020,IMU,0,1,2,3,4,5,6",
                        "121000,ECG,1,1,0,0,0,0,0",
                        "121000,PPG,1,1,2,0,0,0,0",
                        "121000,IMU,1,1,2,3,4,5,6",
                    ]
                )
                + "\n",
                encoding="utf-8",
            )
            with wave.open(str(root / "audio.wav"), "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(8000)
                w.writeframes(b"\x00\x00" * 16000)

            result = verify_session(root)

            self.assertEqual(result["session_id"], "20260530_210000_T00001000")
            self.assertEqual(result["requested_duration_ms"], 120000)
            self.assertEqual(result["actual_duration_ms"], 120000)
            self.assertEqual(result["csv_first_tick"], 1000)
            self.assertEqual(result["csv_last_tick"], 121000)
            self.assertEqual(result["csv_duration_ms"], 120000)
            self.assertEqual(result["ecg_file_count"], 2)
            self.assertEqual(result["ppg_file_count"], 2)
            self.assertEqual(result["icm_file_count"], 2)
            self.assertTrue(result["wav_exists"])
            self.assertEqual(result["wav_sample_rate"], 8000)
            self.assertEqual(result["wav_data_chunk_size"], 32000)
            self.assertEqual(result["mic_status"], "MIC_OK")
            self.assertTrue(result["core_pass_candidate"])
            self.assertTrue(result["mic_status_candidate"])

    def test_flags_missing_wav(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "session.txt").write_text(
                "session_id=s\nrequested_duration_ms=3600000\nduration_ms=94000\n",
                encoding="utf-8",
            )
            (root / "ecg_samples.csv").write_text(
                "timestamp_ms,type,seq,v1,v2,v3,v4,v5,v6\n1000,ECG,0,1,0,0,0,0,0\n",
                encoding="utf-8",
            )

            result = verify_session(root)

            self.assertFalse(result["wav_exists"])
            self.assertEqual(result["mic_status"], "MIC_NO_WAV")
            self.assertFalse(result["mic_status_candidate"])


if __name__ == "__main__":
    unittest.main()
