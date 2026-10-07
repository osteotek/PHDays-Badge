"""Build the web UI in webui/ into main/ui/index.html.gz for embedding.

Usage: python3 scripts/build_webui.py [--install]
  --install  run `npm ci` first (done automatically when node_modules is missing)

Runs the web UI tests, type-checks, builds one self-contained index.html and
gzips it deterministically, so an unchanged UI gives an identical file.
"""
import gzip
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEBUI = os.path.join(ROOT, "webui")
BUILT = os.path.join(WEBUI, "dist", "index.html")
EMBEDDED = os.path.join(ROOT, "main", "ui", "index.html.gz")


def npm(*args):
    executable = shutil.which("npm") or sys.exit("npm not found; install Node.js 22.12 or newer")
    subprocess.run([executable, *args], cwd=WEBUI, check=True)


def main():
    if "--install" in sys.argv or not os.path.isdir(os.path.join(WEBUI, "node_modules")):
        npm("ci", "--no-audit", "--no-fund")
    npm("test")
    npm("run", "build")
    with open(BUILT, "rb") as f:
        html = f.read()
    with open(EMBEDDED, "wb") as out:
        with gzip.GzipFile(filename="", mode="wb", fileobj=out, compresslevel=9, mtime=0) as gz:
            gz.write(html)
    print(f"main/ui/index.html.gz: {len(html)} bytes -> {os.path.getsize(EMBEDDED)} gzipped; rebuild the firmware to embed it.")


if __name__ == "__main__":
    main()
