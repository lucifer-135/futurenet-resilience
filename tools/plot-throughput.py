#!/usr/bin/env python3
"""Plot throughput of resilience-example-scheduled and shade link outages.

Usage: python3 plot-throughput.py <prefix>
Reads <prefix>-throughput.csv and <prefix>-events.csv, writes <prefix>-throughput.png.
"""

import csv
import sys

import matplotlib.pyplot as plt


def read_outages(events_file):
    """Return (start_s, end_s) pairs, one per failure of a link."""
    outages = []
    down_since = {}
    with open(events_file, newline="") as f:
        for row in csv.DictReader(f):
            link = row["detail"]
            time_s = float(row["time_s"])
            if row["event"] == "LinkDown" and link not in down_since:
                down_since[link] = time_s
            elif row["event"] == "RecoveryCompleted" and link in down_since:
                outages.append((down_since.pop(link), time_s))
    return outages


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    prefix = sys.argv[1]

    times, mbps = [], []
    with open(prefix + "-throughput.csv", newline="") as f:
        for row in csv.DictReader(f):
            times.append(float(row["time_s"]))
            mbps.append(float(row["throughput_mbps"]))

    fig, ax = plt.subplots(figsize=(8, 4))
    ax.plot(times, mbps, label="received throughput")
    for i, (start, end) in enumerate(read_outages(prefix + "-events.csv")):
        ax.axvspan(start, end, color="red", alpha=0.2, label="primary link down" if i == 0 else None)
    ax.set_xlabel("time (s)")
    ax.set_ylabel("throughput (Mbit/s)")
    ax.legend()
    fig.tight_layout()
    fig.savefig(prefix + "-throughput.png", dpi=150)
    print("wrote", prefix + "-throughput.png")


if __name__ == "__main__":
    main()
