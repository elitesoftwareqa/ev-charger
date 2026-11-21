# Launching the web front end on Windows (WSL)

This explains how to run the EV charger web page on a Windows PC using WSL
(Windows Subsystem for Linux). WSL runs a real Linux, so the project builds and
runs exactly as it does on Linux.

The web page is served by `ev_charger_server`. You start the server inside WSL
and open the page in your normal Windows browser.

> Tested: the project builds and passes all tests on Ubuntu 24.04 (x86-64) in CI.
> It has not been run inside WSL itself, so tell us if a step differs on your PC.

## 1. One-time setup

### Install WSL (skip if you already have an Ubuntu WSL)

In PowerShell as Administrator:

```powershell
wsl --install -d Ubuntu
```

Restart if asked, then open **Ubuntu** from the Start menu and create a Linux
username and password.

### Install the build tools

Inside the Ubuntu (WSL) terminal:

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build git python3 curl
```

### Get the code

Clone into the Linux home folder, **not** under `/mnt/c/...`. Builds on the
Windows drive are much slower, and Windows line endings can break the shell scripts.

```bash
git clone https://github.com/elitesoftwareqa/ev-charger.git ~/ev-charger
cd ~/ev-charger
```

## 2. Start the server

```bash
scripts/rest_server.sh
```

The first run downloads the dependencies (it needs internet) and builds the
server, which takes a few minutes. It then prints:

```
==> Starting. Web page: http://127.0.0.1:8099/   (database /tmp/ev-charger.db). Press Ctrl+C to stop.
```

Leave this terminal open. The server runs for as long as it is open.

## 3. Open the web page

In your **Windows** browser (Edge, Chrome, Firefox) go to:

**http://127.0.0.1:8099/**

WSL2 forwards `localhost` to Windows, so this normally works. You should see the
charger cards (ABB, BTC and the Generic 11 kW).

If the page does not load, find the WSL address and use it instead:

```bash
hostname -I
```

Then open `http://<that address>:8099/`.

## 4. Use the page

1. Pick a charger and enter a budget, then start a session.
2. Use the simulated clock controls (or Auto-run) to move time forward.
3. Watch the battery and cost chart update.
4. Stop the session to see the receipt.

## 5. Stop the server

Press **Ctrl+C** in the WSL terminal. Sessions are kept in the server's memory,
so they are lost when it stops.

## Options

```bash
scripts/rest_server.sh --port 9000          # use another port (default 8099)
scripts/rest_server.sh --db ~/chargers.db   # keep the database somewhere permanent
scripts/rest_server.sh --help
```

The default database is `/tmp/ev-charger.db`, which Linux may clear on restart.

## Troubleshooting

| Problem | Fix |
|---|---|
| `Missing tools: cmake ninja g++` | Run the `sudo apt install ...` line from step 1. |
| `Port 8099 is already in use` | Run `pkill -f ev_charger_server`, or start with `--port 9000`. |
| Browser cannot connect | Make sure the terminal still shows the server running, then try the `hostname -I` address. |
| `bad interpreter` or `\r` errors in scripts | The repo was cloned on the Windows drive. Clone it again inside WSL (`~/ev-charger`). |
| Slow build | Same cause: use the Linux home folder, not `/mnt/c`. |

## Notes

- The server has no login and listens on this PC only (127.0.0.1). Do not expose it to an untrusted network.
- A build made in WSL is a Linux program. It will not run from PowerShell. For a
  native Windows build, see the Windows job in `.github/workflows/ci.yml`.
- To run the tests: `scripts/run_tests.sh` (366 tests).
- Other docs: [REST service guide](EV-Charger-REST-Service.docx), [QA test guide](EV-Charger-QA-Test-Guide.docx), [project README](../README.md).
