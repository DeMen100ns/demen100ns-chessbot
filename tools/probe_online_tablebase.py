#!/usr/bin/env python3

import argparse
import json
import sys
import urllib.error
import urllib.parse
import urllib.request


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", required=True)
    parser.add_argument("--fen", required=True)
    parser.add_argument("--timeout-ms", type=int, default=200)
    args = parser.parse_args()

    encoded_fen = urllib.parse.quote(args.fen, safe="")
    separator = "&" if "?" in args.url else "?"
    request_url = f"{args.url}{separator}fen={encoded_fen}"

    try:
        with urllib.request.urlopen(request_url, timeout=max(args.timeout_ms, 1) / 1000.0) as response:
            payload = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        if exc.code == 404:
            return 0
        return 0
    except Exception:
        return 0

    moves = payload.get("moves") or []
    if not moves:
        return 0

    uci = moves[0].get("uci")
    if isinstance(uci, str) and uci:
        sys.stdout.write(uci)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
