#!/usr/bin/env python3
"""Command-line client for the ev_charger_server REST API. Standard library only.

  ev_client.py chargers
  ev_client.py charger ABB
  ev_client.py start ABB --budget 20 [--battery-percent 20] [--card-balance 100]
  ev_client.py advance ABB --seconds 60 [--kw 150]
  ev_client.py status ABB
  ev_client.py stop ABB
  ev_client.py demo ABB              # start, advance in steps, stop, print the receipt

The server address comes from --url or the EV_CHARGER_URL environment variable
(default http://127.0.0.1:8080).
"""
import argparse
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request


class ApiError(Exception):
    def __init__(self, status, message):
        super().__init__(f"HTTP {status}: {message}")
        self.status = status


class EvChargerClient:
    def __init__(self, base_url="http://127.0.0.1:8080", timeout=10):
        self.base = base_url.rstrip("/")
        self.timeout = timeout

    def _call(self, method, path, body=None):
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(self.base + path, data=data, method=method,
                                     headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                return json.load(resp)
        except urllib.error.HTTPError as e:
            try:
                message = json.load(e).get("error", e.reason)
            except Exception:
                message = e.reason
            raise ApiError(e.code, message) from None
        except urllib.error.URLError as e:
            raise ApiError(0, f"cannot reach {self.base}: {e.reason}") from None

    @staticmethod
    def _session(name):
        return f"/chargers/{urllib.parse.quote(name, safe='')}/session"

    def health(self):
        return self._call("GET", "/health")

    def chargers(self):
        return self._call("GET", "/chargers")

    def charger(self, name):
        return self._call("GET", f"/chargers/{urllib.parse.quote(name, safe='')}")

    def start(self, name, budget, battery_percent=None, battery_capacity_kwh=None,
              card_balance=None):
        body = {"budget": budget}
        for key, value in (("battery_percent", battery_percent),
                           ("battery_capacity_kwh", battery_capacity_kwh),
                           ("card_balance", card_balance)):
            if value is not None:
                body[key] = value
        return self._call("POST", self._session(name), body)

    def status(self, name):
        return self._call("GET", self._session(name))

    def advance(self, name, seconds, requested_kw=None):
        body = {"seconds": seconds}
        if requested_kw is not None:
            body["requested_kw"] = requested_kw
        return self._call("POST", self._session(name) + "/advance", body)

    def stop(self, name):
        return self._call("DELETE", self._session(name))


def show(obj):
    print(json.dumps(obj, indent=2))


def demo(client, name, args):
    print(f"Starting a ${args.budget:.2f} charge on {name}")
    st = client.start(name, args.budget, args.battery_percent)
    print(f"  battery {st['battery_percent']:.1f}%")
    steps = 0
    while st["charging"] and steps < args.max_steps:
        st = client.advance(name, args.step_seconds, args.kw)
        steps += 1
        print(f"  +{args.step_seconds:g}s  battery {st['battery_percent']:5.1f}%  "
              f"{st['energy_delivered_kwh']:6.2f} kWh  ${st['cost_so_far']:.2f}")
    out = client.stop(name)
    r = out["receipt"]
    print("Receipt:")
    print(f"  delivered  {r['energy_delivered_kwh']:.2f} kWh")
    print(f"  charged    ${r['amount_charged']:.2f}   released ${r['amount_released']:.2f}")
    print(f"  events     {', '.join(out['events'])}")


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--url", default=os.environ.get("EV_CHARGER_URL", "http://127.0.0.1:8080"))
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("chargers", help="list charger models")
    for cmd in ("charger", "status", "stop"):
        sub.add_parser(cmd).add_argument("name")

    s = sub.add_parser("start")
    s.add_argument("name")
    s.add_argument("--budget", type=float, required=True)
    s.add_argument("--battery-percent", type=float)
    s.add_argument("--battery-capacity", type=float, dest="battery_capacity_kwh")
    s.add_argument("--card-balance", type=float)

    a = sub.add_parser("advance")
    a.add_argument("name")
    a.add_argument("--seconds", type=float, required=True)
    a.add_argument("--kw", type=float)

    d = sub.add_parser("demo", help="run a whole charge and print the receipt")
    d.add_argument("name")
    d.add_argument("--budget", type=float, default=20.0)
    d.add_argument("--battery-percent", type=float, default=20.0)
    d.add_argument("--step-seconds", type=float, default=120.0)
    d.add_argument("--kw", type=float, default=150.0)
    d.add_argument("--max-steps", type=int, default=100)

    args = p.parse_args(argv)
    c = EvChargerClient(args.url)
    try:
        if args.cmd == "chargers":
            show(c.chargers())
        elif args.cmd == "charger":
            show(c.charger(args.name))
        elif args.cmd == "start":
            show(c.start(args.name, args.budget, args.battery_percent,
                         args.battery_capacity_kwh, args.card_balance))
        elif args.cmd == "status":
            show(c.status(args.name))
        elif args.cmd == "advance":
            show(c.advance(args.name, args.seconds, args.kw))
        elif args.cmd == "stop":
            show(c.stop(args.name))
        elif args.cmd == "demo":
            demo(c, args.name, args)
    except ApiError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
