"""Send firmware.bin to the badge's Wi-Fi update endpoint (used by env:badge-ota).

Usage: ota_upload.py FIRMWARE_BIN HOST
The token comes from [ota] token in wifi_secrets.ini next to platformio.ini.
The upload runs through curl: on macOS, Local Network privacy can block Python
from reaching the badge while curl is allowed.
"""
import configparser
import os
import shutil
import subprocess
import sys


def main():
    firmware, host = sys.argv[1], sys.argv[2]
    secrets = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "wifi_secrets.ini")
    config = configparser.ConfigParser(interpolation=None)
    config.read(secrets, encoding="utf-8")
    token = config.get("ota", "token", fallback="")
    if not token:
        sys.exit(f"No [ota] token in {secrets}")
    curl = shutil.which("curl") or sys.exit("curl not found")

    size = os.path.getsize(firmware)
    print(f"Uploading {firmware} ({size} bytes) to http://{host}/api/v1/ota", flush=True)
    # -4: the badge also announces IPv6 addresses over mDNS, which are not
    # always routable from the computer. The token goes in via stdin, not argv.
    result = subprocess.run(
        [curl, "-4", "--fail-with-body", "--progress-bar", "--max-time", "180", "-X", "POST", "-H", "@-",
         "-H", "Content-Type: application/octet-stream", "--data-binary", f"@{firmware}", f"http://{host}/api/v1/ota"],
        input=f"Authorization: Bearer {token}\n", text=True, stdout=subprocess.PIPE)
    print(f"Badge: {result.stdout.strip()}")
    if result.returncode != 0:
        sys.exit(f"Upload failed (curl exit {result.returncode}). If the badge restarted, the update may have "
                 f"installed; check http://{host}/api/v1/status")


if __name__ == "__main__":
    main()
