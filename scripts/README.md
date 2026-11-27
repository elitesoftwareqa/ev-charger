# Scripts

## `run_tests.sh`: build and test without VS Code

Configures, builds and runs the tests with one command.

```bash
scripts/run_tests.sh
```

It needs a compiler, CMake and Ninja. If they are missing, the script says so;
install them with:

```bash
sudo apt-get install -y build-essential cmake ninja-build
```

The first run downloads SQLite and GoogleTest, so it needs internet. Later runs
only rebuild what changed.

### Options

| Option | What it does |
|---|---|
| `-g`, `--group NAME` | Run one test group: `battery`, `charger`, `payment`, `app`, `database`, `rest`, `payter_master`, `app_master` |
| `-r`, `--regex REGEX` | Run only tests whose name matches the regex |
| `-d`, `--demo` | Also run the demo program after the tests pass |
| `-c`, `--clean` | Delete the build folder first (full rebuild) |
| `-b`, `--build-dir DIR` | Use another build folder |
| `-j`, `--jobs N` | Number of parallel jobs (default: all cores) |
| `-l`, `--list` | List the tests instead of running them |
| `-h`, `--help` | Show the help text |

### Examples

```bash
scripts/run_tests.sh                      # build and run all 366 tests
scripts/run_tests.sh -g battery           # only the battery group
scripts/run_tests.sh -r 'ABB'             # only tests with "ABB" in the name
scripts/run_tests.sh -g app_master -d     # one group, then run the demo
scripts/run_tests.sh --list               # list every test name
scripts/run_tests.sh --clean              # rebuild from scratch
```

### Notes

- The script builds in `build-cli/`, not `build/`. VS Code uses `build/` with
  the Ninja generator, and a build from another tool in that folder can make
  VS Code's Configure fail. `build-cli/` is ignored by git.
- The exit code is 0 when everything passes and non-zero if the build or any
  test fails, so it can be used in other scripts.
- The demo writes its database to `build-cli/demo-chargers.db`, so it does not
  leave a `chargers.db` in the project folder.

## REST scripts: `rest_demo.sh`, `rest_server.sh`, `rest_client.sh`

Run the REST service and its command-line client without remembering the
commands. All three build into `build-cli/` and use port **8099** by default
(8080 is often taken); change it with `--port` or `EV_CHARGER_PORT`.

**One command: see it work.** Starts a throwaway server, runs a $10 charge on
the ABB, BTC and Generic chargers through the client, prints the receipts, then
stops the server:

```bash
scripts/rest_demo.sh
```

**Web page.** `rest_server.sh` also serves a browser interface at
`http://127.0.0.1:8099/`: pick a charger, start a charge, watch the battery and
cost, then stop and read the receipt. No command line needed.

**Two terminals: use it yourself.**

```bash
# Terminal 1: build if needed and start the server (Ctrl+C stops it)
scripts/rest_server.sh                 # options: -p PORT, -d DATABASE-FILE

# Terminal 2: send commands
scripts/rest_client.sh chargers
scripts/rest_client.sh start ABB --budget 20
scripts/rest_client.sh advance ABB --seconds 120
scripts/rest_client.sh stop ABB
scripts/rest_client.sh demo BTC --budget 10
```

If the port is already in use, `rest_server.sh` says so and exits instead of
starting. Details of every command and endpoint: [clients/README.md](../clients/README.md).

## `coverage.sh`: code coverage report

Builds with coverage on, runs the tests and writes an HTML report to
`build-coverage/coverage/index.html`. It needs `gcovr`
(`pip install gcovr`). CI runs it with a 95% line-coverage gate:

```bash
scripts/coverage.sh build-coverage --fail-under-line 95
```

## `jira_tool.py`: test cases and bugs in Jira

Creates and updates issues in the Jira space `EVC` (Python 3, standard library
only). It reads its settings from environment variables and never stores the API
token in a file:

```bash
export JIRA_URL=https://elitetestengineering.atlassian.net
export JIRA_EMAIL=you@example.com
export JIRA_PROJECT=EVC
read -s JIRA_API_TOKEN && export JIRA_API_TOKEN     # paste the token; nothing is echoed
```

| Command | What it does |
|---|---|
| `scripts/jira_tool.py cases [--dry-run]` | Create one Task per test case in `jira/test_cases.json` (TC-01 to TC-09). Cases that already exist are skipped |
| `scripts/jira_tool.py bug --title T --body B [--dry-run]` | Create a Bug using the defect template. `--ci` reuses the open `ci-failure` bug and adds a comment |
| `scripts/jira_tool.py filters [--dry-run]` | Create the saved filters the Jira dashboard uses (Test Cases, Open Bugs, Critical Bugs, CI Failures) |

The space needs a work type named `Bug`; set `JIRA_BUG_TYPE` if yours has another
name. CI runs the `--ci` form when a job fails on `main`, using four GitHub
secrets (`JIRA_URL`, `JIRA_EMAIL`, `JIRA_API_TOKEN`, `JIRA_PROJECT`). Full set-up:
[docs/EV-Charger-Jira-Workflow.docx](../docs/EV-Charger-Jira-Workflow.docx).
