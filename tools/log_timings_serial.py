import argparse
import csv
import sys
import time

import serial


def parse_line(line):
    parts = [p.strip() for p in line.split(",")]
    if len(parts) != 5:
        return None
    task, block, start_us, end_us, duration_us = parts
    if task not in {"sampling", "filter", "fft", "comm"}:
        return None
    try:
        return {
            "task": task,
            "block": int(block),
            "start_us": int(start_us),
            "end_us": int(end_us),
            "duration_us": int(duration_us),
        }
    except ValueError:
        return None


def main():
    parser = argparse.ArgumentParser(description="Log task timing CSV from serial to file.")
    parser.add_argument("--port", required=True, help="Serial port, e.g. COM5")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate")
    parser.add_argument("--output", default="timings.csv", help="CSV output path")
    parser.add_argument("--duration", type=float, default=0.0, help="Seconds to log; 0 = infinite")
    args = parser.parse_args()

    start = time.time()
    rows = 0

    with serial.Serial(args.port, args.baud, timeout=1) as ser, open(args.output, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["task", "block", "start_us", "end_us", "duration_us"])
        writer.writeheader()

        print(f"Logging from {args.port} at {args.baud} baud -> {args.output}")
        print("Press Ctrl+C to stop.")

        try:
            while True:
                if args.duration > 0 and (time.time() - start) >= args.duration:
                    break

                raw = ser.readline()
                if not raw:
                    continue

                line = raw.decode(errors="ignore").strip()
                record = parse_line(line)
                if record is None:
                    continue

                writer.writerow(record)
                rows += 1
                if rows % 100 == 0:
                    f.flush()
                    print(f"Saved {rows} rows...")

        except KeyboardInterrupt:
            pass

        f.flush()

    print(f"Done. Saved {rows} rows to {args.output}")


if __name__ == "__main__":
    main()
