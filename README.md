# ev-charger

[![CI](https://github.com/elitesoftwareqa/ev-charger/actions/workflows/ci.yml/badge.svg)](https://github.com/elitesoftwareqa/ev-charger/actions/workflows/ci.yml)

A C++20 simulation of an EV charging session, built as a testing playground:
chargers, a car battery, a payment terminal, a phone app, and a SQLite database
of charger models, with a large GoogleTest suite around them.

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
| `constants.h` | Every tunable number in one place. |

## Build and run

Needs a C++20 compiler, CMake 3.14+, and internet on the first configure
(CMake downloads SQLite and GoogleTest). Ninja is optional but recommended.

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/ev_charger            # demo; creates chargers.db in the current folder
./build/ev_charger my.db      # or use a database file of your choice
```

Charger models live in the database. After the first run you can edit them with
any SQLite tool, for example:

```bash
sqlite3 chargers.db "UPDATE chargers SET price_per_kwh = 0.55 WHERE name = 'ABB'"
```

## Tests

344 tests in 7 groups, each its own program:

| Group | Covers |
|---|---|
| `battery` | Battery and charge levels |
| `charger` | Charger, ABB, BTC, shared contract tests for every type |
| `payment` | Payment terminal and payment session |
| `app` | Phone app |
| `database` | SQLite, repository stub and mock, charger factory |
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
`--build-arg RUN_TESTS=false`). The final image holds only the demo binary and
runs as a non-root user; the database lives in the `/data` volume.

## CI

`.github/workflows/ci.yml` runs on every push and pull request: build and test,
coverage (with the 95% gate; the HTML report is uploaded as an artifact), and a
Docker image build with a smoke test.

## Layout

```
include/   headers          src/     implementation (+ demo in main.cpp)
tests/     GoogleTest suites scripts/ coverage.sh
```

## License

No license has been chosen yet.
