# PHDays Badge Source Code

![LAST0063](https://github.com/user-attachments/assets/c6ef8c73-12f9-4978-b324-96bd69605260)

PHDays Badge is an interactive device made by Positive Labs for cybersecurity festival PHDays Fest. It can display images and animations on a 10x10 pixel screen. There is a set of some standard, but you can connect to it via Wi-Fi and draw your own image or animation. It also can play RTTTL melodies and has built-in RTTTL-editor.

The web UI source is in [`webui/`](webui), copied from the [PHDays-Badge-WebUI](https://github.com/nlef/PHDays-Badge-WebUI) repository (a fork of [pixel-art-react](https://github.com/jvalen/pixel-art-react), MIT).

3D models of the badge and its body details can be found in the [PHD_Badge_2025 models](https://github.com/Ushinbuy/PHD_Badge_2025) repository.

## Home Wi-Fi firmware

This version joins a 2.4 GHz WPA2-compatible home network automatically. The badge's
password-protected `phd2_…` hotspot is only a fallback.

1. Copy `wifi_secrets.example.ini` to `wifi_secrets.ini` and enter your network name
   and password. The file is gitignored; the build compiles the values into the firmware.
2. Build and flash (see below). The badge joins your network on boot.
3. Open the badge's IP address to use the pixel and music editors. The router lists
   the badge as `phdays-badge` (a DHCP reservation keeps its IP stable), and the IP is
   also printed on the USB serial console.

If home Wi-Fi has no connection for 2 minutes (wrong password, router off, different
location), the hotspot turns on. Join it and open `http://192.168.4.1/wifi` to enter
another network; the badge saves it to NVS flash and restarts. Settings saved this way
take precedence over `wifi_secrets.ini`. The hotspot turns off again as soon as home
Wi-Fi connects; while it is on, the badge retries home Wi-Fi every 30 seconds. Without
any configured network, the badge runs the hotspot only.

Credentials are never logged or returned by the settings API, and configuration
requests from the home-network interface are rejected. WPA3-only and 5 GHz-only
networks are not supported.

The editor uses same-origin API requests, so it works through either interface.
Automatic festival OTA downloads and the periodic Wi-Fi disconnect task are disabled.
USB flashing remains available. The existing display, sound, buttons, and saved-project
features are retained.

### Build and install

The firmware builds with [PlatformIO](https://platformio.org/). `platformio.ini`
pins `espressif32@7.1.3`, which provides ESP-IDF 6.1.0; PlatformIO downloads the
framework and toolchain on the first build.

```sh
pio run                  # build
pio run -t upload        # flash bootloader, partition table, otadata and app
pio device monitor       # serial console at 115200 baud
```

`pio run -t upload` writes the bootloader (`0x1000`), the partition table (`0x8000`),
a blank otadata (`0x19000`, so the badge boots `ota_0`) and the app (`ota_0`,
`0x20000`). It never writes NVS or `storage`, so Wi-Fi settings and saved projects
survive. Do not erase the whole chip. Before changing `partitions.csv`, back up the
flash: moving NVS or `storage` loses their contents.

### Web UI

The editor is a React app in `webui/` (Node.js 20 or newer). The firmware embeds
its production build from `main/ui/`:

```sh
python3 scripts/build_webui.py   # npm ci (first run), build, copy into main/ui/
pio run -e badge-ota -t upload   # embed and install it
```

The script fails if the build output no longer matches the embedded file list
(`board_build.embed_files` in `platformio.ini` and `EMBED_FILES` in
`main/CMakeLists.txt`). `main/ui/wifi.html` is firmware-only and not built from
`webui/`. For quick UI work, `npm run development` in `webui/` starts a dev
server; API requests go to the same origin (`/api/v1`).

### Updating over Wi-Fi

After one USB flash, later builds can be installed over Wi-Fi:

```sh
pio run -e badge-ota -t upload                          # badge at 192.168.1.45
pio run -e badge-ota -t upload --upload-port <badge IP>
```

This needs an `[ota] token` in `wifi_secrets.ini` (see `wifi_secrets.example.ini`);
without one, Wi-Fi updates are disabled. The upload goes to the inactive app slot
(`ota_0`/`ota_1`), is verified, and the badge restarts into it. While it runs, the
screen shows a scrolling download arrow, then fills one pixel per percent; after the
restart a check mark appears. The new firmware keeps itself only once it is reachable
for the next update again, through home Wi-Fi or, if that is out of range, the fallback
hotspot; otherwise it shows a cross after 3 minutes and rolls back to the previous
firmware, so a broken build cannot lock out Wi-Fi updates. The token is sent over
plain HTTP on the home network. Wi-Fi updates replace the app only; bootloader and
partition-table changes need USB.

Uploads run at 115200 baud because the badge's CH340 USB-serial adapter drops bytes
at higher rates on macOS. The home IP is also printed on the USB serial console after
DHCP succeeds.

Embedded UI files are listed in both `main/CMakeLists.txt` and
`board_build.embed_files` in `platformio.ini`; keep the two lists in sync.

The ESP-IDF configuration is `sdkconfig.defaults`, which holds only non-default
options; `sdkconfig` is generated from it on the first build and is not committed.
After changing options with `pio run -t menuconfig`, regenerate the defaults with
ESP-IDF's `save-defconfig` target
(`~/.platformio/packages/tool-ninja/ninja -C .pio/build/badge save-defconfig`) and
re-add `CONFIG_CJSON_NESTING_LIMIT=16`, which that target drops.
