#!/usr/bin/env bash
# One-command demo: starts a throwaway server, runs a full charge on every
# charger type through the REST client, then stops the server.
#
#   scripts/rest_demo.sh [options]
#
# Options:
#   -p, --port N       port to use (default: $EV_CHARGER_PORT or 8099)
#   -b, --build-dir D  build folder (default: build-cli)
#   -h, --help         show this help
# Exit code: 0 if every step worked, non-zero otherwise.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$root/build-cli"
port="${EV_CHARGER_PORT:-8099}"

usage() { sed -n '2,11p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }
while [[ $# -gt 0 ]]; do
  case "$1" in
    -p|--port)      port="${2:?missing port}"; shift 2 ;;
    -b|--build-dir) build="$(realpath -m "${2:?missing folder}")"; shift 2 ;;
    -h|--help)      usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if ss -ltn "sport = :$port" 2>/dev/null | grep -q LISTEN; then
  echo "Port $port is already in use. Use --port, or: pkill -f ev_charger_server" >&2
  exit 1
fi

echo "==> Building"
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$build" --target ev_charger_server

db="$(mktemp -u /tmp/ev-demo-XXXXXX.db)"
"$build/ev_charger_server" "$db" "$port" >/dev/null &
server=$!
trap 'kill "$server" 2>/dev/null || true; rm -f "$db"' EXIT

export EV_CHARGER_URL="http://127.0.0.1:$port"
for _ in $(seq 40); do
  curl -sf "$EV_CHARGER_URL/health" >/dev/null && break
  kill -0 "$server" 2>/dev/null || { echo "Server failed to start" >&2; exit 1; }
  sleep 0.25
done

client="$root/clients/ev_client.py"
export PYTHONDONTWRITEBYTECODE=1
echo; echo "==> Chargers"
python3 "$client" chargers | python3 -c "import sys,json;[print(f\"  {c['name']:14} {c['type']:8} {c['max_power_kw']:>5} kW  \${c['price_per_kwh']}/kWh\") for c in json.load(sys.stdin)]"
for charger in "ABB" "BTC" "Generic 11 kW"; do
  echo; echo "==> Charge on $charger"
  python3 "$client" demo "$charger" --budget 10 --step-seconds 600
done
echo; echo "==> Done. Server stopped."
