#!/usr/bin/env python3
"""Average resilience-example-random results per parameter setting (validation V2).

Usage: python3 summarize-random.py <prefix>-summary.csv
"""

import csv
import statistics
import sys
from collections import defaultdict


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)

    groups = defaultdict(list)
    with open(sys.argv[1], newline="") as f:
        for row in csv.DictReader(f):
            key = (float(row["failure_probability"]), float(row["recovery_probability"]))
            groups[key].append(row)

    print("p      q      runs  mean_outage_s (expected)  availability (expected)  delivery_ratio")
    for (p, q), rows in sorted(groups.items()):
        outage = statistics.mean(float(r["mean_outage_s"]) for r in rows)
        availability = statistics.mean(float(r["measured_availability"]) for r in rows)
        delivery = statistics.mean(float(r["delivery_ratio"]) for r in rows)
        expected_outage = float(rows[0]["expected_mean_outage_s"])
        expected_availability = float(rows[0]["expected_availability"])
        print(
            f"{p:<6} {q:<6} {len(rows):<5} {outage:8.2f} ({expected_outage:6.2f})"
            f"         {availability:.4f} ({expected_availability:.4f})"
            f"          {delivery:.4f}"
        )


if __name__ == "__main__":
    main()
