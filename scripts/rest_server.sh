#!/usr/bin/env bash
# Build (if needed) and start the REST server in the foreground. Ctrl+C stops it.
#
#   scripts/rest_server.sh [options]
#
# Options:
#   -p, --port N       port to listen on (default: $EV_CHARGER_PORT or 8099)
#   -d, --db FILE      database file (default: /tmp/ev-charger.db)
#   -b, --build-dir D  build folder (default: build-cli)
#   -h, --help         show this help
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$root/build-cli"
port="${EV_CHARGER_PORT:-8099}"
db="/tmp/ev-charger.db"

usage() { sed -n '2,10p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    -p|--port)      port="${2:?missing port}"; shift 2 ;;
    -d|--db)        db="${2:?missing file}"; shift 2 ;;
    -b|--build-dir) build="$(realpath -m "${2:?missing folder}")"; shift 2 ;;
    -h|--help)      usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

missing=()
for tool in cmake ninja g++; do command -v "$tool" >/dev/null || missing+=("$tool"); done
if ((${#missing[@]})); then
  echo "Missing tools: ${missing[*]}" >&2
  echo "Install them with: sudo apt-get install -y build-essential cmake ninja-build" >&2
  exit 1
fi

if ss -ltn "sport = :$port" 2>/dev/null | grep -q LISTEN; then
  echo "Port $port is already in use. Pick another with --port, or stop the other server:" >&2
  echo "  pkill -f ev_charger_server" >&2
  exit 1
fi

echo "==> Building ev_charger_server ($build)"
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$build" --target ev_charger_server

echo "==> Starting. Web page: http://127.0.0.1:$port/   (database $db). Press Ctrl+C to stop."
exec "$build/ev_charger_server" "$db" "$port"
