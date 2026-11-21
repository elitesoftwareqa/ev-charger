# ev-charger

[![CI](https://github.com/elitesoftwareqa/ev-charger/actions/workflows/ci.yml/badge.svg)](https://github.com/elitesoftwareqa/ev-charger/actions/workflows/ci.yml)

A C++20 simulation of an EV charging session, built as a testing playground:
chargers, a car battery, a payment terminal, a phone app, and a SQLite database
of charger models, a REST service in front of them, and a large GoogleTest
suite around it all.

**Everything here is simulated.** The ABB and BTC charger specs, the payment
terminal, and all numbers are made up for the simulation. Nothing talks to real
hardware or a real payment system.

## What's in it

| Part | What it does |
|---|---|
| `Charger` | State machine (available, connected, charging, finished, faulted), energy and cost tracking. `AbbCharger` tapers power at high battery levels; `BtcCharger` adds a session fee. |
| `Battery` | State of charge, charge levels (empty, low, normal, full), time to full. |
| `PaymentTerminal` / `PaymentSession` | Simulated card terminal (hold, capture, release) and a session that records battery energy before and after, and never bills above the hold. |
| `ChargingApp` | Simulated phone app that starts and stops a charge, including when the phone goes offline. |
| `ChargerRepository` | Interface for charger models. `ChargerDatabase` (SQLite) is the real one; `InMemoryChargerRepository` is a stub and `MockChargerRepository` (tests only) is a gmock mock. |
| `RestApi` / `ev_charger_server` | JSON-over-HTTP interface: list chargers, start a paid charge, advance the simulated clock, stop and get a receipt. `clients/ev_client.py` is a command-line client. |
| `constants.h` | Every tunable number in one place. |

## Build and run

Needs a C++20 compiler, CMake 3.14+, and internet on the first configure
(CMake downloads SQLite, GoogleTest, cpp-httplib and nlohmann/json, all pinned
by hash). Ninja is optional but recommended.

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/ev_charger            # demo; creates chargers.db in the current folder
./build/ev_charger my.db      # or use a database file of your choice
```

Or build and test in one step, without VS Code: `scripts/run_tests.sh`
(see [scripts/README.md](scripts/README.md)).

Charger models live in the database. After the first run you can edit them with
any SQLite tool, for example:

```bash
sqlite3 chargers.db "UPDATE chargers SET price_per_kwh = 0.55 WHERE name = 'ABB'"
```

## REST service

```bash
./build/ev_charger_server                  # [database] [port] [bind-address]; default port 8080
clients/ev_client.py chargers              # in another terminal
clients/ev_client.py demo ABB --budget 20  # a whole charge, then the receipt
```

Endpoints: `GET /health`, `GET /chargers`, `GET /chargers/{name}`,
`POST /chargers/{name}/session` (start), `GET .../session` (status),
`POST .../session/advance` (move the clock) and `DELETE .../session` (stop and
get the receipt). The server listens on 127.0.0.1 by default and has no
authentication. Details: [clients/README.md](clients/README.md) and
[docs/EV-Charger-REST-Service.docx](docs/EV-Charger-REST-Service.docx).

## Tests

363 tests in 8 groups, each its own program. QA testers: see
[docs/EV-Charger-QA-Test-Guide.docx](docs/EV-Charger-QA-Test-Guide.docx).

| Group | Covers |
|---|---|
| `battery` | Battery and charge levels |
| `charger` | Charger, ABB, BTC, shared contract tests for every type |
| `payment` | Payment terminal and payment session |
| `app` | Phone app |
| `database` | SQLite, repository stub and mock, charger factory |
| `rest` | REST service, tested through a real HTTP server |
| `payter_master` | End-to-end: charge triggered by a card tap |
| `app_master` | End-to-end: charge triggered by the app |

```bash
ctest --test-dir build --output-on-failure   # everything
ctest --test-dir build -R '^battery\.'       # one group
./build/battery_tests                        # or run a group directly
```

In VS Code, the CMake Tools extension shows these in the Testing panel
(`.vscode/settings.json` groups them by name).

### Coverage

```bash
pip install gcovr
scripts/coverage.sh          # HTML report: build-coverage/coverage/index.html
```

Line coverage is about 99.5%. CI fails if it drops below 95%.

## Docker

```bash
docker build -t ev-charger .
docker run --rm -v ev-data:/data ev-charger
```

The build runs the full test suite and fails if any test fails (skip with
`--build-arg RUN_TESTS=false`). To run the REST server instead of the demo:
`docker run --rm -p 127.0.0.1:8080:8080 --entrypoint ev_charger_server ev-charger /data/chargers.db 8080 0.0.0.0`.
The final image holds the demo and the REST server binaries and
runs as a non-root user; the database lives in the `/data` volume.

## CI

`.github/workflows/ci.yml` runs on every push and pull request: build and test,
coverage (with the 95% gate; the HTML report is uploaded as an artifact), and a
Docker image build with a smoke test. The build job and the Docker job also
smoke-test the REST server with the Python client.

## Layout

```
include/   headers            src/      implementation (demo: main.cpp, server: server_main.cpp)
tests/     GoogleTest suites   scripts/  run_tests.sh, coverage.sh
clients/   REST client         docs/     reports, presentation, REST and QA guides
```

## License

No license has been chosen yet.
