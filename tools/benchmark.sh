#!/usr/bin/env bash
# Arena Edit — benchmark harness (Linux CI): cold start (selftest), idle RSS,
# binary size, and the unit-test wall time. Mirrors tools/benchmark.ps1.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build}"
EXE="$BUILD/app/arenaedit"
RUNS="${RUNS:-5}"
[ -x "$EXE" ] || { echo "build first: cmake -B build -G Ninja && cmake --build build"; exit 1; }

echo "=== Arena Edit benchmark (Linux/CI) $(date -u +%Y-%m-%dT%H:%M:%SZ) ==="

total=0
for i in $(seq 1 "$RUNS"); do
    start=$(date +%s%N)
    QT_QPA_PLATFORM=offscreen "$EXE" --selftest > /dev/null 2>&1
    code=$?
    end=$(date +%s%N)
    ms=$(( (end - start) / 1000000 ))
    [ "$code" -eq 0 ] || { echo "selftest run $i failed"; exit 1; }
    total=$(( total + ms ))
done
avg=$(( total / RUNS ))
echo "cold start (selftest): avg ${avg} ms (target < 2000 ms)"

# Idle RSS: launch, sample VmRSS, exit.
QT_QPA_PLATFORM=offscreen "$EXE" &
pid=$!
sleep 3
rss_kb=$(awk '/VmRSS/{print $2}' /proc/$pid/status 2>/dev/null || echo 0)
mb=$(( rss_kb / 1024 ))
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
echo "idle RSS (empty project): ${mb} MB (target < 150 MB)"
echo "core binary size: $(du -h "$EXE" | cut -f1) (target < 60 MB before bundled FFmpeg)"

if [ -d "$BUILD" ]; then
    echo "--- unit tests ---"
    (cd "$BUILD" && ctest --output-on-failure 2>&1 | tail -4)
fi

# M9: export encode FPS through the real ExportQueue path (FFmpeg builds).
BENCH="$BUILD/tools/arena_export_bench"
FIXTURE="$ROOT/tests/fixtures/clip01.mp4"
if [ -x "$BENCH" ] && [ -f "$FIXTURE" ]; then
    echo "--- export encode FPS (real pipeline) ---"
    "$BENCH" "$FIXTURE" || echo "export benchmark failed (non-fatal for this stage)"
else
    echo "--- export encode FPS: skipped (FFmpeg build or fixture missing) ---"
fi
