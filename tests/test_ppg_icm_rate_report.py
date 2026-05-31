import tempfile
import unittest
from pathlib import Path

from python.ppg_icm_rate_report import analyze_experiment


class PpgIcmRateReportTest(unittest.TestCase):
    def test_parses_csv_counts_ticks_rates_and_diag_lines(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            csv_path = root / "ecg_samples.csv"
            csv_path.write_text(
                "\n".join(
                    [
                        "timestamp_ms,type,seq,v1,v2,v3,v4,v5,v6",
                        "1000,PPG,0,11,12,0,0,0,0",
                        "1100,IMU,0,1,2,3,4,5,6",
                        "2000,PPG,1,11,12,0,0,0,0",
                        "2100,IMU,1,1,2,3,4,5,6",
                        "3100,IMU,2,1,2,3,4,5,6",
                    ]
                )
                + "\n",
                encoding="utf-8",
            )
            (root / "log.txt").write_text(
                "\n".join(
                    [
                        "RATE_ISO_CONFIG,case=2,label=PPG_ONLY_AVG1,ppg_fifo_cfg=0x1F,ppg_spo2=0x22,icm_accel_div=21,icm_gyro_div=21",
                        "RATE_ISO_PPG_REG,ppg_fifo_cfg=0x1F,ppg_mode=0x03,ppg_spo2=0x22",
                        "RATE_ISO_ICM_REG,icm_who=0xEA,icm_accel_div=21,icm_gyro_div=21",
                        "RATE_ISO_I2C,ppg_task_calls=20,imu_task_calls=10,ppg_mutex_wait_ms=4,ppg_mutex_hold_ms=8,imu_mutex_wait_ms=2,imu_mutex_hold_ms=6,i2c_errors=1",
                        "PPG_STATS,intwake=2,towake=3,todrain=1,read_ok=2,read_fail=0,empty=1,fifo_ov=0,samples=2,write_ok=2,write_fail=0,drop_blk=0",
                        "IMU_STATS,read_ok=3,read_fail=0,samples=3,write_ok=3,write_fail=0,drop_blk=0",
                    ]
                )
                + "\n",
                encoding="utf-8",
            )

            result = analyze_experiment(root)

            self.assertEqual(result.modalities["PPG"].file_count, 2)
            self.assertEqual(result.modalities["PPG"].first_tick, 1000)
            self.assertEqual(result.modalities["PPG"].last_tick, 2000)
            self.assertAlmostEqual(result.modalities["PPG"].actual_sps, 2.0)
            self.assertEqual(result.modalities["IMU"].file_count, 3)
            self.assertAlmostEqual(result.modalities["IMU"].actual_sps, 1.5)
            self.assertEqual(result.config["label"], "PPG_ONLY_AVG1")
            self.assertEqual(result.config["ppg_fifo_cfg"], "0x1F")
            self.assertEqual(result.config["icm_who"], "0xEA")
            self.assertEqual(result.diagnostics["ppg_task_calls"], 20)
            self.assertEqual(result.diagnostics["i2c_errors"], 1)


if __name__ == "__main__":
    unittest.main()
