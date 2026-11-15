#!/usr/bin/env python3
"""
Log the tuner's result stream (a "trace") from the STM32 board to a CSV file.

A trace is not audio. The board prints one line per pitch result, every 32 ms:

    T,<freq_hz>,<confidence>,<valid>,<dbfs>,<ovr>\\r\\n
    T,82.41,0.012,1,-41.3,0

The logger only listens. It never sends a byte, because any byte makes the
board re-initialise the pitch module.

Each trace is saved as data/traces/<timestamp>_<label>.csv with the columns
    t_s, freq_hz, confidence, valid, dbfs, ovr
(t_s = PC arrival time in seconds since the first line) and gets one row in
data/traces/manifest.csv.

Two checks run on every trace, and a failed one prints a warning (the file is
still saved, the manifest says what was wrong):
    - line count against the duration: the board sends 31.25 lines per second
    - the overrun counter must not change during the recording

Usage:
    python tools/trace.py --list-ports
    python tools/trace.py --port COM5 --label silence_room --seconds 30 --mic-cm 15
    python tools/trace.py --port COM5 --label E2_ringout_amp --instrument guitar \\
                          --mic-cm 15 --notes "amp, neck pickup"
    python tools/trace.py --port /dev/cu.SLAB_USBtoUART --label tune_E2_unpl_b

Without --seconds it records until Ctrl+C. Close any serial monitor first.
"""
import argparse
import csv
import math
import os
import re
import sys
import time
from datetime import datetime
from pathlib import Path

import serial
import serial.tools.list_ports

BAUD_RATE = 115200
LINE_TIMEOUT_S = 1.0            # timeout for reading one text line
FIRST_LINE_DEADLINE_S = 5.0     # max wait for the first trace line
LINE_PREFIX = "T"               # first field of a trace line
FIELD_COUNT = 6                 # prefix + freq, confidence, valid, dbfs, ovr

SAMPLE_RATE_HZ = 32000          # mic sample rate
BLOCK_SAMPLES = 1024            # MIC_BLOCK_SAMPLES: one result per block
LINES_PER_SECOND = SAMPLE_RATE_HZ / BLOCK_SAMPLES    # 31.25
LINE_COUNT_TOLERANCE = 2        # lines; covers USB buffering at both ends

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUT_DIR = REPO_ROOT / "data" / "traces"
MANIFEST_NAME = "manifest.csv"
MANIFEST_FIELDS = [
    "file", "time", "label", "lines", "duration_s", "expected_lines",
    "ovr_first", "ovr_last", "malformed", "problems",
    "instrument", "mic_cm", "notes",
]
CSV_FIELDS = ["t_s", "freq_hz", "confidence", "valid", "dbfs", "ovr"]


class TraceError(Exception):
    """Nothing usable arrived; nothing is saved."""


# ---------------------------------------------------------------- arguments

def parse_args():
    parser = argparse.ArgumentParser(
        description="Log the board's pitch result lines to a CSV trace.")
    parser.add_argument("--port",
                        help="serial port, e.g. COM5 or /dev/cu.SLAB_USBtoUART")
    parser.add_argument("--label", default="trace",
                        help="scenario name used in the file name, e.g. tune_E2_amp")
    parser.add_argument("--seconds", type=float, default=None,
                        help="stop after this many seconds (default: until Ctrl+C)")
    parser.add_argument("--instrument", default="",
                        help="e.g. guitar or bass (manifest column)")
    parser.add_argument("--mic-cm", type=float, default=None,
                        help="mic distance from the string or speaker in cm (manifest column)")
    parser.add_argument("--notes", default="",
                        help='free text for the manifest, e.g. "unplugged, soft then hard pluck"')
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR,
                        help=f"where traces and {MANIFEST_NAME} go (default: data/traces)")
    parser.add_argument("--quiet", action="store_true",
                        help="don't show the live reading while recording")
    parser.add_argument("--list-ports", action="store_true",
                        help="list serial ports and exit")
    args = parser.parse_args()
    if not args.list_ports and not args.port:
        parser.error("--port is required (run with --list-ports to see the available ports)")
    if args.seconds is not None and args.seconds <= 0:
        parser.error("--seconds must be positive")
    return args


def list_ports():
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("No serial ports found.")
    for port in ports:
        print(f"{port.device:28} {port.description}")


# ------------------------------------------------------------ serial helpers

def open_port(name):
    kwargs = {"baudrate": BAUD_RATE, "timeout": LINE_TIMEOUT_S}
    if os.name == "posix":
        # Refuse to share the port with another program (e.g. a serial monitor).
        # Windows ports are always exclusive.
        kwargs["exclusive"] = True
    return serial.Serial(name, **kwargs)


def parse_trace_line(line):
    """
    'T,82.41,0.012,1,-41.3,0' -> (82.41, 0.012, 1, -41.3, 0).
    None if the line is not a trace line; ValueError if it is one but broken.
    """
    parts = line.split(",")
    if parts[0] != LINE_PREFIX:
        return None
    if len(parts) != FIELD_COUNT:
        raise ValueError(f"expected {FIELD_COUNT} fields, got {len(parts)}")
    freq_hz = float(parts[1])
    confidence = float(parts[2])
    valid = int(parts[3])
    dbfs = float(parts[4])
    ovr = int(parts[5])
    if valid not in (0, 1) or ovr < 0:
        raise ValueError("valid must be 0 or 1, ovr must not be negative")
    return freq_hz, confidence, valid, dbfs, ovr


def record(port_name, seconds, quiet):
    """
    Read trace lines until the time is up or Ctrl+C.
    Returns (rows, malformed): rows are (t_s, freq_hz, confidence, valid, dbfs, ovr).
    """
    rows = []
    malformed = 0
    with open_port(port_name) as ser:
        ser.reset_input_buffer()
        ser.readline()      # the first line is usually cut in half: throw it away
        print(f"Listening on {port_name}"
              + (f" for {seconds:g} s" if seconds else ", Ctrl+C to stop") + "...")

        opened = time.monotonic()
        t_first = None
        try:
            while True:
                raw = ser.readline()
                now = time.monotonic()
                if t_first is None and now - opened > FIRST_LINE_DEADLINE_S:
                    raise TraceError(
                        f"no '{LINE_PREFIX},...' line within {FIRST_LINE_DEADLINE_S:.0f} s "
                        "(wrong port? board not running? firmware still prints the old line?)")
                if seconds is not None and t_first is not None and now - t_first >= seconds:
                    break
                line = raw.decode("ascii", errors="replace").strip()
                if not line:
                    continue
                try:
                    fields = parse_trace_line(line)
                except ValueError as e:
                    malformed += 1
                    print(f"\n  malformed line {line!r}: {e}", file=sys.stderr)
                    continue
                if fields is None:
                    print(f"\n  board: {line}")
                    continue
                if t_first is None:
                    t_first = now
                rows.append((now - t_first, *fields))
                if not quiet:
                    show_live(rows[-1])
        except KeyboardInterrupt:
            pass
    if not quiet:
        print()
    if not rows:
        raise TraceError("no trace lines received")
    return rows, malformed


def show_live(row):
    """One status line, overwritten in place."""
    t_s, freq_hz, confidence, valid, dbfs, ovr = row
    reading = f"{freq_hz:8.2f} Hz" if valid else "   --      "
    print(f"\r  {t_s:6.1f} s  {reading}  conf {confidence:5.3f}  "
          f"{dbfs:6.1f} dBFS  ovr {ovr}   ", end="", flush=True)


# ------------------------------------------------------------- check and save

def check_trace(rows):
    """Return (duration_s, expected_lines, problems). problems is a list of strings."""
    problems = []
    duration_s = rows[-1][0]                # first line is at t = 0
    # n lines span n - 1 intervals of 32 ms
    expected_lines = round(duration_s * LINES_PER_SECOND) + 1
    if abs(len(rows) - expected_lines) > LINE_COUNT_TOLERANCE:
        problems.append(f"line count {len(rows)}, expected {expected_lines}")
    ovr_first, ovr_last = rows[0][5], rows[-1][5]
    if ovr_last != ovr_first:
        problems.append(f"ovr went from {ovr_first} to {ovr_last}")
    # The pitch module reports NAN as the frequency of some invalid frames, on
    # purpose. That is fine; a valid frame must have a real frequency.
    if any(not math.isfinite(v) for row in rows for v in (row[2], row[4])):
        problems.append("nan or inf in confidence or level")
    if any(row[3] and not math.isfinite(row[1]) for row in rows):
        problems.append("valid frame with a nan or inf frequency")
    return duration_s, expected_lines, problems


def save_trace(out_dir, label, rows, malformed, instrument="", mic_cm=None, notes=""):
    out_dir.mkdir(parents=True, exist_ok=True)
    now = datetime.now()
    safe_label = re.sub(r"[^A-Za-z0-9_-]+", "_", label).strip("_") or "trace"
    csv_path = out_dir / f"{now:%Y%m%d_%H%M%S}_{safe_label}.csv"

    with csv_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(CSV_FIELDS)
        for t_s, freq_hz, confidence, valid, dbfs, ovr in rows:
            writer.writerow([f"{t_s:.3f}", f"{freq_hz:.2f}", f"{confidence:.3f}",
                             valid, f"{dbfs:.1f}", ovr])

    duration_s, expected_lines, problems = check_trace(rows)
    if malformed:
        problems.append(f"{malformed} malformed lines")

    manifest = out_dir / MANIFEST_NAME
    is_new = not manifest.exists()
    with manifest.open("a", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=MANIFEST_FIELDS)
        if is_new:
            writer.writeheader()
        writer.writerow({
            "file": csv_path.name,
            "time": now.isoformat(timespec="seconds"),
            "label": label,
            "lines": len(rows),
            "duration_s": f"{duration_s:.2f}",
            "expected_lines": expected_lines,
            "ovr_first": rows[0][5],
            "ovr_last": rows[-1][5],
            "malformed": malformed,
            "problems": "; ".join(problems),
            "instrument": instrument,
            "mic_cm": "" if mic_cm is None else f"{mic_cm:g}",
            "notes": notes,
        })
    return csv_path, duration_s, expected_lines, problems


# --------------------------------------------------------------------- main

def main():
    args = parse_args()
    if args.list_ports:
        list_ports()
        return 0

    try:
        rows, malformed = record(args.port, args.seconds, args.quiet)
    except TraceError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1
    except serial.SerialException as e:
        print(f"ERROR opening or using {args.port}: {e}\n"
              "Is the port name right, and is no serial monitor using it?",
              file=sys.stderr)
        return 1

    csv_path, duration_s, expected_lines, problems = save_trace(
        args.out_dir, args.label, rows, malformed,
        args.instrument, args.mic_cm, args.notes)
    try:
        shown = csv_path.relative_to(REPO_ROOT)
    except ValueError:
        shown = csv_path

    valid_count = sum(row[3] for row in rows)
    levels = [row[4] for row in rows]
    print(f"Saved {shown}")
    print(f"  {len(rows)} lines in {duration_s:.2f} s (expected {expected_lines}), "
          f"{valid_count} valid, level {min(levels):.1f} to {max(levels):.1f} dBFS, "
          f"ovr {rows[-1][5]}")
    for problem in problems:
        print(f"  WARNING: {problem}", file=sys.stderr)
    return 2 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
