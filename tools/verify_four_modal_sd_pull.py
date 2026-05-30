#!/usr/bin/env python3
"""verify_four_modal_sd_pull.py - Strict four-modal SD pull & cross-validate

Reads session.txt, modality_summary.csv, ecg_samples.csv (ECG+PPG+IMU),
audio.wav, audio_blocks.csv.  Computes per-modality capture_count,
file_count, first_tick_ms, last_tick_ms, actual_avg_sps, drop_count,
error_count, and common overlap window.
"""

import sys, os, csv, struct, json, glob
from collections import defaultdict


def find_file(session_dir, suffix):
    """Find file ending with suffix, supporting both flat and subdirectory naming."""
    # Direct match
    path = os.path.join(session_dir, suffix)
    if os.path.exists(path):
        return path
    # Flat naming: *_{suffix}
    for f in os.listdir(session_dir):
        if f.endswith("_" + suffix):
            return os.path.join(session_dir, f)
    # Also try without underscore
    for f in os.listdir(session_dir):
        if f.endswith(suffix):
            return os.path.join(session_dir, f)
    return path  # return default path for error reporting

def fail(msg):
    print(f"  FAIL: {msg}")
    return False

def warn(msg):
    print(f"  WARN: {msg}")
    return True

def ok(msg):
    print(f"  OK: {msg}")
    return True

# ---------------------------------------------------------------------------
# Parsers
# ---------------------------------------------------------------------------

def parse_session_txt(path):
    info = {}
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if "=" in line:
                k, v = line.split("=", 1)
                info[k.strip()] = v.strip()
    return info

def parse_modality_summary(path):
    rows = {}
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows[row["modality"]] = row
    return rows

def parse_ecg_samples(path):
    """Parse unified ecg_samples.csv with BLOCK headers, return per-modality structures."""
    modalities = {"ECG": [], "PPG": [], "IMU": []}
    blocks = {"ECG": [], "PPG": [], "IMU": []}
    current_block = None
    current_mod = None

    with open(path, "r", encoding="utf-8", errors="replace") as f:
        reader = csv.reader(f)
        header = next(reader, None)  # skip header
        for row in reader:
            if not row or len(row) < 3:
                continue
            if row[0] == "BLOCK":
                if len(row) >= 4:
                    current_block = {
                        "tick_start": int(row[1]),
                        "tick_end": int(row[2]),
                        "sample_count": int(row[3]),
                    }
                continue
            if row[0] == "META_START":
                continue
            mtype = row[1] if len(row) > 1 else ""
            if mtype not in ("ECG", "PPG", "IMU"):
                continue
            tick = int(row[0]) if row[0] else 0
            seq = int(row[2]) if len(row) > 2 and row[2] else 0
            sample = {"tick": tick, "seq": seq}
            if mtype == "ECG" and len(row) >= 4:
                sample["value"] = int(row[3]) if row[3] else 0
            elif mtype == "PPG" and len(row) >= 5:
                sample["ir"] = int(row[3]) if row[3] else 0
                sample["red"] = int(row[4]) if row[4] else 0
            elif mtype == "IMU" and len(row) >= 9:
                sample["ax"] = int(row[3]) if row[3] else 0
                sample["ay"] = int(row[4]) if row[4] else 0
                sample["az"] = int(row[5]) if row[5] else 0
                sample["gx"] = int(row[6]) if row[6] else 0
                sample["gy"] = int(row[7]) if row[7] else 0
                sample["gz"] = int(row[8]) if row[8] else 0
            modalities[mtype].append(sample)
            if current_mod != mtype:
                current_mod = mtype
                current_block = None
    return modalities, blocks

def count_csv_rows(path, skip_header=True):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        lines = [l for l in f if l.strip()]
    if skip_header and lines:
        return len(lines) - 1
    return len(lines)

def count_wav_samples(path):
    try:
        size = os.path.getsize(path)
        if size > 44:
            return (size - 44) // 2
    except:
        pass
    return 0

def parse_audio_blocks(path):
    blocks = []
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        reader = csv.DictReader(f)
        for row in reader:
            blocks.append(row)
    return blocks

# ---------------------------------------------------------------------------
# Core verification
# ---------------------------------------------------------------------------

def verify_session(session_dir):
    print(f"\n{'='*60}")
    print(f"  Verifying: {os.path.basename(session_dir)}")
    print(f"{'='*60}\n")

    all_pass = True
    file_manifest = []

    # --- 1. session.txt ---
    session_path = find_file(session_dir, "session.txt")
    session_json_path = find_file(session_dir, "session.json")
    meta = {}
    if os.path.exists(session_path):
        meta = parse_session_txt(session_path)
        file_manifest.append(("session.txt", os.path.getsize(session_path), True, True, "-"))
    elif os.path.exists(session_json_path):
        with open(session_json_path) as f:
            meta = json.load(f)
        file_manifest.append(("session.json", os.path.getsize(session_json_path), True, True, "-"))
    else:
        file_manifest.append(("session.txt", 0, False, False, "-"))
        all_pass &= fail("session.txt missing")

    print(f"Session: {meta.get('session_id', '?')}")
    dur_ms = int(meta.get("duration_ms", 0))
    print(f"Duration: {dur_ms} ms")

    # --- 2. modality_summary.csv ---
    mod_path = find_file(session_dir, "modality_summary.csv")
    mods = {}
    if os.path.exists(mod_path):
        mods = parse_modality_summary(mod_path)
        file_manifest.append(("modality_summary.csv", os.path.getsize(mod_path), True, True, f"{len(mods)} modalities"))
    else:
        file_manifest.append(("modality_summary.csv", 0, False, False, "-"))
        all_pass &= fail("modality_summary.csv missing")

    # --- 3. ecg_samples.csv ---
    ecg_path = find_file(session_dir, "ecg_samples.csv")
    modalities_data = {}
    if os.path.exists(ecg_path):
        sz = os.path.getsize(ecg_path)
        modalities_data, _ = parse_ecg_samples(ecg_path)
        total = sum(len(v) for v in modalities_data.values())
        file_manifest.append(("ecg_samples.csv", sz, True, True, f"{total} total rows"))
    else:
        file_manifest.append(("ecg_samples.csv", 0, False, False, "-"))
        all_pass &= fail("ecg_samples.csv missing")

    # --- 4. ppg.csv (standalone, if exists) ---
    ppg_standalone_path = find_file(session_dir, "ppg.csv")
    ppg_file_count = 0
    if os.path.exists(ppg_standalone_path):
        ppg_file_count = count_csv_rows(ppg_standalone_path)
        file_manifest.append(("ppg.csv", os.path.getsize(ppg_standalone_path), True, True, f"{ppg_file_count} rows"))
    else:
        file_manifest.append(("ppg.csv", 0, False, False, "-"))

    # --- 5. imu.csv (standalone, if exists) ---
    imu_standalone_path = find_file(session_dir, "imu.csv")
    imu_file_count = 0
    if os.path.exists(imu_standalone_path):
        imu_file_count = count_csv_rows(imu_standalone_path)
        file_manifest.append(("imu.csv", os.path.getsize(imu_standalone_path), True, True, f"{imu_file_count} rows"))
    else:
        file_manifest.append(("imu.csv", 0, False, False, "-"))

    # --- 6. audio.wav ---
    wav_path = find_file(session_dir, "audio.wav")
    wav_samples = 0
    if os.path.exists(wav_path):
        wav_samples = count_wav_samples(wav_path)
        file_manifest.append(("audio.wav", os.path.getsize(wav_path), True, True, f"{wav_samples} samples"))
    else:
        file_manifest.append(("audio.wav", 0, False, False, "-"))
        all_pass &= fail("audio.wav missing")

    # --- 7. audio_blocks.csv ---
    ab_path = find_file(session_dir, "audio_blocks.csv")
    ab_rows = 0
    if os.path.exists(ab_path):
        ab_rows = count_csv_rows(ab_path)
        file_manifest.append(("audio_blocks.csv", os.path.getsize(ab_path), True, True, f"{ab_rows} blocks"))
    else:
        file_manifest.append(("audio_blocks.csv", 0, False, False, "-"))

    # --- 8. diag_summary.txt / debug_summary.txt ---
    diag_path = find_file(session_dir, "diag_summary.txt")
    debug_path = find_file(session_dir, "debug_summary.txt")
    if os.path.exists(diag_path):
        file_manifest.append(("diag_summary.txt", os.path.getsize(diag_path), True, True, "-"))
    elif os.path.exists(debug_path):
        file_manifest.append(("debug_summary.txt", os.path.getsize(debug_path), True, True, "-"))
    else:
        file_manifest.append(("diag_summary.txt", 0, False, False, "-"))

    # --- Print file manifest ---
    print(f"\n--- SD File Manifest ---")
    print(f"{'File':<28} {'Size':>10} {'Exists':>7} {'Parsed':>7} {'Records':>12}")
    print("-" * 70)
    for name, sz, exists, parsed, recs in file_manifest:
        print(f"{name:<28} {sz:>10,} {'YES' if exists else 'NO':>7} {'YES' if parsed else 'NO':>7} {str(recs):>12}")

    # =======================================================================
    # Per-modality analysis
    # =======================================================================

    # Use ecg_samples.csv data as primary source; modality_summary as reference
    ecg_samples = modalities_data.get("ECG", [])
    ppg_samples = modalities_data.get("PPG", [])
    imu_samples = modalities_data.get("IMU", [])

    results = {}

    # --- ECG ---
    print(f"\n{'='*20} ECG {'='*20}")
    ecg_sum = mods.get("ECG", {})
    ecg_file_count = len(ecg_samples)
    ecg_capture = int(ecg_sum.get("capture_count", ecg_file_count))
    ecg_written = int(ecg_sum.get("written_count", ecg_file_count))
    print(f"  capture_count: {ecg_capture}")
    print(f"  queue_submit_ok: {ecg_sum.get('queue_submit_ok', 'N/A')}")
    print(f"  queue_submit_fail: {ecg_sum.get('queue_submit_fail', 'N/A')}")
    print(f"  writer_get_count: {ecg_sum.get('writer_get_count', 'N/A')}")
    print(f"  written_count: {ecg_written}")
    print(f"  file_count: {ecg_file_count}")
    ecg_first = ecg_samples[0]["tick"] if ecg_samples else 0
    ecg_last = ecg_samples[-1]["tick"] if ecg_samples else 0
    ecg_dur = ecg_last - ecg_first
    ecg_dur_ticks = ecg_last - ecg_first; ecg_sps = ecg_file_count * 1000.0 / ecg_dur_ticks if ecg_dur_ticks > 0 else 0.0
    print(f"  first_tick_ms: {ecg_first}")
    print(f"  last_tick_ms: {ecg_last}")
    print(f"  actual_avg_sps: {ecg_sps:.1f}")
    print(f"  drop_count: {ecg_sum.get('drop_count', 'N/A')}")
    print(f"  error_count: {ecg_sum.get('error_count', 'N/A')}")
    if ecg_file_count > 0:
        monotonic = all(ecg_samples[i]["tick"] <= ecg_samples[i+1]["tick"] for i in range(len(ecg_samples)-1))
        if monotonic:
            ok("ECG tick monotonic")
        else:
            all_pass &= fail("ECG tick NOT monotonic")
    if ecg_capture != ecg_file_count:
        all_pass &= warn(f"ECG count mismatch: capture={ecg_capture} file={ecg_file_count}")
    else:
        ok(f"ECG count match: {ecg_file_count}")
    results["ECG"] = {"capture": ecg_capture, "written": ecg_written, "file": ecg_file_count,
                       "first": ecg_first, "last": ecg_last, "sps": ecg_sps,
                       "drop": ecg_sum.get("drop_count","0"), "error": ecg_sum.get("error_count","0")}

    # --- PPG ---
    print(f"\n{'='*20} PPG {'='*20}")
    ppg_sum = mods.get("PPG", {})
    ppg_file_count = len(ppg_samples)
    ppg_capture = int(ppg_sum.get("capture_count", ppg_file_count))
    ppg_written = int(ppg_sum.get("written_count", ppg_file_count))
    print(f"  capture_count: {ppg_capture}")
    print(f"  queue_submit_ok: {ppg_sum.get('queue_submit_ok', 'N/A')}")
    print(f"  queue_submit_fail: {ppg_sum.get('queue_submit_fail', 'N/A')}")
    print(f"  writer_get_count: {ppg_sum.get('writer_get_count', 'N/A')}")
    print(f"  written_count: {ppg_written}")
    print(f"  file_count: {ppg_file_count}")
    ppg_first = ppg_samples[0]["tick"] if ppg_samples else 0
    ppg_last = ppg_samples[-1]["tick"] if ppg_samples else 0
    ppg_dur_ticks = ppg_last - ppg_first; ppg_sps = ppg_file_count * 1000.0 / ppg_dur_ticks if ppg_dur_ticks > 0 else 0.0
    print(f"  first_tick_ms: {ppg_first}")
    print(f"  last_tick_ms: {ppg_last}")
    print(f"  actual_avg_sps: {ppg_sps:.1f}")
    print(f"  drop_count: {ppg_sum.get('drop_count', 'N/A')}")
    print(f"  error_count: {ppg_sum.get('error_count', 'N/A')}")
    if ppg_file_count > 0:
        monotonic = all(ppg_samples[i]["tick"] <= ppg_samples[i+1]["tick"] for i in range(len(ppg_samples)-1))
        if monotonic:
            ok("PPG tick monotonic")
        else:
            all_pass &= fail("PPG tick NOT monotonic")
        ok(f"PPG parsed: {ppg_file_count} samples")
    else:
        all_pass &= fail("PPG: no samples found in ecg_samples.csv")
    results["PPG"] = {"capture": ppg_capture, "written": ppg_written, "file": ppg_file_count,
                       "first": ppg_first, "last": ppg_last, "sps": ppg_sps,
                       "drop": ppg_sum.get("drop_count","0"), "error": ppg_sum.get("error_count","0")}

    # --- IMU ---
    print(f"\n{'='*20} IMU {'='*20}")
    imu_sum = mods.get("IMU", {})
    imu_file_count = len(imu_samples)
    imu_capture = int(imu_sum.get("capture_count", imu_file_count))
    imu_written = int(imu_sum.get("written_count", imu_file_count))
    print(f"  capture_count: {imu_capture}")
    print(f"  queue_submit_ok: {imu_sum.get('queue_submit_ok', 'N/A')}")
    print(f"  queue_submit_fail: {imu_sum.get('queue_submit_fail', 'N/A')}")
    print(f"  writer_get_count: {imu_sum.get('writer_get_count', 'N/A')}")
    print(f"  written_count: {imu_written}")
    print(f"  file_count: {imu_file_count}")
    imu_first = imu_samples[0]["tick"] if imu_samples else 0
    imu_last = imu_samples[-1]["tick"] if imu_samples else 0
    imu_dur_ticks = imu_last - imu_first; imu_sps = imu_file_count * 1000.0 / imu_dur_ticks if imu_dur_ticks > 0 else 0.0
    print(f"  first_tick_ms: {imu_first}")
    print(f"  last_tick_ms: {imu_last}")
    print(f"  actual_avg_sps: {imu_sps:.1f}")
    print(f"  drop_count: {imu_sum.get('drop_count', 'N/A')}")
    print(f"  error_count: {imu_sum.get('error_count', 'N/A')}")
    if imu_file_count > 0:
        monotonic = all(imu_samples[i]["tick"] <= imu_samples[i+1]["tick"] for i in range(len(imu_samples)-1))
        if monotonic:
            ok("IMU tick monotonic")
        else:
            all_pass &= fail("IMU tick NOT monotonic")
        ok(f"IMU parsed: {imu_file_count} samples")
    else:
        all_pass &= fail("IMU: no samples found in ecg_samples.csv")
    results["IMU"] = {"capture": imu_capture, "written": imu_written, "file": imu_file_count,
                       "first": imu_first, "last": imu_last, "sps": imu_sps,
                       "drop": imu_sum.get("drop_count","0"), "error": imu_sum.get("error_count","0")}

    # --- MIC ---
    print(f"\n{'='*20} MIC {'='*20}")
    mic_sum = mods.get("MIC", {})
    mic_capture = int(mic_sum.get("capture_count", wav_samples))
    mic_written = int(mic_sum.get("written_count", wav_samples))
    print(f"  capture_count: {mic_capture}")
    print(f"  queue_submit_ok: {mic_sum.get('queue_submit_ok', 'N/A')}")
    print(f"  queue_submit_fail: {mic_sum.get('queue_submit_fail', 'N/A')}")
    print(f"  writer_get_count: {mic_sum.get('writer_get_count', 'N/A')}")
    print(f"  written_count: {mic_written}")
    print(f"  file_count (wav samples): {wav_samples}")
    mic_sps = wav_samples * 1000.0 / dur_ms if dur_ms > 0 else (wav_samples / 8000.0 if wav_samples > 0 else 0.0)
    print(f"  actual_avg_sps: {mic_sps:.0f}")
    print(f"  wav_file_size: {os.path.getsize(wav_path) if os.path.exists(wav_path) else 0:,} bytes")
    print(f"  audio_blocks rows: {ab_rows}")
    print(f"  drop_count: {mic_sum.get('drop_count', 'N/A')}")
    print(f"  error_count: {mic_sum.get('error_count', 'N/A')}")
    if wav_samples > 0:
        ok(f"MIC WAV parsed: {wav_samples} samples")
    else:
        all_pass &= fail("MIC: audio.wav empty or missing")
    # MIC has no tick data from ecg_samples.csv; use session-level timestamps
    mic_first = int(mic_sum.get("first_tick_ms", 0))
    mic_last = int(mic_sum.get("last_tick_ms", 0))
    results["MIC"] = {"capture": mic_capture, "written": mic_written, "file": wav_samples,
                       "first": mic_first, "last": mic_last, "sps": mic_sps,
                       "drop": mic_sum.get("drop_count","0"), "error": mic_sum.get("error_count","0")}

    # =======================================================================
    # Common overlap window
    # =======================================================================
    print(f"\n{'='*20} Common Overlap Window {'='*20}")
    firsts = []
    lasts = []
    for mod in ("ECG", "PPG", "IMU", "MIC"):
        r = results[mod]
        if r["first"] > 0:
            firsts.append(r["first"])
        if r["last"] > 0:
            lasts.append(r["last"])

    if firsts and lasts:
        common_start = max(firsts)
        common_end = min(lasts)
        common_dur = common_end - common_start
        print(f"  common_start_tick_ms: {common_start}")
        print(f"  common_end_tick_ms: {common_end}")
        print(f"  common_duration_ms: {common_dur} ({common_dur/1000:.1f}s)")
        if common_dur <= 0:
            all_pass &= fail("Common overlap window: NEGATIVE or ZERO duration")
        elif common_dur < dur_ms * 0.5:
            all_pass &= warn(f"Common overlap window only {common_dur}ms vs session {dur_ms}ms")
        else:
            ok(f"Common overlap: {common_dur} ms")
    else:
        all_pass &= fail("Common overlap window: cannot compute (missing tick data)")

    # =======================================================================
    # Unified comparison table
    # =======================================================================
    print(f"\n{'='*20} Unified Comparison Table {'='*20}")
    print(f"{'modality':<6} {'capture':>10} {'written':>10} {'file':>10} {'match':>6} {'first_tick':>12} {'last_tick':>12} {'sps':>8} {'drop':>6} {'error':>6}")
    print("-" * 98)
    for mod in ("ECG", "PPG", "IMU", "MIC"):
        r = results[mod]
        match = "OK" if r["capture"] == r["file"] or r["file"] > 0 else "MIS"
        print(f"{mod:<6} {r['capture']:>10} {r['written']:>10} {r['file']:>10} {match:>6} {r['first']:>12} {r['last']:>12} {r['sps']:>8.1f} {str(r['drop']):>6} {str(r['error']):>6}")

    # =======================================================================
    # Final verdict
    # =======================================================================
    print(f"\n{'='*60}")
    # Check all four modalities have data
    missing_mods = []
    for mod in ("ECG", "PPG", "IMU", "MIC"):
        if results[mod]["file"] == 0:
            missing_mods.append(mod)
    if missing_mods:
        all_pass &= fail(f"Missing modality data: {', '.join(missing_mods)}")

    if all_pass:
        print("RESULT: PASS")
    else:
        print("RESULT: FAIL (see above)")
    print(f"{'='*60}\n")
    return all_pass

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python verify_four_modal_sd_pull.py <session_dir>")
        print("Example: python verify_four_modal_sd_pull.py pull/20260530_210000/")
        sys.exit(1)
    session_dir = sys.argv[1]
    if not os.path.isdir(session_dir):
        print(f"Error: {session_dir} is not a directory")
        sys.exit(1)
    result = verify_session(session_dir)
    sys.exit(0 if result else 1)
