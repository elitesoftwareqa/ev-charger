# REST server and client

`ev_charger_server` serves the charger library over HTTP (JSON). `clients/ev_client.py`
is a command-line client for it (Python 3, standard library only).

## Run

```bash
scripts/run_tests.sh              # builds everything (build-cli/) and runs the tests
build-cli/ev_charger_server       # [database-file] [port] [bind-address], default chargers.db and 8080
clients/ev_client.py chargers
clients/ev_client.py demo ABB --budget 20
```

The server listens on 127.0.0.1 only and has no authentication. Set `EV_CHARGER_URL`
or pass `--url` to point the client elsewhere.

## Endpoints

| Method | Path | Does |
|---|---|---|
| GET | `/health` | `{"status":"ok"}` |
| GET | `/chargers` | List charger models and whether each is in use |
| GET | `/chargers/{name}` | One charger model |
| POST | `/chargers/{name}/session` | Start a paid charge. Body: `budget` (required), `card_balance` (100), `battery_percent` (20), `battery_capacity_kwh` (60) |
| GET | `/chargers/{name}/session` | What the app screen shows, plus notification events |
| POST | `/chargers/{name}/session/advance` | Move the simulated clock. Body: `seconds` (required), `requested_kw` (150) |
| DELETE | `/chargers/{name}/session` | Stop, settle payment, return the receipt |

Names with spaces must be URL-encoded (`Generic%2011%20kW`). One session per charger at a time.

| Status | Meaning |
|---|---|
| 201 | Session started |
| 400 | Missing or invalid field, or bad JSON |
| 402 | Card declined |
| 404 | No such charger, or no session on it |
| 409 | Charger already has a session, or is unavailable |

## curl example

```bash
curl -X POST localhost:8080/chargers/ABB/session -d '{"budget": 20}'
curl -X POST localhost:8080/chargers/ABB/session/advance -d '{"seconds": 120}'
curl -X DELETE localhost:8080/chargers/ABB/session
```

Sessions live in the server's memory and are lost on restart. The REST tests are the `rest` group (19 tests).
