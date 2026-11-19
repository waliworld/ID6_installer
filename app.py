#!/usr/bin/env python3
"""
TunedByWali NBT Evo Toolbox - the app.

Run this and it opens the toolbox in your browser. No Go, no build step, no
command line needed once packaged. Everything runs locally on this computer.

    python app.py                 # opens the toolbox
    python app.py --no-browser    # just print the URL
    python app.py --port 8730      # fixed port

When frozen with PyInstaller (the double-click .exe/.app), data files live next
to the bundle; otherwise next to this script.
"""
from __future__ import annotations
import argparse
import json
import sys
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


def base_dir() -> Path:
    if getattr(sys, "frozen", False):           # PyInstaller onefile/onedir
        return Path(getattr(sys, "_MEIPASS", Path(sys.executable).parent))
    return Path(__file__).resolve().parent


BASE = base_dir()
TOOLS = BASE / "tools"
INDEX = BASE / "source" / "index.html"

# Make the backend importable and load its op table in-process.
sys.path.insert(0, str(TOOLS))
for sub in ("addons", "coding", "apps"):
    sys.path.insert(0, str(TOOLS / sub))
import toolbox  # noqa: E402  (the shared OPS live here)

_shutdown = threading.Event()


class Handler(BaseHTTPRequestHandler):
    server_version = "TBW-Toolbox"

    def log_message(self, *_a):           # keep the console quiet
        pass

    def _send(self, code: int, body: bytes, ctype: str):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            try:
                self._send(200, INDEX.read_bytes(), "text/html; charset=utf-8")
            except OSError:
                self._send(500, b"index.html not found next to the app", "text/plain")
        else:
            self._send(404, b"not found", "text/plain")

    def do_POST(self):
        op = self.path.strip("/")
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length) if length else b""
        try:
            body = json.loads(raw.decode() or "{}")
        except ValueError:
            body = {}
        if op == "quit":
            self._send(200, b'{"Result":"Toolbox closed. You can close this tab."}',
                       "application/json")
            _shutdown.set()
            return
        fn = getattr(toolbox, "OPS", {}).get(op)
        if fn is None:
            self._send(404, b'{"Result":"unknown operation"}', "application/json")
            return
        try:
            result = fn(body)
        except Exception as e:  # never crash the server on a handler error
            result = {"Ok": False, "Result": f"backend error: {e}"}
        self._send(200, json.dumps(result).encode(), "application/json")


def main() -> int:
    ap = argparse.ArgumentParser(description="TunedByWali NBT Evo Toolbox")
    ap.add_argument("--port", type=int, default=0, help="fixed port (default: auto)")
    ap.add_argument("--no-browser", action="store_true")
    args = ap.parse_args()

    if not INDEX.exists():
        print(f"ERROR: cannot find the interface at {INDEX}. Keep the app together with its "
              "source/ and tools/ folders.", file=sys.stderr)
        return 1

    httpd = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    url = f"http://127.0.0.1:{httpd.server_address[1]}/"
    print("TunedByWali NBT Evo Toolbox is running.")
    print(f"  Open: {url}")
    print("  Close this window (or press Ctrl+C) to stop.")
    if not args.no_browser:
        threading.Timer(0.6, lambda: webbrowser.open(url)).start()

    server_thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    server_thread.start()
    try:
        _shutdown.wait()
    except KeyboardInterrupt:
        pass
    httpd.shutdown()
    return 0


def _run_tool() -> int:
    """Frozen self-reinvocation: `app --run-tool path|module <target> [args]`.
    Lets the packaged binary stand in for a python interpreter when the backend
    shells out to a bundled script/module."""
    import os
    import runpy
    kind, target, rest = sys.argv[2], sys.argv[3], sys.argv[4:]
    for p in os.environ.get("PYTHONPATH", "").split(os.pathsep):
        if p and p not in sys.path:
            sys.path.insert(0, p)
    sys.argv = [target] + rest
    if kind == "module":
        runpy.run_module(target, run_name="__main__", alter_sys=True)
    else:
        runpy.run_path(target, run_name="__main__")
    return 0


if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[1] == "--run-tool":
        sys.exit(_run_tool())
    sys.exit(main())
