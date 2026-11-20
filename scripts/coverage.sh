#!/usr/bin/env bash
# Builds with coverage instrumentation, runs every test, and writes a report.
#   scripts/coverage.sh [build-dir]        (default build dir: build-coverage)
# Needs gcovr:  pip install gcovr   (or: sudo apt-get install gcovr)
# Output: <build-dir>/coverage/index.html (open in a browser), coverage.xml,
# and a summary printed in the terminal.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${1:-$root/build-coverage}"

if ! command -v gcovr >/dev/null; then
  echo "gcovr not found. Install it with: pip install gcovr" >&2
  exit 1
fi

cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
cmake --build "$build"

# Start from zero so numbers from older runs are not mixed in.
find "$build" -name '*.gcda' -delete
ctest --test-dir "$build" --output-on-failure

mkdir -p "$build/coverage"
# Measure our library code only. src/main.cpp is the demo program and is
# deliberately left out. Add --fail-under-line N to fail below a threshold.
gcovr --root "$root" "$build" \
  --filter "$root/src/" --filter "$root/include/" \
  --exclude "$root/src/main.cpp" \
  --print-summary \
  --txt --html-details "$build/coverage/index.html" --xml "$build/coverage.xml" \
  "${@:2}"

echo
echo "HTML report: $build/coverage/index.html"
