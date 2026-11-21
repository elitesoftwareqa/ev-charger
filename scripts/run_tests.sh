#!/usr/bin/env bash
# Configure, build and test the project from the command line (no VS Code needed).
#
#   scripts/run_tests.sh [options]
#
# Options:
#   -g, --group NAME    run only one test group (battery, charger, payment, app,
#                       database, rest, payter_master, app_master)
#   -r, --regex REGEX   run only tests whose name matches REGEX (ctest -R)
#   -d, --demo          also run the demo program after the tests pass
#   -c, --clean         delete the build folder first (full rebuild)
#   -b, --build-dir D   build folder (default: build-cli, separate from VS Code's build/)
#   -j, --jobs N        number of parallel jobs (default: all cores)
#   -l, --list          list the tests instead of running them
#   -h, --help          show this help
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$root/build-cli"
group="" regex="" demo=0 clean=0 list=0
jobs="$(nproc 2>/dev/null || echo 4)"

usage() { sed -n '2,15p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    -g|--group)     group="${2:?missing group name}"; shift 2 ;;
    -r|--regex)     regex="${2:?missing regex}"; shift 2 ;;
    -d|--demo)      demo=1; shift ;;
    -c|--clean)     clean=1; shift ;;
    -b|--build-dir) build="$(realpath -m "${2:?missing folder}")"; shift 2 ;;
    -j|--jobs)      jobs="${2:?missing number}"; shift 2 ;;
    -l|--list)      list=1; shift ;;
    -h|--help)      usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

valid_groups="battery charger payment app database rest payter_master app_master"
if [[ -n "$group" && " $valid_groups " != *" $group "* ]]; then
  echo "Unknown group '$group'. Valid groups: $valid_groups" >&2
  exit 2
fi

missing=()
for tool in cmake ninja g++; do
  command -v "$tool" >/dev/null || missing+=("$tool")
done
if ((${#missing[@]})); then
  echo "Missing tools: ${missing[*]}" >&2
  echo "Install them with: sudo apt-get install -y build-essential cmake ninja-build" >&2
  exit 1
fi

if ((clean)); then
  echo "==> Removing $build"
  rm -rf "$build"
fi

echo "==> Configuring ($build)"
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release

echo "==> Building"
cmake --build "$build" -j "$jobs"

ctest_args=(--test-dir "$build" --output-on-failure -j "$jobs")
[[ -n "$group" ]] && ctest_args+=(-R "^${group}\\.")
[[ -n "$regex" ]] && ctest_args+=(-R "$regex")

if ((list)); then
  ctest "${ctest_args[@]}" -N
  exit 0
fi

echo "==> Running tests"
ctest "${ctest_args[@]}"

if ((demo)); then
  echo "==> Running the demo (database: $build/demo-chargers.db)"
  "$build/ev_charger" "$build/demo-chargers.db"
fi
