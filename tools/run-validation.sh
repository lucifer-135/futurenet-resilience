#!/usr/bin/env bash
# Runs validation cases V1-V3 (see doc/futurenet-resilience.md, section 6).
# Run from the ns-3 root:  bash contrib/futurenet-resilience/tools/run-validation.sh
set -euo pipefail

OUT=resilience-validation
mkdir -p "$OUT"

{
    echo "ns-3 version: $(head -n 1 VERSION 2>/dev/null || echo unknown)"
    echo "git commit:   $(git -C contrib/futurenet-resilience rev-parse HEAD 2>/dev/null || echo none)"
    echo "date:         $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} | tee "$OUT/environment.txt"

echo "== V1 scheduled outage with backup path"
./ns3 run "resilience-example-scheduled --outputPrefix=$OUT/v1-backup" | tee "$OUT/v1-backup.txt"

echo "== V1b scheduled outage without backup path"
./ns3 run "resilience-example-scheduled --enableBackup=false --outputPrefix=$OUT/v1-nobackup" |
    tee "$OUT/v1-nobackup.txt"

echo "== V2 larger mean outage (smaller recoveryProbability), 5 runs each"
for q in 0.2 0.1 0.05; do
    for run in 1 2 3 4 5; do
        ./ns3 run "resilience-example-random --recoveryProbability=$q --RngRun=$run --outputPrefix=$OUT/v2" >/dev/null
    done
done

echo "== V3 boundaries"
./ns3 run "resilience-example-random --failureProbability=0 --outputPrefix=$OUT/v3" >/dev/null
./ns3 run "resilience-example-random --failureProbability=1 --recoveryProbability=1 --outputPrefix=$OUT/v3" >/dev/null

echo "Done. Summaries: $OUT/v2-summary.csv $OUT/v3-summary.csv"
echo "Analyse:  python3 contrib/futurenet-resilience/tools/summarize-random.py $OUT/v2-summary.csv"
echo "Plot:     python3 contrib/futurenet-resilience/tools/plot-throughput.py $OUT/v1-backup"
