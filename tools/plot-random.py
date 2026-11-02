#!/usr/bin/env python3
"""Plot link outages of resilience-example-random.

Usage: python3 plot-random.py <prefix>
Reads <prefix>-events.csv and <prefix>-summary.csv, writes <prefix>-outages.png.
"""

import csv
import sys

import matplotlib.pyplot as plt


def read_outages(events_file, end_s):
    """Return {link: [(start_s, end_s), ...]}. An outage still open at the end is cut at end_s."""
    outages = {}
    down_since = {}
    with open(events_file, newline="") as f:
        for row in csv.DictReader(f):
            # Both interfaces of a link send an event; keep only the first of each pair.
            link = row["detail"]
            time_s = float(row["time_s"])
            outages.setdefault(link, [])
            if row["event"] == "LinkDown" and link not in down_since:
                down_since[link] = time_s
            elif row["event"] == "RecoveryCompleted" and link in down_since:
                outages[link].append((down_since.pop(link), time_s))
    for link, start_s in down_since.items():
        outages[link].append((start_s, end_s))
    return outages


def read_last_summary(summary_file):
    """The summary file gets one row per run, so the last row belongs to this run."""
    with open(summary_file, newline="") as f:
        return list(csv.DictReader(f))[-1]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    prefix = sys.argv[1]

    summary = read_last_summary(prefix + "-summary.csv")
    end_s = float(summary["simulation_time_s"])
    outages = read_outages(prefix + "-events.csv", end_s)
    # A link that never failed has no events; add it so the plot shows every managed link.
    for index in range(int(summary["num_managed_links"])):
        outages.setdefault("link={}".format(index), [])
    links = sorted(outages, key=lambda name: int(name.split("=")[1]))

    fig, (timeline, count) = plt.subplots(
        2, 1, figsize=(10, 7), sharex=True, gridspec_kw={"height_ratios": [2, 1]}
    )

    for row, link in enumerate(links):
        spans = [(start, stop - start) for start, stop in outages[link]]
        timeline.broken_barh(spans, (row - 0.4, 0.8), color="tab:red")
    timeline.set_yticks(range(len(links)))
    timeline.set_yticklabels(links, fontsize=7)
    timeline.set_ylabel("Managed link")
    timeline.set_title(
        "Random link failures: availability {:.3f} (expected {:.3f}), mean outage {:.1f} s (expected {:.1f} s)".format(
            float(summary["measured_availability"]),
            float(summary["expected_availability"]),
            float(summary["mean_outage_s"]),
            float(summary["expected_mean_outage_s"]),
        ),
        fontsize=10,
    )

    changes = []
    for spans in outages.values():
        for start, stop in spans:
            changes.append((start, 1))
            changes.append((stop, -1))
    changes.sort()
    times, down = [0.0], [0]
    for time_s, delta in changes:
        times.append(time_s)
        down.append(down[-1] + delta)
    times.append(end_s)
    down.append(down[-1])
    count.step(times, down, where="post", color="tab:blue")
    count.set_ylabel("Links down")
    count.set_xlabel("Time (s)")
    count.set_xlim(0, end_s)
    count.grid(alpha=0.3)

    fig.tight_layout()
    fig.savefig(prefix + "-outages.png", dpi=120)
    print("Wrote " + prefix + "-outages.png")


if __name__ == "__main__":
    main()
