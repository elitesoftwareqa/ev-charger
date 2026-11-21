#!/usr/bin/env bash
# Run the REST command-line client against the local server.
#
#   scripts/rest_client.sh COMMAND [args]
#   scripts/rest_client.sh chargers
#   scripts/rest_client.sh demo ABB --budget 20
#
# The server address is http://127.0.0.1:$EV_CHARGER_PORT (default port 8099),
# unless EV_CHARGER_URL is set. Start the server first: scripts/rest_server.sh
# All commands: scripts/rest_client.sh --help
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export EV_CHARGER_URL="${EV_CHARGER_URL:-http://127.0.0.1:${EV_CHARGER_PORT:-8099}}"
export PYTHONDONTWRITEBYTECODE=1

if [[ $# -eq 0 ]]; then
  sed -n '2,9p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
  exit 2
fi
exec python3 "$root/clients/ev_client.py" "$@"
