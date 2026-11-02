#!/usr/bin/env python3
"""Sends one request to a running emulator and prints the answer.

    scripts/wsmcp.py [--port 7701] METHOD [PARAMS...]

PARAMS is a JSON object, or key=value pairs. A value is read as JSON when it
is one (60, true, ["a", "start"]), as a hexadecimal integer when it is
written 0x..., and as a string otherwise (F000:FFF0, a path):

    scripts/wsmcp.py emu.status
    scripts/wsmcp.py cart.load path="$HOME/Documents/WS/ROM/game.wsc"
    scripts/wsmcp.py emu.run_frames count=300
    scripts/wsmcp.py input.press buttons='["start"]'
    scripts/wsmcp.py memory.read address=F000:FFF0 size=16

The emulator is `wsmcp-cli --server tcp:PORT` (headless) or `wsmcp --server
tcp:PORT` (the desktop application); either keeps its state between requests.
The methods are docs/spec/control-protocol.md. The answer's `result` is printed
as JSON; an error is printed to stderr, and the exit status is 1.
"""

import argparse
import json
import socket
import sys


def parse_value(text):
    if text.lower().startswith("0x"):
        try:
            return int(text, 16)
        except ValueError:
            pass
    try:
        return json.loads(text)
    except ValueError:
        return text


def parse_params(items):
    if len(items) == 1 and items[0].lstrip().startswith("{"):
        return json.loads(items[0])
    params = {}
    for item in items:
        key, separator, value = item.partition("=")
        if not separator:
            sys.exit(f"wsmcp.py: expected key=value, got {item!r}")
        params[key] = parse_value(value)
    return params


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=7701)
    parser.add_argument("method")
    parser.add_argument("params", nargs="*")
    args = parser.parse_args()

    request = {"id": 1, "method": args.method, "params": parse_params(args.params)}
    try:
        connection = socket.create_connection(("127.0.0.1", args.port))
    except OSError as error:
        sys.exit(f"wsmcp.py: no emulator on port {args.port} ({error}); start one with "
                 f"wsmcp-cli --server tcp:{args.port}")
    with connection, connection.makefile("rw", encoding="utf-8") as stream:
        stream.write(json.dumps(request) + "\n")
        stream.flush()
        response = json.loads(stream.readline())
    if not response.get("ok"):
        error = response.get("error", {})
        print(f"{error.get('code')}: {error.get('message')}", file=sys.stderr)
        return 1
    print(json.dumps(response["result"], indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
