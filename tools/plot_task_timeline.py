import argparse
import csv

import matplotlib.pyplot as plt


def load_rows(path):
    rows = []
    with open(path, "r", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                rows.append(
                    {
                        "task": row["task"].strip(),
                        "block": int(row["block"]),
                        "start_us": int(row["start_us"]),
                        "end_us": int(row["end_us"]),
                    }
                )
            except (KeyError, ValueError):
                continue
    return rows


def main():
    parser = argparse.ArgumentParser(description="Plot task timeline from timing CSV.")
    parser.add_argument("--input", default="timings.csv", help="CSV path from logger")
    parser.add_argument("--max-blocks", type=int, default=0, help="Plot only first N blocks (0 = all)")
    args = parser.parse_args()

    rows = load_rows(args.input)
    if not rows:
        print("No valid rows found.")
        return

    tasks_order = ["sampling", "filter", "fft", "comm"]
    y_map = {task: idx for idx, task in enumerate(tasks_order)}

    min_start = min(r["start_us"] for r in rows)

    if args.max_blocks > 0:
        rows = [r for r in rows if r["block"] < args.max_blocks]

    fig, ax = plt.subplots(figsize=(12, 6))

    for r in rows:
        task = r["task"]
        if task not in y_map:
            continue
        start_ms = (r["start_us"] - min_start) / 1000.0
        duration_ms = (r["end_us"] - r["start_us"]) / 1000.0
        ax.broken_barh([(start_ms, duration_ms)], (y_map[task] - 0.35, 0.7), alpha=0.8)

    ax.set_yticks(list(y_map.values()))
    ax.set_yticklabels(tasks_order)
    ax.set_xlabel("Time from first sample block [ms]")
    ax.set_ylabel("Task")
    ax.set_title("Task timeline per processed block")
    ax.grid(True, axis="x", alpha=0.3)
    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()
