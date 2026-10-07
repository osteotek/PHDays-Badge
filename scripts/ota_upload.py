"""Send firmware.bin to the badge's Wi-Fi update endpoint (used by env:badge-ota).

Usage: ota_upload.py FIRMWARE_BIN HOST
The token comes from [ota] token in wifi_secrets.ini next to platformio.ini.
"""
import configparser
import http.client
import os
import sys

CHUNK = 16 * 1024


def main():
    firmware, host = sys.argv[1], sys.argv[2]
    secrets = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "wifi_secrets.ini")
    config = configparser.ConfigParser(interpolation=None)
    config.read(secrets, encoding="utf-8")
    token = config.get("ota", "token", fallback="")
    if not token:
        sys.exit(f"No [ota] token in {secrets}")

    size = os.path.getsize(firmware)
    print(f"Uploading {firmware} ({size} bytes) to http://{host}/api/v1/ota")
    conn = http.client.HTTPConnection(host, 80, timeout=120)
    conn.putrequest("POST", "/api/v1/ota")
    conn.putheader("Authorization", f"Bearer {token}")
    conn.putheader("Content-Type", "application/octet-stream")
    conn.putheader("Content-Length", str(size))
    conn.endheaders()
    sent = 0
    try:
        with open(firmware, "rb") as f:
            while chunk := f.read(CHUNK):
                conn.send(chunk)
                sent += len(chunk)
                print(f"\r  {sent * 100 // size:3d}%", end="", flush=True)
        print()
    except OSError as e:
        # The badge may reject the upload (bad token, wrong image) and close the
        # connection early; its response explains why.
        print(f"\n  connection closed after {sent} bytes ({e})")
    try:
        response = conn.getresponse()
        body = response.read().decode("utf-8", "replace")
    except OSError as e:
        # The upload may still have succeeded; the reply can be lost on a weak link.
        sys.exit(f"No reply from the badge ({e}). If it restarted, the update may have installed;"
                 f" check http://{host}/api/v1/system/info")
    print(f"Badge: {response.status} {body}")
    sys.exit(0 if response.status == 200 else 1)


if __name__ == "__main__":
    main()
