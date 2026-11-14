#!/usr/bin/env python3
"""Check what the privacy policy promises about a rendered compose stack.

Reads `docker compose ... config --format json` on stdin; the argument names the combination for
the messages. checks.yml runs it on two renderings of each combination: with --env-file .env.example,
and with no env file at all, which is what a host whose .env lacks a key runs and the only one where
a compose default shows (.env.example sets MHLLM_MEMORY=0, which would hide "${MHLLM_MEMORY:-1}").
Exits 1, with one GitHub annotation per problem, unless:

- coturn, the signalling server and the OIDC gate, whose logs carry viewers' network addresses and
  signed-in email addresses, log to Docker's "local" driver with a size cap, so that recreating
  them deletes their logs (systemd/ci-metahuman-log-purge@.timer does that weekly: 14 days at most);
- the signalling server publishes its ports on loopback only, and oauth2-proxy.cfg listens on
  loopback, so nothing reaches the player or the streamer around the gate;
- conversation memory is off: no compose default turns MHLLM_MEMORY on, and .env.example, which
  operators copy to .env, sets it to 0;
- an opening line, where the stack sets one, says that she is an AI.
"""

import json
from pathlib import Path
import re
import sys

HERE = Path(__file__).resolve().parent
CONNECTION_LOGS = ("turnserver", "signalling", "oidc-proxy")
OFF = {"", "0", "false", "no", "off"}


def problems(config: dict) -> list:
    services = config.get("services", {})
    found = []
    for name in CONNECTION_LOGS:
        logging = (services.get(name) or {}).get("logging") or {}
        options = logging.get("options") or {}
        if logging.get("driver") != "local" or not options.get("max-size") or not options.get("max-file"):
            found.append(f"{name} must log to the 'local' driver with max-size and max-file, not {logging}")
    for port in (services.get("signalling") or {}).get("ports") or []:
        if port.get("host_ip") != "127.0.0.1":
            found.append(f"signalling publishes {port.get('published')} beyond loopback: {port}")
    environment = (services.get("project") or {}).get("environment") or {}
    if str(environment.get("MHLLM_MEMORY") or "").strip().lower() not in OFF:
        found.append(f"project turns conversation memory on: MHLLM_MEMORY={environment['MHLLM_MEMORY']}")
    line = str(environment.get("MHLLM_OPENING_LINE") or "")
    if line and line.lower() != "none" and "an AI" not in line:
        found.append(f"the opening line does not say she is an AI: {line!r}")
    return found


def file_problems() -> list:
    found = []
    proxy = (HERE / "oauth2-proxy.cfg").read_text(encoding="utf-8")
    if not re.search(r'^http_address\s*=\s*"127\.0\.0\.1:\d+"', proxy, re.M):
        found.append("oauth2-proxy.cfg: http_address must be on 127.0.0.1")
    memory = re.findall(r"^MHLLM_MEMORY=(.*)$", (HERE / ".env.example").read_text(encoding="utf-8"), re.M)
    if len(memory) != 1 or memory[0].strip().lower() not in OFF:
        found.append(f".env.example must set MHLLM_MEMORY=0 once, not {memory}")
    return found


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: docker compose ... config --format json | check_compose_privacy.py <combination>")
    errors = problems(json.load(sys.stdin)) + file_problems()
    for error in errors:
        print(f"::error::{sys.argv[1]}: {error}")
    if not errors:
        print(f"{sys.argv[1]}: connection logs capped, ports on loopback, memory off")
    sys.exit(1 if errors else 0)
