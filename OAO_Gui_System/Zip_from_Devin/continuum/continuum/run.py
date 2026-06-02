#!/usr/bin/env python3
"""Continuum launcher.

Starts the local web server and opens your browser. Everything runs on this
machine; nothing leaves it.

Usage:
    python run.py                # start on http://127.0.0.1:8000
    python run.py --port 8800    # custom port
    python run.py --no-browser   # don't auto-open a browser
"""
from __future__ import annotations

import argparse
import threading
import webbrowser


def main() -> None:
    parser = argparse.ArgumentParser(description="Continuum — local knowledge & meaning engine")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--no-browser", action="store_true", help="do not open a browser window")
    args = parser.parse_args()

    try:
        import uvicorn  # noqa: F401
    except ImportError:
        raise SystemExit(
            "Dependencies missing. Activate your venv and run:\n"
            "    pip install -r requirements.txt"
        )

    url = f"http://{args.host}:{args.port}"
    print("=" * 56)
    print("  Continuum — local knowledge & meaning engine")
    print(f"  Open: {url}")
    print("  Press Ctrl+C to stop.")
    print("=" * 56)

    if not args.no_browser:
        threading.Timer(1.4, lambda: webbrowser.open(url)).start()

    import uvicorn
    uvicorn.run("app.server:app", host=args.host, port=args.port, log_level="info")


if __name__ == "__main__":
    main()
