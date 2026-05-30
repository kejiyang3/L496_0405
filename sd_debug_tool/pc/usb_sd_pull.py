#!/usr/bin/env python3
"""USB CDC SD-card debug client v2 with ACK/CRC, resume, progress, get-all."""
import argparse
import binascii
import os
import re
import sys
import time
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("pyserial is required: pip install pyserial", )
    raise

# 鈹€鈹€ Protocol regex 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
GET_BEGIN_RE = re.compile(r"^\[GET_BEGIN\s+(\S+)\s+(\d+)(?:\s+(\d+))?\]$")
GET_END_RE   = re.compile(r"^\[GET_END\s+(\S+)\s+(\d+)\]$")
HEX_CRC_RE   = re.compile(r"^\[HEX\s+([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]{4})\]$")
OLD_HEX_RE   = re.compile(r"^([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]+)$")
LS_FILE_RE   = re.compile(r"^FILE\s+(\d+)\s+(.+)$")
LS_DIR_RE    = re.compile(r"^DIR\s+(.+)$")

LOCK_FILE = os.path.join(os.environ.get("TEMP", os.path.expanduser("~")), "usb_sd_pull.lock")
PROGRESS_EVERY_LINES = 32

# 鈹€鈹€ CRC-16/XMODEM 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
def crc16_xmodem(data: bytes) -> int:
    crc = 0x0000
    for b in data:
        crc ^= (b << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc

# 鈹€鈹€ Port lock 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
def acquire_lock():
    """Create lock file. Wait up to 10s if locked by another process."""
    for _ in range(20):
        try:
            if os.path.exists(LOCK_FILE):
                with open(LOCK_FILE, "r") as f:
                    pid = f.read().strip()
                try:
                    os.kill(int(pid), 0)
                except (OSError, ValueError, ProcessLookupError):
                    os.remove(LOCK_FILE)
                    continue
                print("Waiting for COM port to be released...", )
                time.sleep(0.5)
                continue
            with open(LOCK_FILE, "w") as f:
                f.write(str(os.getpid()))
            return
        except OSError:
            time.sleep(0.5)
    raise SystemExit("Could not acquire COM port lock after 10 seconds.")

def release_lock():
    try:
        if os.path.exists(LOCK_FILE):
            os.remove(LOCK_FILE)
    except OSError:
        pass

# 鈹€鈹€ Serial helpers 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
def auto_port():
    ports = list(list_ports.comports())
    if not ports:
        raise SystemExit("No serial ports found")
    cdc = [p.device for p in ports
           if any(k in (p.description or "").upper()
                  for k in ("STM", "USB", "VCP"))]
    return cdc[0] if cdc else ports[0].device

def open_serial(port, baudrate, timeout):
    acquire_lock()
    ser = serial.Serial(port, baudrate=baudrate, timeout=timeout, write_timeout=timeout)
    ser.dtr = True
    ser.rts = True
    time.sleep(0.2)
    ser.reset_input_buffer()
    return ser

def send_command(ser, command):
    ser.write((command.strip() + "\n").encode("ascii"))
    ser.flush()

def send_ack(ser):
    ser.write(b"ACK\n")
    ser.flush()

def send_nak(ser):
    ser.write(b"NAK\n")
    ser.flush()

def read_line(ser, deadline):
    line = ser.readline()
    if line:
        return line.decode("utf-8", errors="replace").rstrip("\r\n")
    if time.monotonic() > deadline:
        raise TimeoutError("Timed out waiting for board response")
    return None

# 鈹€鈹€ Simple commands 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
def print_response(ser, command, timeout):
    send_command(ser, command)
    deadline = time.monotonic() + timeout
    while True:
        line = read_line(ser, deadline)
        if line is None:
            continue
        print(line, flush=True)
        if line.startswith("[END ") or line.startswith("[ERR "):
            if line.startswith("[ERR "):
                continue
            return

# 鈹€鈹€ Progress bar 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
def progress_bar(current, total, width=40):
    if total == 0:
        return
    pct = current / total
    filled = int(width * pct)
    bar = "#" * filled + "." * (width - filled)
    kb_current = current / 1024.0
    kb_total = total / 1024.0
    print(f"\r[{bar}] {kb_current:.1f} KB / {kb_total:.1f} KB ({pct*100:.1f}%)",
          end="", )
    sys.stderr.flush()

def progress_done(total):
    kb_total = total / 1024.0
    print(f"\r[{'#'*40}] {kb_total:.1f} KB / {kb_total:.1f} KB (100.0%)",
          )

# 鈹€鈹€ GET with ACK/CRC / resume 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
def get_file(ser, filename, output, timeout, resume_offset=0, max_retries=3):
    for attempt in range(max_retries):
        try:
            _get_file_once(ser, filename, output, timeout, resume_offset)
            return
        except (TimeoutError, SystemExit) as e:
            if attempt < max_retries - 1:
                # Record how many bytes we got for resume
                partial = _last_received_bytes
                print(f"\nTransfer interrupted after {partial} bytes. "
                      f"Retry {attempt+2}/{max_retries} from offset {partial}...",
                      )
                time.sleep(1)
                ser.reset_input_buffer()
                resume_offset = partial
            else:
                raise

_last_received_bytes = 0

def _get_file_once(ser, filename, output, timeout, resume_offset=0):
    global _last_received_bytes
    if resume_offset > 0:
        send_command(ser, f"GET {filename} {resume_offset}")
    else:
        send_command(ser, f"GET {filename}")

    deadline = time.monotonic() + timeout
    begin_name = None
    expected_size = None
    actual_offset = None
    data = bytearray()

    # Wait for GET_BEGIN
    while True:
        line = read_line(ser, deadline)
        if line is None:
            continue
        print(line)
        m = GET_BEGIN_RE.match(line)
        if m:
            begin_name = m.group(1)
            expected_size = int(m.group(2))
            actual_offset = int(m.group(3)) if m.group(3) else 0
            if resume_offset > 0:
                print(f"Resuming from offset {actual_offset} (requested {resume_offset})",
                      )
            break
        if line.startswith("[ERR "):
            raise SystemExit(line)

    if begin_name is None:
        raise SystemExit("No GET_BEGIN received")

    line_count = 0
    while True:
        line = read_line(ser, deadline)
        if line is None:
            continue

        # Check for CRC hex line (new protocol)
        m = HEX_CRC_RE.match(line)
        if m:
            offset = int(m.group(1), 16)
            hex_data = m.group(2)
            expected_crc = int(m.group(3), 16)
            chunk = binascii.unhexlify(hex_data)

            # Verify CRC
            actual_crc = crc16_xmodem(chunk)
            if actual_crc != expected_crc:
                print(f"\nCRC mismatch at offset {offset}: "
                      f"got {actual_crc:04X}, expected {expected_crc:04X}",
                      )
                send_nak(ser)
                continue

            if offset != len(data):
                print(f"\nOffset mismatch at {offset}: expected {len(data)}",
                      )
                send_nak(ser)
                continue

            data.extend(chunk)
            send_ack(ser)
            deadline = time.monotonic() + timeout
            line_count += 1

            if line_count % PROGRESS_EVERY_LINES == 0 and expected_size > 0:
                progress_bar(len(data), expected_size)
            continue

        # Check for old-style hex line (backward compat)
        m = OLD_HEX_RE.match(line)
        if m:
            offset = int(m.group(1), 16)
            chunk = binascii.unhexlify(m.group(2))
            if offset != len(data):
                print(f"\nOffset mismatch at {offset}: expected {len(data)}",
                      )
                continue
            data.extend(chunk)
            deadline = time.monotonic() + timeout
            line_count += 1
            continue

        # GET_END
        m = GET_END_RE.match(line)
        if m:
            end_name = m.group(1)
            sent_size = int(m.group(2))
            send_ack(ser)  # ACK the GET_END

            if expected_size > 0:
                progress_done(expected_size)

            print(line)
            if begin_name and end_name != begin_name:
                raise SystemExit(f"Name mismatch: begin={begin_name}, end={end_name}")
            if expected_size is not None and expected_size != len(data):
                raise SystemExit(
                    f"Size mismatch: expected={expected_size}, got={len(data)}")
            if sent_size != len(data):
                raise SystemExit(
                    f"Sent mismatch: board={sent_size}, got={len(data)}")

            output_path = Path(output) if output else Path(filename).name
            output_path.write_bytes(data)
            print(f"Saved {len(data)} bytes to {output_path}")
            return

        if line.startswith("[ERR "):
            print(line)
            _last_received_bytes = len(data)
            raise SystemExit(line)

        # Print other lines (like old protocol hex lines)
        print(line)

    _last_received_bytes = len(data)

# 鈹€鈹€ get-all 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
def get_all_files(ser, output_dir, timeout):
    """List files, then download each one with resume support."""
    out_path = Path(output_dir)
    out_path.mkdir(parents=True, exist_ok=True)

    # Do LS
    send_command(ser, "LS")
    deadline = time.monotonic() + timeout
    files = []
    while True:
        line = read_line(ser, deadline)
        if line is None:
            continue
        if line.startswith("[END LS]"):
            break
        if line.startswith("[ERR "):
            raise SystemExit(line)
        m = LS_FILE_RE.match(line)
        if m:
            fsize = int(m.group(1))
            fname = m.group(2).strip()
            # Skip system directories
            if fname.lower() == "system volume information":
                continue
            files.append((fname, fsize))
            print(f"  {fname}  ({fsize} bytes)")
        m_dir = LS_DIR_RE.match(line)
        if m_dir:
            dname = m_dir.group(1).strip()
            if dname.lower() != "system volume information":
                print(f"  [DIR] {dname}  (skipped)")

    if not files:
        print("No files to download.")
        return

    print(f"\nDownloading {len(files)} file(s) to {output_dir}/\n")

    for fname, fsize in files:
        dest = out_path / fname
        est_timeout = max(timeout, fsize / 1024.0 + 5.0)
        print(f"--- {fname} ({fsize} bytes) ---")
        try:
            get_file(ser, fname, str(dest), est_timeout, resume_offset=0, max_retries=3)
        except SystemExit as e:
            print(f"  FAILED: {e}", )

# 鈹€鈹€ CLI 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
def main():
    parser = argparse.ArgumentParser(description="USB CDC SD-card debug client v2")
    parser.add_argument("--port", default=None, help="COM port")
    parser.add_argument("--baudrate", type=int, default=115200)
    parser.add_argument("--timeout", "-t", type=float, default=10.0,
                        help="Command timeout in seconds")
    sub = parser.add_subparsers(dest="cmd", required=True)

    sub.add_parser("ping")
    sub.add_parser("list")
    sub.add_parser("clear")

    cat_p = sub.add_parser("cat")
    cat_p.add_argument("filename")

    info_p = sub.add_parser("info")
    info_p.add_argument("filename")

    get_p = sub.add_parser("get")
    get_p.add_argument("filename")
    get_p.add_argument("-o", "--output", default=None)
    get_p.add_argument("--resume", type=int, default=0,
                       help="Resume from byte offset (manual)")

    all_p = sub.add_parser("get-all")
    all_p.add_argument("-o", "--output", default="sd_pull", help="Output directory")

    args = parser.parse_args()

    port = args.port or auto_port()
    try:
        with open_serial(port, args.baudrate, args.timeout) as ser:
            if args.cmd == "ping":
                print_response(ser, "PING", args.timeout)
            elif args.cmd == "list":
                print_response(ser, "LS", args.timeout)
            elif args.cmd == "clear":
                print_response(ser, "CLEAR", max(args.timeout, 30.0))
            elif args.cmd == "cat":
                print_response(ser, f"CAT {args.filename}", args.timeout)
            elif args.cmd == "info":
                print_response(ser, f"INFO {args.filename}", args.timeout)
            elif args.cmd == "get":
                get_file(ser, args.filename, args.output, args.timeout,
                         resume_offset=args.resume)
            elif args.cmd == "get-all":
                get_all_files(ser, args.output, args.timeout)
    finally:
        release_lock()


if __name__ == "__main__":
    main()