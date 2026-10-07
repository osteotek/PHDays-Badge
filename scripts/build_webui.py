"""Build the web UI in webui/ and copy its output into main/ui/ for embedding.

Usage: python3 scripts/build_webui.py [--install]
  --install  run `npm ci` first (done automatically when node_modules is missing)

Fails if the built files no longer match board_build.embed_files in platformio.ini
(and EMBED_FILES in main/CMakeLists.txt), so new bundles are never left out.
"""
import configparser
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEBUI = os.path.join(ROOT, "webui")
DEPLOY = os.path.join(WEBUI, "deploy")
UI = os.path.join(ROOT, "main", "ui")
FIRMWARE_ONLY = {"wifi.html"}  # served by the firmware itself, not built from webui/
IGNORED = {"_redirects"}  # upstream Netlify config, unused on the badge


def embedded_files():
    config = configparser.ConfigParser(interpolation=None)
    config.read(os.path.join(ROOT, "platformio.ini"))
    return {os.path.basename(path) for path in config.get("env:badge", "board_build.embed_files").split()}


def npm(*args):
    executable = shutil.which("npm") or sys.exit("npm not found; install Node.js 20 or newer")
    subprocess.run([executable, *args], cwd=WEBUI, check=True)


def main():
    if "--install" in sys.argv or not os.path.isdir(os.path.join(WEBUI, "node_modules")):
        npm("ci", "--no-audit", "--no-fund")
    npm("run", "build")

    built = set(os.listdir(DEPLOY)) - IGNORED
    expected = embedded_files() - FIRMWARE_ONLY
    if built != expected:
        print("Built files do not match the firmware's embedded files:")
        for name in sorted(built - expected):
            print(f"  new, not embedded: {name}")
        for name in sorted(expected - built):
            print(f"  embedded, not built: {name}")
        sys.exit("Update board_build.embed_files in platformio.ini and EMBED_FILES in main/CMakeLists.txt")

    for name in sorted(built):
        shutil.copyfile(os.path.join(DEPLOY, name), os.path.join(UI, name))
    print(f"Copied {len(built)} files into main/ui/; rebuild the firmware to embed them.")


if __name__ == "__main__":
    main()
