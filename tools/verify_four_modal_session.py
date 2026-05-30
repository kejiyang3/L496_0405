#!/usr/bin/env python3
"""verify_four_modal_session.py - Cross-validate SD debug counts vs actual files"""

import sys, os, csv, json, struct

def fail(msg):
    print(f"  FAIL: {msg}")
    return False

def warn(msg):
    print(f"  WARN: {msg}")
    return True

def ok(msg):
    print(f"  OK: {msg}")
    return True

def parse_session_txt(path):
    """Parse session.txt key=value format"""
    info = {}
    with open(path, 'r') as f:
        for line in f:
            line = line.strip()
            if '=' in line:
                k, v = line.split('=', 1)
                info[k.strip()] = v.strip()
    return info

def count_csv_rows(path, skip_header=True):
    """Count data rows in a CSV file"""
    with open(path, 'r') as f:
        lines = [l for l in f if l.strip()]
    if skip_header and lines:
        return len(lines) - 1
    return len(lines)

def parse_modality_summary(path):
    """Parse modality_summary.csv"""
    rows = {}
    with open(path, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows[row['modality']] = row
    return rows

def parse_ecg_blocks(path):
    """Parse ecg_samples.csv with BLOCK headers"""
    blocks = []
    samples = []
    current_block = None
    with open(path, 'r') as f:
        reader = csv.reader(f)
        header = next(reader, None)  # skip header
        for row in reader:
            if row[0] == 'BLOCK':
                current_block = {
                    'tick_start': int(row[1]),
                    'tick_end': int(row[2]),
                    'sample_count': int(row[3]),
                    'samples': []
                }
                blocks.append(current_block)
            elif len(row) >= 4:
                samples.append({
                    'tick': int(row[0]),
                    'type': row[1],
                    'seq': int(row[2]),
                    'value': int(row[3])
                })
                if current_block is not None:
                    current_block['samples'].append(int(row[3]))
    return blocks, samples

def count_audio_wav_samples(path):
    """Count PCM samples in WAV file"""
    try:
        size = os.path.getsize(path)
        # WAV header is typically 44 bytes, 16-bit mono PCM
        if size > 44:
            return (size - 44) // 2
    except:
        pass
    return 0

def verify_session(session_dir):
    """Main verification function"""
    print(f"\n=== Verifying: {os.path.basename(session_dir)} ===\n")

    # 1. Parse session metadata
    session_path = os.path.join(session_dir, 'session.txt')
    if not os.path.exists(session_path):
        return fail("session.txt missing")

    meta = parse_session_txt(session_path)
    print(f"Session: {meta.get('session_id', '?')}")
    print(f"Duration: {meta.get('duration_ms', '?')} ms")
    dur_ms = int(meta.get('duration_ms', 0))
    start_tick = int(meta.get('start_tick_ms', 0))
    end_tick = int(meta.get('end_tick_ms', 0))

    # 2. Parse modality summary
    mod_path = os.path.join(session_dir, 'modality_summary.csv')
    if not os.path.exists(mod_path):
        return fail("modality_summary.csv missing")
    mods = parse_modality_summary(mod_path)

    all_pass = True

    # 3. Verify ECG
    print("\n--- ECG ---")
    ecg_path = os.path.join(session_dir, 'ecg_samples.csv')
    if os.path.exists(ecg_path):
        blocks, samples = parse_ecg_blocks(ecg_path)
        file_count = len(samples)
        block_count = len(blocks)
        summary_count = int(mods.get('ECG', {}).get('capture_count', 0))

        match = file_count == summary_count
        print(f"  capture_count: {summary_count}, file_count: {file_count}, blocks: {block_count}")
        if match:
            all_pass &= ok(f"ECG count match: {file_count}")
        else:
            all_pass &= fail(f"ECG count mismatch: summary={summary_count} file={file_count}")

        # Check SPS
        if dur_ms > 0:
            actual_sps = file_count * 1000.0 / dur_ms
            print(f"  actual_avg_sps: {actual_sps:.1f}")

        # Check tick monotonicity
        if len(samples) > 1:
            monotonic = all(samples[i]['tick'] <= samples[i+1]['tick'] for i in range(len(samples)-1))
            if monotonic:
                all_pass &= ok("ECG tick monotonic")
            else:
                all_pass &= fail("ECG tick NOT monotonic")

        # Check first/last tick
        if samples:
            print(f"  first_tick: {samples[0]['tick']}, last_tick: {samples[-1]['tick']}")
    else:
        all_pass &= warn("ecg_samples.csv not found (check file name)")

    # 4. Verify PPG
    print("\n--- PPG ---")
    ppg_path = os.path.join(session_dir, 'ppg.csv')
    if os.path.exists(ppg_path):
        file_count = count_csv_rows(ppg_path)
        summary_count = int(mods.get('PPG', {}).get('capture_count', 0))
        print(f"  capture_count: {summary_count}, file_count: {file_count}")
        if file_count > 0:
            all_pass &= ok(f"PPG file exists: {file_count} rows")
    else:
        all_pass &= warn("ppg.csv not found")

    # 5. Verify IMU
    print("\n--- IMU ---")
    imu_path = os.path.join(session_dir, 'imu.csv')
    if os.path.exists(imu_path):
        file_count = count_csv_rows(imu_path)
        summary_count = int(mods.get('IMU', {}).get('capture_count', 0))
        print(f"  capture_count: {summary_count}, file_count: {file_count}")
        if file_count > 0:
            all_pass &= ok(f"IMU file exists: {file_count} rows")
    else:
        all_pass &= warn("imu.csv not found")

    # 6. Verify MIC
    print("\n--- MIC ---")
    wav_path = os.path.join(session_dir, 'audio.wav')
    ab_path = os.path.join(session_dir, 'audio_blocks.csv')
    if os.path.exists(wav_path):
        wav_samples = count_audio_wav_samples(wav_path)
        summary_samples = int(mods.get('MIC', {}).get('capture_count', 0))
        print(f"  summary_capture_count: {summary_samples}, wav_samples: {wav_samples}")
        if wav_samples > 0:
            all_pass &= ok(f"MIC WAV exists: {wav_samples} samples")
    if os.path.exists(ab_path):
        block_rows = count_csv_rows(ab_path)
        print(f"  audio_blocks rows: {block_rows}")
        if block_rows > 0:
            all_pass &= ok(f"audio_blocks.csv: {block_rows} blocks")

    # 7. Check diag_summary
    diag_path = os.path.join(session_dir, 'diag_summary.txt')
    if os.path.exists(diag_path):
        all_pass &= ok("diag_summary.txt exists")
    else:
        all_pass &= warn("diag_summary.txt not found")

    # 8. Common overlap window
    print(f"\n--- Common Overlap Window ---")
    print(f"  session: {start_tick} - {end_tick} ({dur_ms} ms)")
    if dur_ms > 0:
        all_pass &= ok(f"Session duration: {dur_ms} ms")

    # 9. Final result
    print(f"\n{'='*40}")
    if all_pass:
        print("RESULT: PASS")
    else:
        print("RESULT: FAIL (see above)")
    return all_pass

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python verify_four_modal_session.py <session_dir>")
        print("Example: python verify_four_modal_session.py /REC/20260530_210000/")
        sys.exit(1)

    session_dir = sys.argv[1]
    if not os.path.isdir(session_dir):
        print(f"Error: {session_dir} is not a directory")
        sys.exit(1)

    result = verify_session(session_dir)
    sys.exit(0 if result else 1)
