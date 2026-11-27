#!/usr/bin/env python3
"""Jira Cloud helper for the EV charger project (Python 3, standard library only).

  scripts/jira_tool.py cases [--dry-run]            create one Task per test case in jira/test_cases.json
  scripts/jira_tool.py bug --title T --body B       open a Bug, or comment on the open CI bug (--dry-run to preview)

Settings come from environment variables, never from files:
  JIRA_URL        https://your-site.atlassian.net
  JIRA_EMAIL      the Atlassian account email that owns the API token
  JIRA_API_TOKEN  API token from https://id.atlassian.com/manage-profile/security/api-tokens
  JIRA_PROJECT    project key, for example EVC
  JIRA_BUG_TYPE   optional: name of the issue type used for bugs (default Bug)
"""
import argparse, base64, json, os, sys, urllib.error, urllib.parse, urllib.request

PRIORITY = {"P1": "Highest", "P2": "High", "P3": "Medium"}
CASES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "jira", "test_cases.json")


def cfg():
    need = ["JIRA_URL", "JIRA_EMAIL", "JIRA_API_TOKEN", "JIRA_PROJECT"]
    missing = [k for k in need if not os.environ.get(k)]
    if missing:
        sys.exit("missing environment variables: " + ", ".join(missing))
    return {k: os.environ[k] for k in need}


def call(c, method, path, body=None):
    url = c["JIRA_URL"].rstrip("/") + path
    auth = base64.b64encode(f'{c["JIRA_EMAIL"]}:{c["JIRA_API_TOKEN"]}'.encode()).decode()
    req = urllib.request.Request(url, method=method, data=None if body is None else json.dumps(body).encode())
    req.add_header("Authorization", "Basic " + auth)
    req.add_header("Accept", "application/json")
    if body is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            raw = r.read()
            return r.status, (json.loads(raw) if raw else {})
    except urllib.error.HTTPError as e:
        raw = e.read().decode(errors="replace")
        try:
            return e.code, json.loads(raw)
        except ValueError:
            return e.code, {"raw": raw}


def search(c, jql):
    q = urllib.parse.urlencode({"jql": jql, "fields": "summary", "maxResults": 5})
    st, data = call(c, "GET", "/rest/api/3/search/jql?" + q)
    if st != 200:
        sys.exit(f"search failed ({st}): {data}")
    return data.get("issues", [])


def create(c, fields):
    st, data = call(c, "POST", "/rest/api/2/issue", {"fields": fields})
    if st == 400 and "priority" in json.dumps(data) and "priority" in fields:
        fields = {k: v for k, v in fields.items() if k != "priority"}   # project screen has no Priority field
        st, data = call(c, "POST", "/rest/api/2/issue", {"fields": fields})
    if st == 400 and "issuetype" in json.dumps(data):
        wanted = fields["issuetype"]["name"]
        st2, proj = call(c, "GET", "/rest/api/3/project/" + c["JIRA_PROJECT"])
        names = [t["name"] for t in proj.get("issueTypes", [])] if st2 == 200 else []
        sys.exit(f"issue type '{wanted}' is not available in {c['JIRA_PROJECT']}. "
                 f"Types in this space: {', '.join(names) or 'could not be read'}. "
                 "Add the type in Space settings, or set JIRA_BUG_TYPE (bugs) to one of the names above.")
    if st not in (200, 201):
        sys.exit(f"create failed ({st}): {data}")
    return data["key"]


def case_text(t):
    out = [f"*Type:* {t['type']}", f"*Priority:* {t['priority']}   *Severity:* {t['severity']}",
           f"*Starts from:* {t['starts_from']}", "", "h3. Objective", t["objective"], "", "h3. Prerequisites"]
    out += [f"* {p}" for p in t["prerequisites"]]
    out += ["", "h3. Test data", t["test_data"], "", "h3. Test steps and expected results",
            "||#||Step||Expected result||"]
    out += [f"|{i}|{s['step']}|{s['expected']}|" for i, s in enumerate(t["steps"], 1)]
    out += ["", "h3. Pass criteria", t["pass_criteria"], "", "h3. Automated coverage", str(t["automated"])]
    out += ["", "Run with:", "{code}"] + t["run"] + ["{code}"] if t.get("run") else []
    return "\n".join(out)


def cmd_cases(a):
    cases = json.load(open(CASES))
    c = None if a.dry_run else cfg()
    for t in cases:
        label = t["id"]
        summary = f"{t['id']}: {t['title']}"
        fields = {"summary": summary, "description": case_text(t), "issuetype": {"name": "Task"},
                  "labels": ["test-case", label, "severity-" + t["severity"].split()[0].lower()],
                  "priority": {"name": PRIORITY[t["priority"]]}}
        if a.dry_run:
            print(f"[dry run] would create: {summary}  ({fields['priority']['name']}, {fields['labels']})")
            continue
        fields["project"] = {"key": c["JIRA_PROJECT"]}
        found = search(c, f'project = {c["JIRA_PROJECT"]} AND labels = "{label}"')
        if found:
            print(f"exists  {found[0]['key']}  {summary}")
        else:
            print(f"created {create(c, fields)}  {summary}")


BUG_TEMPLATE = """h3. Summary
{body}

h3. Test case and step
(fill in: TC-xx, step number)

h3. Build
{build}

h3. Request and response / console output
{{code}}
(paste the exact request and full response)
{{code}}

h3. Expected result
(fill in)

h3. Actual result
(fill in)"""


def cmd_bug(a):
    build = a.build or os.environ.get("GITHUB_SHA", "unknown")[:7]
    body = a.body if a.ci else BUG_TEMPLATE.format(body=a.body, build=build)
    if a.dry_run:
        print(f"[dry run] summary: {a.title}\n{body}")
        return
    c = cfg()
    jql = f'project = {c["JIRA_PROJECT"]} AND labels = "ci-failure" AND statusCategory != Done'
    found = search(c, jql) if a.ci else []
    if found:
        key = found[0]["key"]
        st, data = call(c, "POST", f"/rest/api/2/issue/{key}/comment", {"body": body})
        if st not in (200, 201):
            sys.exit(f"comment failed ({st}): {data}")
        print(f"commented on {key}")
        return
    labels = ["ci-failure"] if a.ci else ["defect"]
    print("created", create(c, {"project": {"key": c["JIRA_PROJECT"]}, "summary": a.title, "description": body,
                                "issuetype": {"name": os.environ.get("JIRA_BUG_TYPE", "Bug")}, "labels": labels,
                                "priority": {"name": "High" if a.ci else "Medium"}}))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("cases"); p.add_argument("--dry-run", action="store_true"); p.set_defaults(f=cmd_cases)
    p = sub.add_parser("bug"); p.add_argument("--title", required=True); p.add_argument("--body", required=True)
    p.add_argument("--build"); p.add_argument("--ci", action="store_true", help="CI mode: reuse the open ci-failure bug")
    p.add_argument("--dry-run", action="store_true"); p.set_defaults(f=cmd_bug)
    a = ap.parse_args(); a.f(a)


if __name__ == "__main__":
    main()
